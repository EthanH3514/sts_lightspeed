// Standalone regression; no other pending patches or bindings required.
#include <algorithm>
#include <iostream>
#include "combat/CardInstance.h"

using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void run() {
    // Original upgrade first adjusts costForTurn using the old offset, then
    // clamps the new base cost. Cover temporary discounts AND increases.
    for (int cost : {0, 1, 2, 3, 4, 5}) {
        for (int turnCost : {0, 1, 2, 3, 4, 5, 6}) {
            CardInstance card(CardId::BLOOD_FOR_BLOOD);
            card.cost = cost;
            card.costForTurn = turnCost;
            card.setUniqueId(42);
            card.upgrade();
            const int raw = cost < 4 ? cost - 1 : 3;
            const int expectedTurn = turnCost > 0 ? std::max(0, raw + turnCost - cost) : 0;
            check(card.cost == std::max(0, raw), "upgrade preserves combat discount");
            check(card.costForTurn == expectedTurn, "upgrade preserves temporary cost offset");
            check(card.isUpgraded() && card.uniqueId == 42, "upgraded flag and identity");
            const auto once = card;
            card.upgrade();
            check(card.cost == once.cost && card.costForTurn == once.costForTurn,
                  "second upgrade is a no-op");
        }
    }
    for (int losses : {0, 1, 2, 3, 4, 5}) for (bool freeTurn : {false, true}) {
        CardInstance card(CardId::BLOOD_FOR_BLOOD);
        for (int i = 0; i < losses; ++i) card.tookDamage();
        const int discounted = std::max(0, 4 - losses);
        check(card.cost == discounted, "actual tookDamage discounts before upgrade");
        if (freeTurn) card.setCostForTurn(0);
        card.upgrade();
        const int expected = std::max(0, discounted - 1);
        check(card.cost == expected && card.costForTurn == (freeTurn ? 0 : expected),
              "damage-discount and temporary free chain survives upgrade");
    }
    CardInstance strike(CardId::STRIKE_RED);
    strike.upgrade();
    check(strike.isUpgraded() && strike.cost == 1 && strike.costForTurn == 1, "ordinary upgrade control");
    CardInstance bodySlam(CardId::BODY_SLAM);
    bodySlam.upgrade();
    check(bodySlam.isUpgraded() && bodySlam.cost == 0 && bodySlam.costForTurn == 0, "ordinary cost upgrade control");
    CardInstance searingBlow(CardId::SEARING_BLOW);
    searingBlow.upgrade(); searingBlow.upgrade();
    check(searingBlow.getUpgradeCount() == 2, "repeatable upgrade control");
}
}
int main() {
    run();
    std::cout << "BLOOD_FOR_BLOOD_UPGRADE_COST_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
