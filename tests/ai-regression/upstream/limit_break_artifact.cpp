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
    for(bool up:{false,true}) for(int strength:{-3,0,4}) for(int artifact:{0,1,2}) {
        Fixture f; f.b.player.strength=strength; f.b.player.artifact=artifact;
        f.play(CardId::LIMIT_BREAK,up);
        const bool blocked=strength<0&&artifact>0;
        check(f.b.player.strength==(blocked?strength:strength*2),"signed Strength doubling respects Artifact");
        check(f.b.player.artifact==artifact-int(blocked),"only negative Strength consumes one Artifact");
        check(f.b.player.energy==19,"one energy spent");
        check(f.b.cards.cardsInHand==0&&f.b.cards.exhaustPile.size()==(up?0:1)
              &&f.b.cards.discardPile.size()==(up?1:0),"base exhausts and upgrade discards");
    }
    for(bool up:{false,true}) {
        Fixture f; f.play(CardId::FLEX,up); f.play(CardId::LIMIT_BREAK,up); f.end();
        check(f.b.player.strength==(up?4:2),"doubling does not double Flex scheduled loss");
    }
}
}
int main() {
    run();
    std::cout<<"LIMIT_BREAK_ARTIFACT_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
