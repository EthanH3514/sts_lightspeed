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
    for(auto id:{CardId::BURN,CardId::WOUND,CardId::DAZED,CardId::SLIMED,
                 CardId::VOID,CardId::REGRET,CardId::ASCENDERS_BANE}) {
        Fixture f; CardInstance c(id); c.setUniqueId(222);
        f.b.cards.hand[f.b.cards.cardsInHand++]=c;
        f.play(CardId::ARMAMENTS,true);
        check(!f.b.cards.hand[0].isUpgraded(),"unupgradeable Status/Curse remains unchanged");
        check(f.b.cards.hand[0].uniqueId==222,"identity preserved");
        check(f.b.player.block==5&&f.b.player.energy==19,"Armaments block and cost unchanged");
        check(f.b.cards.discardPile.size()==1&&f.b.cards.exhaustPile.empty(),"Armaments discards");
    }
    Fixture burn; CardInstance c(CardId::BURN); c.setUniqueId(222);
    burn.b.cards.hand[burn.b.cards.cardsInHand++]=c;
    burn.play(CardId::ARMAMENTS,true); burn.b.player.block=0;
    burn.b.monsters.arr[0].setMove(MMID::CULTIST_INCANTATION);
    burn.end();
    check(burn.b.player.curHp==68,"ordinary Burn still loses2HP, not4, after Armaments+");
    Fixture mixed;
    for(auto id:{CardId::STRIKE_RED,CardId::DEFEND_RED,CardId::SEARING_BLOW}) {
        CardInstance card(id); card.setUniqueId(mixed.b.cards.nextUniqueCardId++);
        if(id!=CardId::STRIKE_RED) card.upgrade();
        mixed.b.cards.hand[mixed.b.cards.cardsInHand++]=card;
    }
    mixed.play(CardId::ARMAMENTS,true);
    check(mixed.b.cards.hand[0].isUpgraded(),"eligible ordinary card upgrades");
    check(mixed.b.cards.hand[1].getUpgradeCount()==1,"already-upgraded ordinary card unchanged");
    check(mixed.b.cards.hand[2].getUpgradeCount()==2,"repeatable Searing Blow upgrades again");
    mixed.play(CardId::ARMAMENTS,true);
    check(mixed.b.cards.hand[2].getUpgradeCount()==3,"repeatable upgrade survives successive plays");
    for(const auto &card:mixed.b.cards.drawPile) check(!card.isUpgraded(),"hand-only scope");
    Fixture empty; empty.play(CardId::ARMAMENTS,true);
    check(empty.b.inputState==InputState::PLAYER_NORMAL,"empty hand resumes normally");
}
}
int main() {
    run();
    std::cout<<"UPGRADE_HAND_FILTER_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
