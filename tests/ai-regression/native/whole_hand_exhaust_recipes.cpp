#include <algorithm>
#include <array>
#include <iostream>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) {
    ++checks;
    if(!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(bool frozen=false, RelicId relic=RelicId::INVALID)
        : game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=10; b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=500;
        for(int i=0;i<10;++i) {
            CardInstance c(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED);
            c.setUniqueId(b.cards.nextUniqueCardId++); b.cards.drawPile.push_back(c);
        }
    }
    void hand(CardId id,bool up=false) {
        CardInstance c(id,up); c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.hand[b.cards.cardsInHand++]=c;
    }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(CardId id,bool up=false) {
        hand(id,up); auto c=b.cards.hand[b.cards.cardsInHand-1];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); drain();
    }
};
void variants() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::SECOND_WIND,CardId::SEVER_SOUL,CardId::FIEND_FIRE,CardId::SENTINEL})
    for(bool up:{false,true}) for(bool frozen:{false,true}) {
        Fixture f(frozen); f.hand(CardId::STRIKE_RED); f.hand(CardId::DEFEND_RED);
        f.hand(CardId::WOUND); f.hand(CardId::INFLAME);
        auto twin=f.b; if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden order leak");
        const auto before=f.b.cards.drawPile;
        f.play(id,up);
        const bool fire=id==CardId::FIEND_FIRE, sentinel=id==CardId::SENTINEL;
        check(f.b.player.energy==10-(id==CardId::SECOND_WIND||sentinel?1:2),"play cost/no premature Sentinel refund");
        check(f.b.cards.cardsInHand==(sentinel?4:fire?0:1),"hand filter/count");
        if(!fire) check(f.b.cards.hand[0].uniqueId==110,"non-attack filter preserves original attack identity");
        check(f.b.cards.exhaustPile.size()==(sentinel?0:fire?5:3),"exhaust membership including Fiend Fire itself");
        check(f.b.cards.discardPile.size()==(fire?0:1),"played card lifecycle");
        check(f.b.player.block==(id==CardId::SECOND_WIND?3*(up?7:5):sentinel?(up?8:5):0),"base/up block");
        check(f.b.monsters.arr[0].curHp==500-(fire?4*(up?10:7):id==CardId::SEVER_SOUL?(up?22:16):0),"base/up damage/count");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        if(frozen) for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"Frozen Eye order changed");
    }
}
void combinations() {
    for(bool up:{false,true}) for(auto id:{CardId::SECOND_WIND,CardId::SEVER_SOUL,CardId::FIEND_FIRE}) {
        Fixture f; f.hand(CardId::SENTINEL,up); f.hand(CardId::WOUND); f.hand(CardId::STRIKE_RED);
        f.b.player.buff<PS::FEEL_NO_PAIN>(3); f.b.player.buff<PS::DARK_EMBRACE>(1);
        f.play(id,up);
        const int exhausted=id==CardId::FIEND_FIRE?4:2;
        check(f.b.cards.exhaustPile.size()==exhausted,"newly drawn cards not exhausted by batch");
        check(f.b.cards.cardsInHand==(id==CardId::FIEND_FIRE?4:3),"Dark Embrace draws incl played Fiend Fire");
        check(f.b.player.energy==10-(id==CardId::SECOND_WIND?1:2)+(up?3:2),"Sentinel on-exhaust refund exactly once");
        check(f.b.player.block==exhausted*3+(id==CardId::SECOND_WIND?2*(up?7:5):0),"per-card block and Feel No Pain");
    }
    for(bool up:{false,true}) for(int n:{0,1,3}) {
        Fixture block; for(int i=0;i<n;++i) block.hand(CardId::WOUND);
        block.b.player.dexterity=2; block.b.player.debuff<PS::FRAIL>(2,false);
        block.b.player.buff<PS::JUGGERNAUT>(5); block.play(CardId::SECOND_WIND,up);
        check(block.b.player.block==n*((up?9:7)*3/4),"Dex/Frail applied per-card before multiplication");
        check(block.b.monsters.arr[0].curHp==500-n*5,"one Juggernaut per block gain");
        Fixture fire; for(int i=0;i<n;++i) fire.hand(CardId::STRIKE_RED);
        fire.b.player.strength=3; fire.b.player.debuff<PS::WEAK>(2,false);
        fire.b.monsters.arr[0].addDebuff<MS::VULNERABLE>(2,false); fire.play(CardId::FIEND_FIRE,up);
        // Card damage is snapshotted once, before the exhaust callbacks.
        check(fire.b.monsters.arr[0].curHp==500-n*int((up?13:10)*.75*1.5),"Fiend Fire damage snapshot and hit count");
    }
    Fixture empty; empty.play(CardId::SEVER_SOUL);
    check(empty.b.monsters.arr[0].curHp==484&&empty.b.cards.exhaustPile.empty(),"empty hand Sever Soul still attacks");
    Fixture attacks; attacks.hand(CardId::STRIKE_RED); attacks.hand(CardId::BASH);
    attacks.play(CardId::SECOND_WIND);
    check(attacks.b.player.block==0&&attacks.b.cards.cardsInHand==2,"all-attack hand has no Second Wind payoff");
    Fixture noBlock; noBlock.hand(CardId::WOUND); noBlock.b.player.debuff<PS::NO_BLOCK>(2,false);
    noBlock.b.player.buff<PS::FEEL_NO_PAIN>(3); noBlock.play(CardId::SECOND_WIND);
    check(noBlock.b.player.block==3,"No Block does not suppress non-card exhaust block");
    Fixture order(false,RelicId::CHARONS_ASHES); order.hand(CardId::WOUND);
    order.b.monsters.arr[0].curHp=3; order.b.monsters.arr[0].buff<MS::THORNS>(5);
    order.play(CardId::SEVER_SOUL);
    check(order.b.player.curHp==70,"Sever Soul must exhaust/Ashes-kill before attack/Thorns");
    check(order.b.cards.exhaustPile.size()==1,"Sever Soul must exhaust before fatal damage");
    for(bool up:{false,true}) {
        Fixture chosen; chosen.hand(CardId::SENTINEL,up); chosen.hand(CardId::STRIKE_RED);
        chosen.play(CardId::TRUE_GRIT,true);
        check(chosen.b.inputState==InputState::CARD_SELECT,"True Grit opens exhaust choice");
        chosen.b.chooseExhaustOneCard(0); chosen.drain();
        check(chosen.b.player.energy==9+(up?3:2)&&chosen.b.player.block==9,
              "chosen Sentinel refunds energy but does not play its block effect");
        check(chosen.b.cards.cardsInHand==1&&chosen.b.cards.hand[0].id==CardId::STRIKE_RED,
              "chosen Sentinel preserves other card");
    }
}
}
int main() {
    variants(); combinations();
    std::cout<<"WHOLE_HAND_EXHAUST_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
