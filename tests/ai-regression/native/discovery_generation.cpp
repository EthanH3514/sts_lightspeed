#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include "combat/BattleContext.h"
#include "constants/CardPools.h"
#include "game/Game.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *m){++checks;if(!ok){++failures;if(failures<=12)std::cerr<<"FAIL: "<<m<<'\n';}}
struct Fixture {
    GameContext g; BattleContext b;
    Fixture():g(CharacterClass::IRONCLAD,123456789,0){
        g.floorNum=1;g.relics={};g.enterBattle(MonsterEncounter::CULTIST);b.init(g);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=3;
        b.player.curHp=70;b.player.maxHp=80;b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
    }
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void play(CardId id,bool up=false){b.cards.createTempCardInHand(CardInstance(id,up));b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand-1],0,b.player.energy));run();}
    void choose(int index){auto a=search::Action(search::ActionType::SINGLE_CARD_SELECT,index);check(a.isValidAction(b),"candidate choice legal");a.execute(b);}
};
void variants(){
    std::set<CardId> seen;
    for(bool up:{false,true})for(int seed=1;seed<=128;++seed)for(int choice=0;choice<3;++choice){
        Fixture f;f.b.cardRandomRng=Random(seed);auto rng=f.b.cardRandomRng;
        auto expected=generateDiscoveryCards(rng,CharacterClass::IRONCLAD,CardType::INVALID);
        f.b.player.buff<PS::NO_DRAW>();f.b.player.buff<PS::FEEL_NO_PAIN>(3);f.play(CardId::DISCOVERY,up);
        check(f.b.inputState==InputState::CARD_SELECT,"Discovery suspends at public selection");
        auto offered=f.b.cardSelectInfo.cards;
        check(offered==expected&&f.b.cardRandomRng.counter==rng.counter,"three unique pool draws with duplicate rejection");
        check(std::set<CardId>(offered.begin(),offered.end()).size()==3,"candidates are distinct");
        for(auto id:offered){seen.insert(id);check(id!=CardId::FEED&&id!=CardId::REAPER&&id!=CardId::BASH&&id!=CardId::DEFEND_RED&&id!=CardId::STRIKE_RED,"healing and starter cards excluded");}
        check(f.b.cards.cardsInHand==0,"candidate previews are not inserted cards");
        f.choose(choice);
        check(f.b.inputState==InputState::PLAYER_NORMAL&&f.b.player.energy==2,"choice resolves without additional energy spending");
        check(f.b.cards.cardsInHand==1,"both variants generate one card despite No Draw");
        auto c=f.b.cards.hand[0];auto base=CardInstance(offered[choice]);
        check(c.id==offered[choice]&&!c.upgraded&&c.uniqueId==101,"fresh base copy of only the chosen card");
        check(c.cost==base.cost&&c.costForTurn==(base.cost>=0?0:base.cost),"normal combat cost and temporary zero except X");
        check(f.b.cards.exhaustPile.size()==(up?0u:1u)&&f.b.cards.discardPile.size()==(up?1u:0u),"upgrade removes generator exhaust");
        check(f.b.player.block==(up?0:3),"only base generator triggers intrinsic exhaustion");
        f.b.cards.resetAttributesAtEndOfTurn();check(f.b.cards.hand[0].costForTurn==base.cost,"temporary zero expires");
    }
    check(seen.size()==70,"fixed corpus covers all seventy Ironclad combat candidates");
}
void interactions(){
    for(bool up:{false,true}){
        Fixture f;for(int i=0;i<10;++i)f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        f.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::DISCOVERY,up));
        f.b.playTopCardInDrawPile(0,true);f.run();const auto selected=f.b.cardSelectInfo.cards[0];f.choose(0);
        check(f.b.cards.cardsInHand==10,"full hand autoplay still offers selection");
        check(f.b.cards.discardPile.size()==1&&f.b.cards.discardPile[0].id==selected,"full-hand generated result overflows to discard");
        check(f.b.cards.discardPile[0].costForTurn==(CardInstance(selected).cost>=0?0:-1),"overflow retains temporary cost");
        f.b.cards.resetAttributesAtEndOfTurn();check(f.b.cards.discardPile[0].costForTurn==CardInstance(selected).cost,"overflow cost resets");
        Fixture draw;draw.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::DEFEND_RED));draw.b.player.buff<PS::DARK_EMBRACE>(1);draw.play(CardId::DISCOVERY,up);draw.choose(0);
        check(draw.b.cards.cardsInHand==(up?1:2),"base exhaust callback draws after generation; upgrade does not exhaust");
        Fixture corruption;corruption.b.player.buff<PS::CORRUPTION>();corruption.play(CardId::DISCOVERY,up);corruption.choose(0);
        check(corruption.b.player.energy==3&&corruption.b.cards.exhaustPile.size()==1,"Corruption makes either generator free and exhausted");
    }
    for(int hits:{0,1,2,4,6}){
        Fixture f;for(int i=0;i<hits;++i)f.play(CardId::BLOODLETTING);
        int seed=1,index=-1;
        for(;seed<10000;++seed){Random rng(seed);auto ids=generateDiscoveryCards(rng,CharacterClass::IRONCLAD,CardType::INVALID);for(int i=0;i<3;++i)if(ids[i]==CardId::BLOOD_FOR_BLOOD)index=i;if(index>=0)break;}
        check(index>=0,"audit corpus offers Blood for Blood");f.b.cardRandomRng=Random(seed);f.play(CardId::DISCOVERY);f.choose(index);
        check(f.b.cards.hand[0].cost==std::max(0,4-hits)&&f.b.cards.hand[0].costForTurn==0,"chosen Blood for Blood inherits prior damage events");
        f.b.cards.resetAttributesAtEndOfTurn();check(f.b.cards.hand[0].costForTurn==std::max(0,4-hits),"Blood for Blood reset retains prior reduction");
    }
}
void roots(){
    for(int seed=1;seed<=24;++seed){
        Fixture f;for(int i=0;i<8;++i)f.b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
        auto twin=f.b;std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());twin.cardRandomRng=Random(99999);
        std::array<std::uint64_t,7> seeds={11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.g,f.b,seeds);public_sampling::resampleCombatContinuation(f.g,twin,seeds);
        f.play(CardId::DISCOVERY,true);twin.cards.createTempCardInHand(CardInstance(CardId::DISCOVERY,true));twin.addToBotCard(CardQueueItem(twin.cards.hand[0],0,3));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        check(f.b.cardSelectInfo.cards==twin.cardSelectInfo.cards,"paired sampled worlds offer same candidates independent of true hidden state");
        bool rejected=false;try{public_sampling::resampleCombatContinuation(f.g,f.b,seeds);}catch(const std::runtime_error&){rejected=true;}check(rejected,"pending selection cannot be resampled");
        f.choose(0);auto a=search::Action(search::ActionType::SINGLE_CARD_SELECT,0);a.execute(twin);
        check(f.b.cards.hand[0].id==twin.cards.hand[0].id,"paired choice resumes queues consistently");
        auto chosen=f.b.cards.hand[0];public_sampling::resampleCombatContinuation(f.g,f.b,seeds);
        check(chosen.id==f.b.cards.hand[0].id&&chosen.uniqueId==f.b.cards.hand[0].uniqueId&&chosen.costForTurn==f.b.cards.hand[0].costForTurn,"resolved roots retain chosen identity and cost");
    }
}
}
int main(){variants();interactions();roots();std::cout<<"DISCOVERY_GENERATION_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
