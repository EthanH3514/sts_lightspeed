// Standalone regression: upstream master plus only the Violence cleanup fix.
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 20) std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    BattleContext b;
    explicit Fixture(int seed) {
        GameContext game(CharacterClass::IRONCLAD, seed, 0);
        game.relics = {}; game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 0;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
    }
};
using Snapshot = std::map<int, std::array<int, 5>>;
Snapshot snapshot(const BattleContext &b) {
    Snapshot result;
    bool unique = true;
    auto add = [&](const CardInstance &c) {
        unique &= !result.count(c.uniqueId);
        result[c.uniqueId] = {int(c.id), c.getUpgradeCount(), c.cost, c.costForTurn, c.specialData};
    };
    for (int i = 0; i < b.cards.cardsInHand; ++i) add(b.cards.hand[i]);
    for (const auto &c : b.cards.drawPile) add(c);
    for (const auto &c : b.cards.discardPile) add(c);
    for (const auto &c : b.cards.exhaustPile) add(c);
    check(unique, "every combat identity occurs in exactly one pile");
    return result;
}
void matrix() {
    for (int seed : {1, 42, 123456789}) for (bool up : {false, true})
    for (int supply : {0, 1, 2, 3, 4, 7}) for (int occupied : {0, 8, 9})
    for (bool noDraw : {false, true}) {
        Fixture f(seed);
        std::vector<int> skillIds;
        for (int i = 0; i < supply; ++i) {
            CardInstance c(i % 2 ? CardId::RAMPAGE : CardId::STRIKE_RED, i % 2);
            if (c.id == CardId::RAMPAGE) c.specialData = 13;
            c.costForTurn = 0;
            f.b.cards.createTempCardInDrawPile(0, c);
        }
        for (int i = 0; i < 3; ++i) {
            f.b.cards.createTempCardInDrawPile(0, CardInstance(CardId::DEFEND_RED));
        }
        for (const auto &c : f.b.cards.drawPile) if (c.getType() == CardType::SKILL) skillIds.push_back(c.uniqueId);
        for (int i = 0; i < occupied; ++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        f.b.cards.createTempCardInHand(CardInstance(CardId::VIOLENCE, up));
        if (noDraw) f.b.player.buff<PS::NO_DRAW>();
        auto before = snapshot(f.b);
        const auto source = f.b.cards.hand[occupied];
        check(source.canUse(f.b, 0, false), "Violence is legal at zero energy, even with no attacks");
        f.b.addToBotCard(CardQueueItem(source, 0, f.b.player.energy));
        f.b.inputState = InputState::EXECUTING_ACTIONS; f.b.executeActions();
        const int moved = std::min(supply, up ? 4 : 3);
        check(f.b.cards.cardsInHand == std::min(10, occupied + moved), "retrieval count and hand capacity");
        check(f.b.cards.drawPile.size() == static_cast<unsigned>(supply + 3 - moved), "selected attacks leave draw even when supply is short");
        check(f.b.cards.discardPile.size() == static_cast<unsigned>(std::max(0, occupied + moved - 10)), "overflow goes to discard");
        check(f.b.cards.exhaustPile.size() == 1 && f.b.cards.exhaustPile[0].uniqueId == source.uniqueId, "source exhausts exactly once");
        check(f.b.player.energy == 0, "zero energy cost");
#ifdef SPIRE_DISCARD_COST_LIFECYCLE
        // Local full-stack wrapper only; standalone upstream-PR test keeps its
        // original expectation on master plus the independent cleanup patch.
        for (const auto &c : f.b.cards.discardPile) before.at(c.uniqueId)[3] = before.at(c.uniqueId)[2];
#endif
        check(snapshot(f.b) == before, "movement preserves identities/base costs/data with scoped overflow lifecycle");
        std::vector<int> remainingSkills;
        for (const auto &c : f.b.cards.drawPile) if (c.getType() == CardType::SKILL) remainingSkills.push_back(c.uniqueId);
        check(remainingSkills == skillIds, "unselected skills remain in their original relative order");
        bool attacksOnly = true;
        for (int i = occupied; i < f.b.cards.cardsInHand; ++i) attacksOnly &= f.b.cards.hand[i].getType() == CardType::ATTACK;
        for (const auto &c : f.b.cards.discardPile) attacksOnly &= c.getType() == CardType::ATTACK;
        check(attacksOnly, "only attacks are retrieved or overflowed");
    }
}
}
int main() {
    matrix();
    std::cout << "VIOLENCE_SHORT_SUPPLY_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
