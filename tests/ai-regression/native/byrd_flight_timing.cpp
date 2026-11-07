#include <iostream>
#include <stdexcept>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0;
void check(bool ok, const char *why) { ++checks; if(!ok) throw std::runtime_error(why); }
struct Fixture {
    GameContext g;
    BattleContext b;
    Fixture() : g(CharacterClass::IRONCLAD,7,0) {
        g.act=2; g.enterBattle(MonsterEncounter::CHOSEN_AND_BYRDS);
        b.init(g); b.executeActions();
        b.monsters.arr[1].damage(b,99999);
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=100; b.player.curHp=b.player.maxHp=1000;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
    }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(CardId id, bool up=false) {
        b.cards.createTempCardInHand(CardInstance(id,up));
        auto c=b.cards.hand[b.cards.cardsInHand-1];
        check(c.canUse(b,0,false),"fixture card cannot play");
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); drain();
    }
};
void randomMultiHit() {
    // Original FlightPower queues reduction at the bottom. All four random
    // attacks calculate damage before those reductions; the fourth stays halved.
    for(bool up : {true,false}) for(int strength : {0,3,10}) {
        Fixture f; f.b.player.strength=strength;
        const int hits=up?4:3;
        f.play(CardId::SWORD_BOOMERANG,up);
        check(f.b.monsters.arr[0].curHp==1000-hits*((3+strength)/2),
              "Sword Boomerang loses Flight before all queued attacks resolve");
        check(!f.b.monsters.arr[0].hasStatus<MS::FLIGHT>() &&
              f.b.monsters.arr[0].moveHistory[0]==MMID::BYRD_STUNNED,
              "queued Flight loss did not ground Byrd");
        const auto hp=f.b.monsters.arr[0].curHp;
        f.play(CardId::STRIKE_RED);
        check(hp-f.b.monsters.arr[0].curHp==6+strength,"next card must hit grounded Byrd at full damage");
    }
}
void queueAndDamageControls() {
    Fixture queued; auto &m=queued.b.monsters.arr[0];
    const auto intent=m.moveHistory[0];
    m.attacked(queued.b,1);
    check(m.getStatus<MS::FLIGHT>()==3 && m.moveHistory[0]==intent,"Flight changed before queued reduction");
    // Exact clones must operate on their own monster, not a captured source pointer.
    auto twin=queued.b; queued.drain();
    twin.inputState=InputState::EXECUTING_ACTIONS; twin.executeActions();
    check(m.getStatus<MS::FLIGHT>()==2 && twin.monsters.arr[0].getStatus<MS::FLIGHT>()==2,
          "Flight callback did not resolve on cloned context");
    m.block=20; m.attacked(queued.b,6); queued.drain();
    check(m.getStatus<MS::FLIGHT>()==2,"blocked attack reduced Flight");
    m.block=0; m.attacked(queued.b,0); m.damage(queued.b,2); queued.drain();
    check(m.getStatus<MS::FLIGHT>()==2,"zero/non-attack damage reduced Flight");
    for(auto id : {CardId::PUMMEL,CardId::TWIN_STRIKE,CardId::WHIRLWIND}) {
        Fixture f; f.b.player.energy=4; f.play(id);
        const int damage=id==CardId::PUMMEL?4:id==CardId::TWIN_STRIKE?4:8;
        check(f.b.monsters.arr[0].curHp==1000-damage,"precomputed multi-hit Flight damage");
        check(f.b.monsters.arr[0].getStatus<MS::FLIGHT>()==(id==CardId::TWIN_STRIKE?1:0),
              "precomputed multi-hit Flight count");
    }
}
}
int main() {
    try { randomMultiHit(); queueAndDamageControls();
        std::cout<<"BYRD_FLIGHT_TIMING "<<checks<<" checks passed\n"; return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
