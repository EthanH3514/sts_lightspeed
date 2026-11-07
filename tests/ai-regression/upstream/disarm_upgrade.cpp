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
    for(bool up:{false,true}) for(int strength:{-2,3}) for(int artifact:{0,1,2}) {
        Fixture f; auto &m=f.b.monsters.arr[0]; m.strength=strength;
        if(artifact) m.buff<MS::ARTIFACT>(artifact);
        f.play(CardId::DISARM,up);
        const int expected=strength-(artifact?0:(up?3:2));
        check(m.strength==expected,"correct base/upgraded Strength reduction");
        check(m.getStatus<MS::ARTIFACT>()==std::max(0,artifact-1),"one Artifact blocks debuff");
        check(m.getStatus<MS::SHACKLED>()==0,"permanent reduction has no restoration");
        check(f.b.player.energy==19,"one energy spent");
        check(f.b.cards.cardsInHand==0&&f.b.cards.exhaustPile.size()==1&&f.b.cards.discardPile.empty(),"exhaust lifecycle");
        f.end();
        check(f.b.player.curHp==70-std::max(0,6+expected),"enemy attack uses reduced Strength");
        check(m.strength==expected,"reduction persists after enemy turn");
    }
}
}
int main() {
    run();
    std::cout<<"DISARM_UPGRADE_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
