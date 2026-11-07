// Standalone regression for Trip's base and upgraded zero energy cost.
#include <iostream>
#include <sstream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/BattleSimulator.h"
#include "sim/search/Action.h"

using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 20) std::cerr << "FAIL: " << message << '\n'; }
}
void variants() {
    for (bool up : {false, true}) {
        check(getEnergyCost(CardId::TRIP, up) == 0, "Trip static cost is zero");
        CardInstance card(CardId::TRIP, up);
        check(card.cost == 0 && card.costForTurn == 0, "Trip instance starts at zero cost");
        check(card.requiresTarget() == !up, "only base Trip requires a target");
        check(!card.doesExhaust(), "Trip does not intrinsically exhaust");
        check(getEnergyCost(CardId::BLIND, up) == 0, "Blind stays zero cost");
        check(getEnergyCost(CardId::TRUE_GRIT, up) == 1, "adjacent True Grit stays one cost");
        check(getEnergyCost(CardId::SWORD_BOOMERANG, up) == 1, "adjacent Sword Boomerang stays one cost");
    }
    CardInstance upgraded(CardId::TRIP);
    upgraded.upgrade();
    check(upgraded.cost == 0 && upgraded.costForTurn == 0 && !upgraded.requiresTarget(),
          "in-combat upgrade preserves zero cost and changes target scope");
}
void combat() {
    for (bool up : {false, true}) for (int energy : {0, 1, 3}) {
        GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
        game.relics = {};
        game.enterBattle(MonsterEncounter::TWO_LOUSE);
        BattleSimulator simulator;
        simulator.initBattle(game);
        auto &b = *simulator.bc;
        b.executeActions();
        b.cards = CardManager{};
        b.cards.nextUniqueCardId = 100;
        b.cards.createTempCardInHand(CardInstance(CardId::TRIP, up));
        b.player.energy = energy;
        for (int i = 0; i < 2; ++i) b.monsters.arr[i].curHp = b.monsters.arr[i].maxHp = 100;

        const search::Action action(search::ActionType::CARD, 0, 1);
        const bool playable = action.isValidAction(b);
        check(playable, "Trip is playable even at zero energy");
        std::ostringstream actions;
        simulator.printNormalActions(actions);
        check(actions.str().find("0:") != std::string::npos, "Trip action is exported at every tested energy");
        // Never execute an invalid action on the unfixed simulator.
        if (playable) action.execute(b);
        check(b.player.energy == energy, "Trip does not spend energy");
        check(b.cards.cardsInHand == 0, "Trip leaves the hand after use");
        check(b.cards.discardPile.size() == 1 && b.cards.exhaustPile.empty(),
              "Trip discards once without exhausting");
        check(b.monsters.arr[0].getStatus<MS::VULNERABLE>() == (up ? 2 : 0),
              "only upgraded Trip affects the unselected enemy");
        check(b.monsters.arr[1].getStatus<MS::VULNERABLE>() == 2,
              "Trip applies two Vulnerable to the selected enemy");
        check(b.monsters.arr[0].curHp == 100 && b.monsters.arr[1].curHp == 100,
              "Trip does not deal damage");
        std::cout << "Trip" << (up ? "+" : "") << ": energy_before=" << energy
                  << ", playable=" << playable << ", energy_after=" << b.player.energy << '\n';
    }
}
}
int main() {
    variants();
    combat();
    std::cout << "TRIP_COST_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
