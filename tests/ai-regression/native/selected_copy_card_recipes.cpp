#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
#include "corruption_cost_watch.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
struct Fixture {
    GameContext game; BattleContext b;
    explicit Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0){
        game.floorNum=1;game.relics={};if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST);b.init(game);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;
        b.player.energy=20;b.player.curHp=70;b.player.maxHp=80;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(auto id:{CardId::DEFEND_RED,CardId::STRIKE_RED})b.cards.createTempCardInDrawPile(0,CardInstance(id));
    }
    int add(CardInstance c){b.cards.createTempCardInHand(c);return b.cards.hand[b.cards.cardsInHand-1].uniqueId;}
    void use(int index){auto c=b.cards.hand[index];b.addToBotCard(CardQueueItem(c,0,b.player.energy));b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void dual(bool up){add(CardInstance(CardId::DUAL_WIELD,up));use(b.cards.cardsInHand-1);}
    int handIndex(int uid){for(int i=0;i<b.cards.cardsInHand;++i)if(b.cards.hand[i].uniqueId==uid)return i;return -1;}
    void resolve(int uid){const auto a=search::Action(search::ActionType::SINGLE_CARD_SELECT,handIndex(uid));check(a.isValidAction(b),"selected attack/power legal");a.execute(b);}
};
std::vector<CardInstance> instances(const BattleContext &b,CardId id){
    std::vector<CardInstance> out;for(int i=0;i<b.cards.cardsInHand;++i)if(b.cards.hand[i].id==id)out.push_back(b.cards.hand[i]);
    for(auto c:b.cards.discardPile)if(c.id==id)out.push_back(c);return out;
}
void matrix(){
    for(auto id:{CardId::RAMPAGE,CardId::SEARING_BLOW,CardId::RITUAL_DAGGER,CardId::INFLAME,CardId::BLOOD_FOR_BLOOD})
    for(bool up:{false,true})for(bool manual:{false,true})for(int beforeHand:{2,9,10}){
        Fixture f;CardInstance c(id,true);
        if(id==CardId::SEARING_BLOW){c.upgrade();c.upgrade();}
        if(id==CardId::RAMPAGE)c.specialData=21;
        if(id==CardId::RITUAL_DAGGER)c.specialData=37;
        c.cost=2;c.costForTurn=0;c.freeToPlayOnce=true;
        const int uid=f.add(c);
        if(manual)f.add(CardInstance(CardId::BASH));
        while(f.b.cards.cardsInHand<beforeHand-1)f.add(CardInstance(CardId::DEFEND_RED));
        const int originalHand=f.b.cards.cardsInHand;
        f.dual(up);
        if(manual){check(f.b.inputState==InputState::CARD_SELECT,"multiple candidates suspend queue");
            check(f.b.cardSelectInfo.dualWield_CopyCount()==(up?2:1),"pending copy count is public base/plus amount");f.resolve(uid);}
        const int count=up?2:1;auto copies=instances(f.b,id);
        check(copies.size()==count+1,"source plus one/two copies, including discard overflow");
        check(f.b.cards.cardsInHand==std::min(10,originalHand+count),"hand capacity respected");
        check((f.handIndex(uid)>=0)==!manual,"original identity kept only on automatic path, matching original game");
        std::set<int> ids;
        for(auto copy:copies){
            check(ids.insert(copy.uniqueId).second,"copies have independent identities");
            check(copy.getUpgradeCount()==c.getUpgradeCount()&&copy.specialData==c.specialData,"upgrade count and mutable damage preserved");
            check(copy.cost==2&&copy.costForTurn==0&&copy.freeToPlayOnce,"combat/turn costs and free-once flag preserved");
        }
        check(f.b.player.energy==19,"Dual Wield costs one energy in both variants");
        check(f.b.cards.exhaustPile.empty(),"unmodified Dual Wield does not exhaust");
        check(f.b.inputState==InputState::PLAYER_NORMAL,"resolved copy returns to normal boundary");
        if(id==CardId::BLOOD_FOR_BLOOD)
            check(f.b.cards.handBloodCardCount+f.b.cards.discardPileBloodCardCount==count+1,"blood-card counters include overflow copies");
        check(corruption_cost_watch::differences(f.b.cards).empty(),"copy matrix does not reach deferred Corruption boundary");
    }
}
void interactions(){
    for(bool up:{false,true}){
        Fixture empty;empty.add(CardInstance(CardId::DEFEND_RED));empty.add(CardInstance(CardId::BURN));empty.dual(up);
        check(empty.b.cards.cardsInHand==2&&empty.b.player.energy==19,"no attack/power is a legal paid no-op");
        Fixture filters;filters.add(CardInstance(CardId::DEFEND_RED));const int uid=filters.add(CardInstance(CardId::STRIKE_RED));filters.add(CardInstance(CardId::INFLAME));filters.dual(up);
        check(!search::Action(search::ActionType::SINGLE_CARD_SELECT,0).isValidAction(filters.b),"Skills cannot be copied");filters.resolve(uid);
        check(filters.b.cards.strikeCount==3+(up?1:0),"new Strike copies update count; selected replacement does not double count");
        Fixture callbacks;callbacks.add(CardInstance(CardId::INFLAME));callbacks.b.player.buff<PS::NO_DRAW>();
        callbacks.b.player.buff<PS::CORRUPTION>();callbacks.b.player.buff<PS::DARK_EMBRACE>(1);callbacks.b.player.buff<PS::FEEL_NO_PAIN>(3);
        callbacks.dual(up);
        check(callbacks.b.player.energy==20&&callbacks.b.player.block==3,"Corruption makes Dual Wield free/exhaust; only own exhaustion triggers block");
        check(callbacks.b.cards.drawPile.size()==2&&callbacks.b.cards.cardsInHand==2+(up?1:0),"No Draw does not block copies; no extra draw");
        check(callbacks.b.player.getStatus<PS::STRENGTH>()==0,"copying a power does not play it");
        Fixture growth;CardInstance ramp(CardId::RAMPAGE);ramp.specialData=13;const int rid=growth.add(ramp);growth.dual(up);
        growth.use(growth.handIndex(rid));
        auto remaining=instances(growth.b,CardId::RAMPAGE);int unchanged=0,grown=0;
        for(auto c:remaining){unchanged+=c.specialData==13;grown+=c.specialData==18;}
        check(unchanged==(up?2:1)&&grown==1,"playing one Rampage grows only that identity");
    }
    for(bool manual:{false,true}){
        Fixture f;f.game.deck.cards[0]=Card(CardId::RITUAL_DAGGER);f.game.deck.cards[0].misc=15;
        CardInstance dagger(f.game.deck.cards[0]);f.add(dagger);f.b.cards.hand[0].uniqueId=0;
        if(manual)f.add(CardInstance(CardId::BASH));f.dual(false);if(manual)f.resolve(0);
        int index=0;while(f.b.cards.hand[index].id!=CardId::RITUAL_DAGGER)++index;
        if(!manual)index=f.handIndex(0);
        f.b.monsters.arr[0].curHp=1;f.use(index);f.b.updateCardsOnExit(f.game.deck);
        check(f.game.deck.cards[0].misc==(manual?15:18),"Ritual Dagger permanent reward follows original versus replaced identity");
    }
}
void roots(){
    const std::array<std::uint64_t,7> seeds={11,22,33,44,55,66,77};
    for(bool up:{false,true})for(bool manual:{false,true})for(bool frozen:{false,true}){
        Fixture f(frozen);CardInstance c(CardId::RAMPAGE);c.specialData=13;const int uid=f.add(c);
        if(manual)f.add(CardInstance(CardId::INFLAME));f.add(CardInstance(CardId::DUAL_WIELD,up));auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());twin.cardRandomRng=Random(999);
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        f.use(f.b.cards.cardsInHand-1);auto played=twin.cards.hand[twin.cards.cardsInHand-1];twin.addToBotCard(CardQueueItem(played,0,twin.player.energy));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        if(manual){bool rejected=false;try{auto suspended=f.b;public_sampling::resampleCombatContinuation(f.game,suspended,seeds);}catch(const std::exception&){rejected=true;}
            check(rejected,"pending copy choice cannot be resampled");f.resolve(uid);search::Action(search::ActionType::SINGLE_CARD_SELECT,0).execute(twin);}
        check(f.b.cards.cardsInHand==twin.cards.cardsInHand,"paired worlds resolve same hand count");
        for(int i=0;i<f.b.cards.cardsInHand;++i)check(f.b.cards.hand[i].uniqueId==twin.cards.hand[i].uniqueId&&f.b.cards.hand[i].specialData==twin.cards.hand[i].specialData,"paired copies preserve public identity/damage");
        auto before=f.b;public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.cards.nextUniqueCardId==before.cards.nextUniqueCardId,"resampling does not reuse generated IDs");
        for(int i=0;i<f.b.cards.cardsInHand;++i)check(f.b.cards.hand[i].uniqueId==before.cards.hand[i].uniqueId&&f.b.cards.hand[i].specialData==before.cards.hand[i].specialData,"resolved copy root preserves hand");
        if(frozen)for(size_t i=0;i<f.b.cards.drawPile.size();++i)check(f.b.cards.drawPile[i].uniqueId==before.cards.drawPile[i].uniqueId,"Frozen Eye order retained");
    }
}
}
int main(){matrix();interactions();roots();std::cout<<"SELECTED_COPY_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
