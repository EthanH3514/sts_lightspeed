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
    for(bool up:{false,true}) for(int artifact:{0,1,2}) {
        Fixture f; auto &m=f.b.monsters.arr[0]; m.strength=3;
        if(artifact) m.buff<MS::ARTIFACT>(artifact);
        const int loss=artifact?0:(up?15:9);
        f.play(CardId::DARK_SHACKLES,up);
        check(m.strength==3-loss,"reduces rather than increases Strength");
        check(m.getStatus<MS::ARTIFACT>()==std::max(0,artifact-1),"one Artifact consumed");
        check(m.getStatus<MS::SHACKLED>()==loss,"restoration only when not blocked");
        check(f.b.player.energy==20,"zero energy cost");
        check(f.b.cards.cardsInHand==0&&f.b.cards.exhaustPile.size()==1&&f.b.cards.discardPile.empty(),"exhaust lifecycle");
        f.end();
        check(f.b.player.curHp==70-std::max(0,9-loss),"attack happens before restoration");
        check(m.strength==3,"temporary reduction restores without free Strength");
        check(m.getStatus<MS::SHACKLED>()==0,"restoration removed");
    }
    Fixture f; auto &m=f.b.monsters.arr[0]; m.strength=3;
    f.play(CardId::DARK_SHACKLES); f.play(CardId::DARK_SHACKLES,true);
    // Base Disarm is already correct on master; no upgraded-Disarm dependency.
    f.play(CardId::DISARM);
    check(m.strength==-23&&m.getStatus<MS::SHACKLED>()==24,"temporary reductions stack alongside permanent loss");
    f.end();
    check(m.strength==1&&f.b.player.curHp==70,"only temporary reductions restored");
    f.end();
    check(f.b.player.curHp==63,"following attack retains permanent loss");
}
}
int main() {
    run();
    std::cout<<"DARK_SHACKLES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
