// From repository root (with the existing json dependency):
// g++ -std=c++17 -O1 -Iinclude -Ijson/single_include tests/draw_trigger_order.cpp \
//     $(find src -name '*.cpp') -o draw_trigger_order
// ./draw_trigger_order
#include <iostream>
#include <stdexcept>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void checkOrder(bool fireFirst, bool upgraded, bool stack, bool reapply, bool clone) {
    GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
    game.floorNum = 1;
    game.enterBattle(MonsterEncounter::CULTIST);
    BattleContext battle;
    battle.init(game);
    battle.executeActions();
    battle.cards = CardManager{};
    battle.cards.nextUniqueCardId = 100;
    battle.player.energy = 10;
    const auto addHand = [&](CardId id) {
        CardInstance card(id, upgraded);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
    };
    addHand(fireFirst ? CardId::FIRE_BREATHING : CardId::EVOLVE);
    addHand(fireFirst ? CardId::EVOLVE : CardId::FIRE_BREATHING);
    // Back is the top. Keep enough real cards for every extra draw.
    for (int i = 0; i < 8; ++i) {
        CardInstance card(CardId::STRIKE_RED);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.drawPile.push_back(card);
    }
    CardInstance wound(CardId::WOUND);
    wound.setUniqueId(battle.cards.nextUniqueCardId++);
    battle.cards.drawPile.push_back(wound);
    for (int i = 0; i < 2; ++i) {
        auto card = battle.cards.hand[0];
        require(card.canUse(battle, 0, false), "power is not playable");
        battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }
    const int evolve = upgraded ? 2 : 1;
    if (stack) {
        // Stacking in reverse order must not reorder existing powers.
        if (fireFirst) {
            battle.player.buff<PS::EVOLVE>(evolve);
            battle.player.buff<PS::FIRE_BREATHING>(6);
        } else {
            battle.player.buff<PS::FIRE_BREATHING>(6);
            battle.player.buff<PS::EVOLVE>(evolve);
        }
    }
    if (reapply) {
        if (fireFirst) {
            battle.player.removeStatus<PS::FIRE_BREATHING>();
            battle.player.buff<PS::FIRE_BREATHING>(6);
        } else {
            battle.player.removeStatus<PS::EVOLVE>();
            battle.player.buff<PS::EVOLVE>(evolve * (stack ? 2 : 1));
        }
        fireFirst = !fireFirst;
    }
    BattleContext copy = battle;
    auto &tested = clone ? copy : battle;
    tested.monsters.arr[0].curHp = 6;
    const int drawnBefore = tested.cardsDrawn;
    tested.addToBot(Actions::DrawCards(1));
    tested.inputState = InputState::EXECUTING_ACTIONS;
    tested.executeActions();
    require(tested.outcome == Outcome::PLAYER_VICTORY, "expected lethal trigger");
    const int expected = 1 + (fireFirst ? 0 : evolve * (stack ? 2 : 1));
    require(tested.cardsDrawn - drawnBefore == expected,
            fireFirst ? "Fire Breathing first: drew extra cards before lethal damage"
                      : "Evolve first: missing extra draw before lethal damage");
    if (clone) require(battle.cardsDrawn == drawnBefore, "copy mutated source");
}
}

int main() {
    try {
        for (bool fireFirst : {false, true})
            for (bool upgraded : {false, true})
                for (bool stack : {false, true})
                    for (bool reapply : {false, true})
                        for (bool clone : {false, true})
                            checkOrder(fireFirst, upgraded, stack, reapply, clone);
        std::cout << "DRAW_TRIGGER_ORDER_OK (32 cases)\n";
    } catch (const std::exception &error) {
        std::cerr << "DRAW_TRIGGER_ORDER_FAILED: " << error.what() << '\n';
        return 1;
    }
}
