#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include "combat/BattleContext.h"
#include "game/Game.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message){++checks;if(!ok){++failures;if(failures<=12)std::cerr<<"FAIL: "<<message<<'\n';}}
struct Fixture {
    GameContext g; BattleContext b;
    Fixture():g(CharacterClass::IRONCLAD,123456789,0){
        g.floorNum=1;g.relics={};g.enterBattle(MonsterEncounter::CULTIST);b.init(g);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=3;
        b.player.curHp=70;b.player.maxHp=80;b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
    }
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void play(CardId id,bool up=false){b.cards.createTempCardInHand(CardInstance(id,up));b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand-1],0,b.player.energy));run();}
};
CardType poolType(CardId id){return id==CardId::CHRYSALIS?CardType::SKILL:CardType::ATTACK;}
void variants(){
    std::set<CardId> skills,attacks;
    for(CardId id:{CardId::CHRYSALIS,CardId::METAMORPHOSIS})for(bool up:{false,true})
    for(int existing:{0,1,7})for(int seed=1;seed<=256;++seed){
        Fixture f;f.b.cardRandomRng=Random(seed);auto expectedRng=f.b.cardRandomRng;
        std::vector<CardId> expected,selected;
        for(int i=0;i<existing;++i){auto old=i%2?CardId::STRIKE_RED:CardId::DEFEND_RED;f.b.cards.createTempCardInDrawPile(i,CardInstance(old));expected.push_back(old);}
        const int oldTop=existing?f.b.cards.drawPile.back().uniqueId:-1;
        const int count=up?5:3;
        for(int i=0;i<count;++i)selected.push_back(getTrulyRandomCardInCombat(expectedRng,CharacterClass::IRONCLAD,poolType(id)));
        // Original CardGroup.addToRandomSpot chooses [0, size-1], not [0, size].
        for(auto generated:selected){int index=expected.empty()?0:expectedRng.random(static_cast<int>(expected.size()-1));expected.insert(expected.begin()+index,generated);}
        f.b.player.buff<PS::NO_DRAW>();f.b.player.buff<PS::FEEL_NO_PAIN>(3);f.play(id,up);
        check(f.b.player.energy==1,"both generator variants cost two");
        check(f.b.cards.cardsInHand==0&&f.b.cards.discardPile.empty(),"generation into draw ignores No Draw without drawing");
        check(f.b.cards.exhaustPile.size()==1&&f.b.player.block==3,"generator exhausts once with callback");
        check(f.b.cards.drawPile.size()==expected.size(),"3/5 fresh cards added");
        std::set<int> ids;
        for(unsigned i=0;i<expected.size();++i){
            const auto &c=f.b.cards.drawPile[i];check(c.id==expected[i],"identity selection precedes ordered random insertions");ids.insert(c.uniqueId);
            if(c.uniqueId>=100+existing+1){
                CardInstance base(c.id);check(!c.upgraded,"upgraded generator makes base cards");
                check(c.cost==std::min<int>(0,base.cost)&&c.costForTurn==std::min<int>(0,base.cost),"only positive costs become combat zero; X stays X");
                (id==CardId::CHRYSALIS?skills:attacks).insert(c.id);
            }
        }
        check(ids.size()==expected.size(),"all generated identities are fresh");
        check(!existing||f.b.cards.drawPile.back().uniqueId==oldTop,"existing top survives random insertion");
        check(f.b.cardRandomRng.counter==expectedRng.counter,"pool and insertion RNG accounting matches original");
        f.b.cards.resetAttributesAtEndOfTurn();
        for(const auto &c:f.b.cards.drawPile)check(c.cost==c.costForTurn,"combat reduction survives end turn");
    }
    check(skills.size()==28&&attacks.size()==28,"fixed corpus reaches full Ironclad non-healing pools");
    check(attacks.count(CardId::WHIRLWIND)&&!attacks.count(CardId::FEED)&&!attacks.count(CardId::REAPER),"X included, healing excluded");
}
void timing(){
    for(CardId id:{CardId::CHRYSALIS,CardId::METAMORPHOSIS})for(bool up:{false,true}){
        Fixture f;Random expected=f.b.cardRandomRng;std::multiset<CardId> selected,actual;
        for(int i=0;i<(up?5:3);++i)selected.insert(getTrulyRandomCardInCombat(expected,CharacterClass::IRONCLAD,poolType(id)));
        f.b.cards.createTempCardInHand(CardInstance(id,up));f.b.playCardQueueItem(CardQueueItem(f.b.cards.hand[0],0,3));
        check(f.b.cardRandomRng.counter==expected.counter,"identity chosen at use, not deferred until action execution");
        check(f.b.cards.drawPile.empty(),"insertion remains queued");
        // Diagnostic interleaving, not a claim that this exact sequence arises naturally.
        f.b.cardRandomRng.random(50);f.run();for(const auto &c:f.b.cards.drawPile)actual.insert(c.id);
        check(selected==actual,"later random consumption cannot change chosen cards");
    }
}
void callbacksAndX(){
    for(CardId id:{CardId::CHRYSALIS,CardId::METAMORPHOSIS}){
        Fixture f;f.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::DEFEND_RED));int top=f.b.cards.drawPile.back().uniqueId;
        f.b.player.buff<PS::DARK_EMBRACE>(1);f.play(id,true);
        check(f.b.cards.cardsInHand==1&&f.b.cards.hand[0].uniqueId==top,"exhaust draw takes unchanged old top after generation");
        check(f.b.cards.drawPile.size()==5,"new cards remain below consumed top");
        Fixture empty;empty.b.player.buff<PS::DARK_EMBRACE>(1);empty.play(id);
        check(empty.b.cards.cardsInHand==1&&empty.b.cards.drawPile.size()==2,"empty-pile generation precedes exhaust draw");
    }
    Fixture x;int seed=1;
    for(;seed<10000;++seed){Random rng(seed);if(getTrulyRandomCardInCombat(rng,CharacterClass::IRONCLAD,CardType::ATTACK)==CardId::WHIRLWIND)break;}
    check(seed<10000,"audit seed reaches Whirlwind first");x.b.cardRandomRng=Random(seed);x.b.player.buff<PS::DARK_EMBRACE>(1);x.play(CardId::METAMORPHOSIS);
    check(x.b.cards.hand[0].id==CardId::WHIRLWIND&&x.b.cards.hand[0].cost==-1,"generated Whirlwind retains X in hand");
    int hp=x.b.monsters.arr[0].curHp;x.b.player.energy=3;
    x.b.addToBotCard(CardQueueItem(x.b.cards.hand[0],0,3));x.run();
    check(x.b.player.energy==0&&x.b.monsters.arr[0].curHp==hp-15,"generated Whirlwind spends current energy and deals X attacks");
    Fixture corruption;corruption.b.player.buff<PS::CORRUPTION>();corruption.play(CardId::CHRYSALIS);
    check(corruption.b.player.energy==3,"Corruption makes generator free");
    for(const auto &c:corruption.b.cards.drawPile)check(c.cost==0&&c.costForTurn==0,"generated skills stay combat zero under Corruption");
}
void roots(){
    for(CardId id:{CardId::CHRYSALIS,CardId::METAMORPHOSIS})for(int seed=1;seed<=24;++seed){
        Fixture f;for(int i=0;i<8;++i)f.b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
        const int top=f.b.cards.drawPile.back().uniqueId;auto twin=f.b;std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end()-1);twin.cardRandomRng=Random(999999);twin.shuffleRng=Random(333333);
        std::array<std::uint64_t,7> seeds={11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.g,f.b,seeds,top);public_sampling::resampleCombatContinuation(f.g,twin,seeds,top);
        f.play(id,true);twin.cards.createTempCardInHand(CardInstance(id,true));twin.addToBotCard(CardQueueItem(twin.cards.hand[0],0,3));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        for(unsigned i=0;i<f.b.cards.drawPile.size();++i)check(f.b.cards.drawPile[i].id==twin.cards.drawPile[i].id&&f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"paired worlds ignore original hidden order/RNG");
        check(f.b.cards.drawPile.back().uniqueId==top,"known top preserved through generation");
        std::multiset<std::array<int,4>> before,after;
        for(const auto &c:f.b.cards.drawPile)before.insert({static_cast<int>(c.id),c.uniqueId,c.cost,c.costForTurn});
        public_sampling::resampleCombatContinuation(f.g,f.b,seeds,top);
        for(const auto &c:f.b.cards.drawPile)after.insert({static_cast<int>(c.id),c.uniqueId,c.cost,c.costForTurn});
        check(before==after&&f.b.cards.drawPile.back().uniqueId==top,"resolved resampling preserves generated multiset/cost and known top");
    }
}
}
int main(){variants();timing();callbacksAndX();roots();std::cout<<"RANDOM_DRAW_GENERATION_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
