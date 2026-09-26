#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
#include "corruption_cost_watch.h"
using namespace sts;
namespace {
int checks=0,failures=0;
void check(bool ok,const char *msg){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game;BattleContext b;
    explicit Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0){
        game.floorNum=1;game.relics={};if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST);b.init(game);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;
        b.player.energy=20;b.player.curHp=70;b.player.maxHp=80;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        // Power cannot be retrieved by either Secret card; keep the deck nonempty.
        b.cards.createTempCardInDrawPile(0,CardInstance(CardId::INFLAME));
    }
    int add(CardId id,bool up=false){b.cards.createTempCardInHand(CardInstance(id,up));return b.cards.hand[b.cards.cardsInHand-1].uniqueId;}
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void use(int i){auto c=b.cards.hand[i];b.addToBotCard(CardQueueItem(c,0,b.player.energy));run();}
    void play(CardId id,bool up=false){add(id,up);use(b.cards.cardsInHand-1);}
    int source(CardId retrieval,CardInstance c){
        if(retrieval==CardId::EXHUME){c.setUniqueId(b.cards.nextUniqueCardId++);b.cards.exhaustPile.push_back(c);return c.uniqueId;}
        b.cards.createTempCardInDrawPile(0,c);return b.cards.drawPile[0].uniqueId;
    }
};
const CardInstance *findHand(const BattleContext &b,int uid){
    for(int i=0;i<b.cards.cardsInHand;++i)if(b.cards.hand[i].uniqueId==uid)return &b.cards.hand[i];
    return nullptr;
}
int sourceIndex(const BattleContext &b,CardId retrieval,int uid){
    const auto &pile=retrieval==CardId::EXHUME?b.cards.exhaustPile:b.cards.drawPile;
    for(int i=0;i<pile.size();++i)if(pile[i].uniqueId==uid)return i;return -1;
}
void cases(){
    for(auto id:{CardId::EXHUME,CardId::SECRET_TECHNIQUE,CardId::SECRET_WEAPON})for(bool up:{false,true})for(int count:{0,1,3}){
        Fixture f;const auto candidate=id==CardId::SECRET_WEAPON?CardId::RAMPAGE:CardId::DEFEND_RED;
        int uid=-1;for(int i=0;i<count;++i){CardInstance c(candidate);c.costForTurn=0;
            if(candidate==CardId::RAMPAGE)c.specialData=13;
            uid=f.source(id,c);
        }
        if(id==CardId::EXHUME)f.source(id,CardInstance(CardId::EXHUME,true));
        f.add(id,up);const bool legal=f.b.cards.hand[0].canUseOnAnyTarget(f.b);
        check(legal==(id==CardId::EXHUME||count>0),"Secret cards require a matching draw-pile card; Exhume may have no target");
        // An illegal Secret card stays in hand: invoke only its action for the no-op control.
        if(!legal){
            f.b.addToBot(Actions::DrawToHandAction(id==CardId::SECRET_TECHNIQUE?CardSelectTask::SECRET_TECHNIQUE:CardSelectTask::SECRET_WEAPON,
                id==CardId::SECRET_TECHNIQUE?CardType::SKILL:CardType::ATTACK));f.run();
            check(f.b.cards.cardsInHand==1&&f.b.player.energy==20,"empty Secret action is a no-op, not a legal card play");
            continue;
        }
        f.use(0);
        if(count>1){
            check(f.b.inputState==InputState::CARD_SELECT,"multiple eligible identities open selection");
            const int index=sourceIndex(f.b,id,uid);const auto a=search::Action(search::ActionType::SINGLE_CARD_SELECT,index);
            check(a.isValidAction(f.b),"matching choice is legal");a.execute(f.b);
        }
        check(f.b.inputState==InputState::PLAYER_NORMAL,"resolved retrieval returns to decision boundary");
        check(f.b.cards.cardsInHand==(count?1:0),"retrieve exactly one original card or no-op");
        if(count){const auto *c=findHand(f.b,uid);check(c!=nullptr,"selected identity returned");
            if(c){check(c->costForTurn==0,"temporary zero cost preserved");check(c->specialData==(candidate==CardId::RAMPAGE?13:0),"mutable growth preserved");}
            check(sourceIndex(f.b,id,uid)<0,"source card removed, not copied");
        }
        check(f.b.player.energy==20-(id==CardId::EXHUME&&!up?1:0),"retrieval card energy cost");
        const bool exhaust=id==CardId::EXHUME||!up;
        const auto &pile=exhaust?f.b.cards.exhaustPile:f.b.cards.discardPile;
        check(std::any_of(pile.begin(),pile.end(),[&](const auto &c){return c.id==id&&c.isUpgraded()==up;}),"base/plus exhaust lifecycle");
        check(corruption_cost_watch::differences(f.b.cards).empty(),"retrieval does not reach deferred Corruption cost boundary");
    }
}
void filtersAndCallbacks(){
    Fixture exhume;exhume.source(CardId::EXHUME,CardInstance(CardId::EXHUME));
    exhume.source(CardId::EXHUME,CardInstance(CardId::DEFEND_RED));exhume.source(CardId::EXHUME,CardInstance(CardId::BASH));
    exhume.play(CardId::EXHUME);
    check(!search::Action(search::ActionType::SINGLE_CARD_SELECT,0).isValidAction(exhume.b),"Exhume cannot retrieve another Exhume");
    search::Action(search::ActionType::SINGLE_CARD_SELECT,2).execute(exhume.b);
    check(exhume.b.cards.strikeCount==0,"retrieving non-Strike does not alter Strike count");
    Fixture strike;strike.source(CardId::EXHUME,CardInstance(CardId::STRIKE_RED));strike.play(CardId::EXHUME);
    check(strike.b.cards.strikeCount==1,"retrieved Strike rejoins active Strike count");
    for(auto id:{CardId::EXHUME,CardId::SECRET_TECHNIQUE,CardId::SECRET_WEAPON}){
        Fixture f;CardInstance c(id==CardId::SECRET_WEAPON?CardId::RAMPAGE:CardId::SENTINEL);
        const int uid=f.source(id,c);f.b.player.buff<PS::NO_DRAW>();
        f.b.player.buff<PS::EVOLVE>(2);f.b.player.buff<PS::FIRE_BREATHING>(6);
        f.b.player.buff<PS::DARK_EMBRACE>(1);f.b.player.buff<PS::FEEL_NO_PAIN>(3);
        const auto draw=f.b.cards.drawPile.size();f.play(id,true);
        check(findHand(f.b,uid)!=nullptr,"No Draw does not block selected movement");
        check(f.b.monsters.arr[0].curHp==1000,"movement is not a draw damage callback");
        check(f.b.player.block==(id==CardId::EXHUME?3:0),"only retrieval card's own exhaust triggers Feel No Pain");
        check(f.b.cards.drawPile.size()==draw-(id==CardId::EXHUME?0:1),"no extra draw from retrieval/blocked Dark Embrace");
    }
    // Status retrieval must not trigger Evolve or Fire Breathing; may retrieve unplayable cards.
    Fixture status;const int uid=status.source(CardId::EXHUME,CardInstance(CardId::BURN));
    status.b.player.buff<PS::EVOLVE>(2);status.b.player.buff<PS::FIRE_BREATHING>(6);status.play(CardId::EXHUME);
    check(findHand(status.b,uid)!=nullptr&&status.b.cards.cardsInHand==1&&status.b.monsters.arr[0].curHp==1000,"Exhume status movement has no draw callback");
    for(auto id:{CardId::EXHUME,CardId::SECRET_TECHNIQUE}){
        Fixture f;const int uid=f.source(id,CardInstance(CardId::DEFEND_RED));
        f.b.player.buff<PS::CORRUPTION>();f.play(id,true);
        const auto *c=findHand(f.b,uid);
        check(c&&c->cost==1&&c->costForTurn==0,"retrieved Skill receives Corruption temporary zero cost without replacing base cost");
        check(corruption_cost_watch::differences(f.b.cards).empty(),"retrieval Corruption path stays outside deferred cost boundary");
    }
    // Full-hand action controls: current card normally frees a hand slot before resolution.
    for(auto id:{CardId::EXHUME,CardId::SECRET_TECHNIQUE,CardId::SECRET_WEAPON}){
        Fixture f;const int uid=f.source(id,CardInstance(id==CardId::SECRET_WEAPON?CardId::BASH:CardId::DEFEND_RED));
        for(int i=0;i<10;++i)f.add(CardId::ANGER);
        f.b.addToBot(id==CardId::EXHUME?Actions::ExhumeAction():Actions::DrawToHandAction(
            id==CardId::SECRET_TECHNIQUE?CardSelectTask::SECRET_TECHNIQUE:CardSelectTask::SECRET_WEAPON,
            id==CardId::SECRET_TECHNIQUE?CardType::SKILL:CardType::ATTACK));f.run();
        check(f.b.cards.cardsInHand==10,"retrieval does not overflow hand capacity");
        check((sourceIndex(f.b,id,uid)>=0)==(id==CardId::EXHUME),"full hand: Exhume no-op versus Secret discard overflow");
    }
}
void roots(){
    const std::array<std::uint64_t,7> seeds={11,22,33,44,55,66,77};
    for(auto id:{CardId::EXHUME,CardId::SECRET_TECHNIQUE,CardId::SECRET_WEAPON})for(bool up:{false,true})for(bool frozen:{false,true}){
        Fixture f(frozen);const auto candidate=id==CardId::SECRET_WEAPON?CardId::RAMPAGE:CardId::DEFEND_RED;
        for(int i=0;i<3;++i){CardInstance c(candidate);c.costForTurn=0;f.source(id,c);}
        f.add(id,up);auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());twin.cardRandomRng=Random(999);
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(size_t i=0;i<f.b.cards.drawPile.size();++i)check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"paired canonical draw order before retrieval");
        f.use(0);twin.addToBotCard(CardQueueItem(twin.cards.hand[0],0,twin.player.energy));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        bool rejected=false;try{auto copy=f.b;public_sampling::resampleCombatContinuation(f.game,copy,seeds);}catch(const std::exception&){rejected=true;}
        check(rejected,"pending retrieval cannot be resampled mid-queue");
        const auto &pile=id==CardId::EXHUME?f.b.cards.exhaustPile:f.b.cards.drawPile;
        int idx=-1;for(int i=0;i<pile.size();++i)if(pile[i].id==candidate){idx=i;break;}
        search::Action(search::ActionType::SINGLE_CARD_SELECT,idx).execute(f.b);
        search::Action(search::ActionType::SINGLE_CARD_SELECT,idx).execute(twin);
        check(f.b.cards.hand[0].uniqueId==twin.cards.hand[0].uniqueId,"paired selected identity result");
        const auto before=f.b;public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.cards.hand[0].uniqueId==before.cards.hand[0].uniqueId&&f.b.cards.hand[0].costForTurn==before.cards.hand[0].costForTurn,"resolved root preserves moved identity and cost");
        if(frozen)for(size_t i=0;i<before.cards.drawPile.size();++i)check(f.b.cards.drawPile[i].uniqueId==before.cards.drawPile[i].uniqueId,"Frozen Eye order preserved after retrieval");
    }
}
}
int main(){cases();filtersAndCallbacks();roots();std::cout<<"PILE_RETRIEVAL_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
