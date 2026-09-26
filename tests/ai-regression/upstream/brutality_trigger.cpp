// Standalone regression; no bindings or other pending simulator patches required.
// g++ -std=c++17 -O2 -Iinclude -Ijson/single_include \
//     tests/brutality_trigger.cpp $(find src -name '*.cpp') -o brutality_trigger
#include <iostream>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"

using namespace sts;
namespace {
int checks = 0;
int failures = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}

struct Fixture {
    GameContext game;
    BattleContext battle;
    explicit Fixture(RelicId relic = RelicId::INVALID)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        if (relic != RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
        // Keep actual remaining cards to avoid the unrelated empty-deck loss guard.
        for (int i = 0; i < 10; ++i) {
            CardInstance card(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED);
            card.setUniqueId(battle.cards.nextUniqueCardId++);
            battle.cards.drawPile.push_back(card);
        }
        battle.player.energy = 10;
        battle.player.maxHp = 80;
        battle.player.curHp = 70;
        battle.monsters.arr[0].curHp = battle.monsters.arr[0].maxHp = 200;
    }
    void drain() {
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }
    void playBrutality(bool upgraded) {
        CardInstance card(CardId::BRUTALITY, upgraded);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
        battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
        drain();
    }
    void endTurn() { search::Action(search::ActionType::END_TURN).execute(battle); }
};

void run() {
    for (bool upgraded : {false, true}) {
        Fixture f;
        f.battle.player.buff<PS::RUPTURE>(2);
        f.playBrutality(upgraded);
        check(f.battle.player.curHp == 70 && f.battle.player.strength == 0 &&
              f.battle.cards.cardsInHand == 0, "no immediate draw/loss/Strength");
        f.endTurn();
        check(f.battle.player.curHp == 69, "Brutality loses one HP");
        check(f.battle.cards.cardsInHand == 6, "normal five-card draw plus Brutality");
        check(f.battle.player.strength == 2, "Brutality loss triggers Rupture");

        Fixture order;
        order.playBrutality(upgraded);
        order.battle.player.applyStartOfTurnPostDrawPowers(order.battle);
        // Execute only the first queued callback to distinguish the ordering.
        auto first = order.battle.actionQueue.popFront();
        first(order.battle);
        check(order.battle.cards.cardsInHand == 1 && order.battle.player.curHp == 70,
              "draw callback precedes HP loss");
        order.drain();
        check(order.battle.cards.cardsInHand == 1 && order.battle.player.curHp == 69,
              "both callbacks resolve exactly once");

        Fixture rod(RelicId::TUNGSTEN_ROD);
        rod.battle.player.buff<PS::RUPTURE>(2);
        rod.playBrutality(upgraded);
        rod.endTurn();
        check(rod.battle.player.curHp == 70 && rod.battle.player.strength == 0,
              "prevented HP loss does not trigger Rupture");
        check(rod.battle.cards.cardsInHand == 6, "prevented loss does not cancel draw");
    }

    Fixture stacked;
    stacked.battle.player.buff<PS::RUPTURE>(3);
    stacked.playBrutality(false);
    stacked.playBrutality(true);
    stacked.endTurn();
    check(stacked.battle.player.curHp == 68 && stacked.battle.cards.cardsInHand == 7,
          "stacked Brutality combines draw and HP loss");
    check(stacked.battle.player.strength == 3, "combined loss triggers Rupture once");

    Fixture noDraw;
    noDraw.battle.player.buff<PS::RUPTURE>(2);
    noDraw.playBrutality(false);
    noDraw.battle.player.debuff<PS::NO_DRAW>(1, false);
    noDraw.battle.player.applyStartOfTurnPostDrawPowers(noDraw.battle);
    noDraw.drain();
    check(noDraw.battle.cards.cardsInHand == 0 && noDraw.battle.player.curHp == 69,
          "No Draw does not cancel HP loss");
    check(noDraw.battle.player.strength == 2, "loss with No Draw still triggers Rupture");

    Fixture external;
    external.battle.player.buff<PS::RUPTURE>(2);
    external.battle.addToBot(Actions::PlayerLoseHp(1));
    external.drain();
    check(external.battle.player.curHp == 69 && external.battle.player.strength == 0,
          "non-self HP loss remains ineligible for Rupture");
}
}

int main() {
    run();
    std::cout << "BRUTALITY_TRIGGER_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
