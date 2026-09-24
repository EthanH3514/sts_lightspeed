// Standalone regression against upstream master; no other pending patches required.
#include <algorithm>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) {
    ++checks; if(!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game; BattleContext b;
    Fixture() : game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=20; b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        b.monsters.arr[0].setMove(MMID::CULTIST_DARK_STRIKE);
        // Keep a real draw pile to avoid the unrelated empty-deck guard.
        for(int i=0;i<10;++i) {
            CardInstance c(CardId::DEFEND_RED); c.setUniqueId(b.cards.nextUniqueCardId++);
            b.cards.drawPile.push_back(c);
        }
    }
    void play(CardId id,bool up=false) {
        CardInstance c(id,up); c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.hand[b.cards.cardsInHand++]=c;
        b.addToBotCard(CardQueueItem(c,0,b.player.energy));
        b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();
    }
    void end() {search::Action(search::ActionType::END_TURN).execute(b);}
};
void run() {
    Fixture f; auto &p=f.b.player; auto &m=f.b.monsters.arr[0];
    for(int start:{-998,0,998}) for(int amount:{-4,4}) {
        p.strength=start; p.buff<PS::STRENGTH>(amount);
        check(p.strength==std::clamp(start+amount,-999,999),"player buff bounds and ordinary control");
        p.strength=start; p.debuff<PS::STRENGTH>(amount,false);
        check(p.strength==std::clamp(start+amount,-999,999),"player debuff bounds");
        m.strength=start; m.buff<MS::STRENGTH>(amount);
        check(m.strength==std::clamp(start+amount,-999,999),"monster buff bounds");
        m.strength=start; m.addDebuff<MS::STRENGTH>(amount,false);
        check(m.strength==std::clamp(start+amount,-999,999),"monster debuff bounds");
    }
    p.strength=-998; p.artifact=1; p.debuff<PS::STRENGTH>(-4,false);
    check(p.strength==-998&&p.artifact==0,"Artifact still blocks player Strength debuff");
    for(bool up:{false,true}) {
        Fixture g; g.b.player.strength=600; g.play(CardId::LIMIT_BREAK,up);
        check(g.b.player.strength==999,"positive Limit Break clamps at999");
    }
    m.removeStatus<MS::SHACKLED>(); m.buff<MS::SHACKLED>(126); m.buff<MS::SHACKLED>(15);
    check(m.getStatus<MS::SHACKLED>()==141,"restoration storage exceeds signed8bit range");
    m.buff<MS::SHACKLED>(900);
    check(m.getStatus<MS::SHACKLED>()==999,"restoration upper bound");
    m.strength=-999; m.applyEndOfTurnTriggers(f.b);
    check(m.strength==0&&m.getStatus<MS::SHACKLED>()==0,"restoration uses full capped count then removes power");
    m.buff<MS::SHACKLED>(-998); m.buff<MS::SHACKLED>(-4);
    check(m.getStatus<MS::SHACKLED>()==-999,"restoration lower bound");
    m.removeStatus<MS::SHACKLED>();
    // Exercise shared helpers directly: this does not depend on Dark Shackles' card fix.
    m.strength=-995; m.addDebuff<MS::STRENGTH>(-9,false); m.buff<MS::SHACKLED>(9);
    check(m.strength==-999,"temporary loss reaches floor");
    m.applyEndOfTurnTriggers(f.b);
    check(m.strength==-990,"full scheduled amount restored even when loss was clamped");
    m.strength=998; m.buff<MS::SHACKLED>(9); m.applyEndOfTurnTriggers(f.b);
    check(m.strength==999,"restoration itself respects Strength cap");
}
}
int main() {
    run();
    std::cout<<"STRENGTH_BOUNDS_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
