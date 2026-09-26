#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) {
    ++checks; if(!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game; BattleContext b;
    Fixture(bool frozen=false,RelicId relic=RelicId::INVALID, bool multiple=false)
        : game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(multiple?MonsterEncounter::SMALL_SLIMES:MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=20; b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        if(multiple) b.monsters.arr[1].curHp=b.monsters.arr[1].maxHp=1000;
        for(int i=0;i<10;++i) b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
    }
    void drain() {b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();}
    void play(CardId id,bool up=false) {
        b.cards.createTempCardInHand(CardInstance(id,up)); auto c=b.cards.hand[b.cards.cardsInHand-1];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); drain();
    }
};
void variantsAndSampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::RUPTURE,CardId::RAGE,CardId::JUGGERNAUT,CardId::BLOOD_FOR_BLOOD})
    for(bool up:{false,true}) for(bool frozen:{false,true}) {
        Fixture f(frozen); auto twin=f.b;
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden order leak");
        const auto before=f.b.cards.drawPile; f.play(id,up);
        const int cost=id==CardId::RUPTURE?1:id==CardId::RAGE?0:id==CardId::JUGGERNAUT?2:up?3:4;
        check(f.b.player.energy==20-cost,"variant energy cost");
        check(f.b.player.block==0&&f.b.player.strength==0,"registration must not fire callback");
        check(f.b.monsters.arr[0].curHp==1000-(id==CardId::BLOOD_FOR_BLOOD?(up?22:18):0),"variant damage");
        const bool power=id==CardId::RUPTURE||id==CardId::JUGGERNAUT;
        check(f.b.cards.discardPile.size()==(power?0:1)&&f.b.cards.exhaustPile.empty(),"card lifecycle");
        if(id==CardId::RUPTURE) check(f.b.player.getStatus<PS::RUPTURE>()==(up?2:1),"Rupture stack");
        if(id==CardId::RAGE) check(f.b.player.getStatus<PS::RAGE>()==(up?5:3),"Rage stack");
        if(id==CardId::JUGGERNAUT) check(f.b.player.getStatus<PS::JUGGERNAUT>()==(up?7:5),"Juggernaut stack");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        if(frozen) for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"Frozen Eye order");
    }
}
void eventChains() {
    for(bool up:{false,true}) {
        Fixture f; f.play(CardId::RUPTURE,up); f.play(CardId::RUPTURE,!up);
        f.b.player.loseHp(f.b,2,true); f.drain(); check(f.b.player.strength==3,"Rupture stacks once per actual self loss");
        f.b.player.loseHp(f.b,2,false); f.drain(); check(f.b.player.strength==3,"external HP loss does not trigger Rupture");
        f.b.player.block=10; f.b.player.attacked(f.b,0,5); f.drain();
        check(f.b.player.strength==3&&f.b.player.curHp==66,"blocked attack does not trigger loss");
        Fixture rod(false,RelicId::TUNGSTEN_ROD); rod.play(CardId::RUPTURE,up);
        rod.b.player.loseHp(rod.b,1,true); rod.drain();
        check(rod.b.player.curHp==70&&rod.b.player.strength==0,"Rod-prevented loss must not trigger Rupture");
        Fixture buffer; buffer.play(CardId::RUPTURE,up); buffer.b.player.buff<PS::BUFFER>(1);
        buffer.b.player.loseHp(buffer.b,2,true); buffer.drain();
        check(buffer.b.player.curHp==70&&buffer.b.player.strength==0,"Buffer-prevented loss must not trigger Rupture");
        Fixture brutality; brutality.play(CardId::RUPTURE,up); brutality.play(CardId::BRUTALITY);
        search::Action(search::ActionType::END_TURN).execute(brutality.b);
        check(brutality.b.player.strength==(up?2:1),"Brutality/Rupture chain");

        Fixture rage; rage.play(CardId::RAGE,up); rage.play(CardId::RAGE,!up);
        rage.b.player.dexterity=9; rage.b.player.debuff<PS::FRAIL>(2,false); rage.b.player.debuff<PS::NO_BLOCK>(2,false);
        rage.play(CardId::INFLAME); check(rage.b.player.block==0,"Rage ignores power play");
        rage.play(CardId::TWIN_STRIKE); check(rage.b.player.block==8,"Rage triggers once per multi-hit card and bypasses card block modifiers");
        rage.play(CardId::DEFEND_RED); check(rage.b.player.block==8,"Rage ignores skill play");
        search::Action(search::ActionType::END_TURN).execute(rage.b);
        check(!rage.b.player.hasStatus<PS::RAGE>(),"Rage expires at turn end");
        rage.play(CardId::STRIKE_RED); check(rage.b.player.block==0,"expired Rage does not trigger");

        Fixture jug; jug.play(CardId::JUGGERNAUT,up); jug.play(CardId::JUGGERNAUT,!up);
        jug.b.player.strength=20; jug.b.player.debuff<PS::WEAK>(2,false);
        jug.b.monsters.arr[0].addDebuff<MS::VULNERABLE>(2,false);
        jug.b.player.gainBlock(jug.b,0); jug.drain(); check(jug.b.monsters.arr[0].curHp==1000,"zero block no damage");
        jug.b.player.block=999; jug.b.player.gainBlock(jug.b,3); jug.drain();
        check(jug.b.player.block==999&&jug.b.monsters.arr[0].curHp==988,"positive gain at cap and non-attack damage");
        Fixture chain; chain.play(CardId::JUGGERNAUT,up); chain.play(CardId::RAGE,up);
        chain.play(CardId::TWIN_STRIKE);
        check(chain.b.player.block==(up?5:3)&&chain.b.monsters.arr[0].curHp==1000-10-(up?7:5),"Rage/Juggernaut once per attack play");
        Fixture wind; wind.play(CardId::JUGGERNAUT,up); wind.play(CardId::FEEL_NO_PAIN);
        wind.b.cards.createTempCardInHand(CardInstance(CardId::WOUND)); wind.b.cards.createTempCardInHand(CardInstance(CardId::WOUND));
        wind.play(CardId::SECOND_WIND);
        check(wind.b.player.block==16&&wind.b.monsters.arr[0].curHp==1000-4*(up?7:5),"Second Wind and Feel No Pain are four separate block events");
    }
}
void bloodCosts() {
    for(bool up:{false,true}) {
        Fixture f; const int base=up?3:4;
        f.b.cards.createTempCardInHand(CardInstance(CardId::BLOOD_FOR_BLOOD,up));
        f.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::BLOOD_FOR_BLOOD,up));
        f.b.cards.createTempCardInDiscard(CardInstance(CardId::BLOOD_FOR_BLOOD,up));
        f.b.cards.moveToExhaustPile(CardInstance(CardId::BLOOD_FOR_BLOOD,up));
        f.b.player.block=10; f.b.player.attacked(f.b,0,5); f.drain();
        check(f.b.cards.hand[0].cost==base,"blocked attack no discount");
        f.b.player.loseHp(f.b,3,true); f.drain();
        check(f.b.cards.hand[0].cost==base-1&&f.b.cards.drawPile[0].cost==base-1&&f.b.cards.discardPile[0].cost==base-1,"one loss event discounts hand/draw/discard once");
        check(f.b.cards.exhaustPile[0].cost==base,"exhaust pile does not receive tookDamage callback");
        f.b.player.block=0; f.b.player.attacked(f.b,0,2); f.drain();
        check(f.b.cards.hand[0].cost==base-2,"enemy loss also discounts");
        for(int i=0;i<5;++i) {f.b.player.loseHp(f.b,1,false); f.drain();}
        check(f.b.cards.hand[0].cost==0&&f.b.cards.hand[0].costForTurn==0,"cost floor zero");
        auto twin=f.b; const std::array<std::uint64_t,7> seeds{1,2,3,4,5,6,7};
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        check(twin.cards.hand[0].cost==0&&twin.cards.hand[0].costForTurn==0,"sample preserves visible mutable cost");
        f.b.player.energy=0;
        check(f.b.cards.hand[0].canUse(f.b,0,false),"discounted card playable at zero energy");
    }
    for(int cost:{0,1,2,3,4,5}) for(int turnCost:{0,1,2,3,4,5,6}) {
        CardInstance c(CardId::BLOOD_FOR_BLOOD); c.cost=cost; c.costForTurn=turnCost;
        c.upgrade(); const int raw=cost<4?cost-1:3;
        const int expected=std::max(0,raw), expectedTurn=turnCost>0?std::max(0,raw+turnCost-cost):0;
        check(c.cost==expected&&c.costForTurn==expectedTurn,"upgrade must preserve prior discounts and temporary cost offsets");
        const int once=c.cost, onceTurn=c.costForTurn;
        c.upgrade(); check(c.cost==once&&c.costForTurn==onceTurn,"second upgrade is a no-op");
    }
}
void randomTargets() {
    std::set<int> outcomes;
    for(bool up:{false,true}) for(bool frozen:{false,true}) for(int seed=0;seed<32;++seed) {
        Fixture f(frozen,RelicId::INVALID,true); f.play(CardId::JUGGERNAUT,up);
        auto twin=f.b;
        twin.cardRandomRng=Random(987654321);
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        const std::array<std::uint64_t,7> seeds{11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        f.b.player.gainBlock(f.b,1); f.drain();
        twin.player.gainBlock(twin,1); twin.inputState=InputState::EXECUTING_ACTIONS; twin.executeActions();
        for(int i=0;i<2;++i) check(f.b.monsters.arr[i].curHp==twin.monsters.arr[i].curHp,"source RNG does not leak into random target");
        const int first=1000-f.b.monsters.arr[0].curHp, second=1000-f.b.monsters.arr[1].curHp;
        check(first+second==(up?7:5)&&(!first||!second),"one live enemy receives non-attack damage");
        outcomes.insert(first>0?0:1);
    }
    check(outcomes.size()==2,"sampled worlds exercise both random targets");
    for(bool up:{false,true}) {
        Fixture f; f.b.player.energy=0;
        f.b.cards.createTempCardInHand(CardInstance(CardId::RAGE,up));
        check(f.b.cards.hand[0].canUse(f.b,0,false),"Rage playable at zero energy");
    }
}
}
int main() {
    variantsAndSampling(); eventChains(); bloodCosts(); randomTargets();
    std::cout<<"EVENT_TRIGGER_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
