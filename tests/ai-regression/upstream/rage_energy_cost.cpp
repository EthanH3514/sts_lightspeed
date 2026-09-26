// Standalone regression; no other pending patches or bindings required.
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void run() {
    for (bool upgraded : {false, true}) {
        check(getEnergyCost(CardId::RAGE, upgraded) == 0, "static Rage cost is zero");
        CardInstance rage(CardId::RAGE, upgraded);
        check(rage.cost == 0 && rage.costForTurn == 0, "instance starts at zero cost");
        for (int energy : {0, 3}) {
            GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
            game.floorNum = 1;
            game.relics = {};
            game.enterBattle(MonsterEncounter::CULTIST);
            BattleContext battle;
            battle.init(game);
            battle.executeActions();
            battle.cards = CardManager{};
            battle.cards.nextUniqueCardId = 100;
            // Keep a real draw pile to avoid the unrelated empty-deck guard.
            for (int i = 0; i < 10; ++i) {
                CardInstance card(CardId::DEFEND_RED);
                card.setUniqueId(battle.cards.nextUniqueCardId++);
                battle.cards.drawPile.push_back(card);
            }
            rage.setUniqueId(battle.cards.nextUniqueCardId++);
            battle.cards.hand[battle.cards.cardsInHand++] = rage;
            battle.player.energy = energy;
            check(rage.canUse(battle, 0, false), "Rage remains legal at zero energy");
            // At zero energy the old implementation rejects this queued card;
            // at three energy it plays but incorrectly spends one energy.
            battle.addToBotCard(CardQueueItem(rage, 0, battle.player.energy));
            battle.inputState = InputState::EXECUTING_ACTIONS;
            battle.executeActions();
            check(battle.player.energy == energy, "playing Rage spends no energy");
            check(battle.player.getStatus<PS::RAGE>() == (upgraded ? 5 : 3), "Rage amount unchanged");
            check(battle.cards.cardsInHand == 0 && battle.cards.discardPile.size() == 1
                  && battle.cards.exhaustPile.empty(), "normal discard lifecycle unchanged");
        }
    }
    check(getEnergyCost(CardId::STRIKE_RED, false) == 1, "one-cost control unchanged");
    check(getEnergyCost(CardId::BLOOD_FOR_BLOOD, false) == 4, "base Blood for Blood control");
    check(getEnergyCost(CardId::BLOOD_FOR_BLOOD, true) == 3, "upgraded Blood for Blood control");
}
}
int main() {
    run();
    std::cout << "RAGE_ENERGY_COST_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
