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
    // Deliberately excludes Blood for Blood: its separate pending fix is not required.
    for(auto id:{CardId::APOTHEOSIS,CardId::DARK_EMBRACE,CardId::BODY_SLAM,CardId::ENTRENCH})
    for(int cost:{0,1,2,3}) for(int turn:{0,1,3}) {
        CardInstance c(id); c.cost=cost; c.costForTurn=turn; c.setUniqueId(222);
        const int target=getEnergyCost(id,true);
        const int expected=turn>0?std::max(0,target+turn-cost):0;
        c.upgrade();
        check(c.cost==target,"upgraded combat cost matches card semantics");
        check(c.costForTurn==expected,"preserve turn/combat difference and temporary zero");
        check(c.uniqueId==222&&c.isUpgraded(),"upgrade keeps identity");
        c.upgrade();
        check(c.cost==target&&c.costForTurn==expected,"second ordinary upgrade is no-op");
    }
    for(auto id:{CardId::STRIKE_RED,CardId::DEFEND_RED}) {
        CardInstance c(id); c.cost=2; c.costForTurn=0; c.upgrade();
        check(c.cost==2&&c.costForTurn==0,"non-cost upgrade preserves modified costs");
    }
    for(auto id:{CardId::ARMAMENTS,CardId::APOTHEOSIS}) for(bool up:{false,true}) {
        Fixture f; CardInstance c(CardId::DARK_EMBRACE);
        c.setUniqueId(222); c.costForTurn=0;
        f.b.cards.hand[f.b.cards.cardsInHand++]=c;
        f.play(id,up);
        check(f.b.cards.hand[0].cost==1&&f.b.cards.hand[0].costForTurn==0,"upgrade card retains free Dark Embrace");
        // Actual play verifies legality and payment after the upgrade.
        f.b.player.energy=0; auto upgraded=f.b.cards.hand[0];
        check(upgraded.canUse(f.b,0,false),"upgraded free card legal at zero energy");
        f.b.addToBotCard(CardQueueItem(upgraded,0,0));
        f.b.inputState=InputState::EXECUTING_ACTIONS; f.b.executeActions();
        check(f.b.player.energy==0&&f.b.player.getStatus<PS::DARK_EMBRACE>()==1,"free upgraded power resolves without energy payment");
    }
    Fixture all;
    for(auto *pile:{&all.b.cards.drawPile,&all.b.cards.discardPile,&all.b.cards.exhaustPile}) {
        CardInstance c(CardId::DARK_EMBRACE); c.setUniqueId(all.b.cards.nextUniqueCardId++);
        c.costForTurn=0; pile->push_back(c);
    }
    all.play(CardId::APOTHEOSIS);
    check(all.b.cards.drawPile.back().costForTurn==0&&all.b.cards.drawPile.back().cost==1,"draw-pile upgrade costs");
    check(all.b.cards.discardPile.back().costForTurn==0&&all.b.cards.discardPile.back().cost==1,"discard-pile upgrade costs");
    check(all.b.cards.exhaustPile[0].costForTurn==0&&all.b.cards.exhaustPile[0].cost==1,"exhaust-pile upgrade costs");
}
}
int main() {
    run();
    std::cout<<"UPGRADE_COST_PRESERVATION_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
