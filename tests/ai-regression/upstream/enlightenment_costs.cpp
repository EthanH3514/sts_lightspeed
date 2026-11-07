// Standalone regression against upstream master; no other pending patches required.
#include <iostream>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1; game.relics = {};
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 20;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
        for (int i = 0; i < 10; ++i) {
            CardInstance c(CardId::DEFEND_RED);
            c.setUniqueId(b.cards.nextUniqueCardId++); b.cards.drawPile.push_back(c);
        }
    }
    void add(CardInstance c) {
        c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.hand[b.cards.cardsInHand++] = c;
    }
    void use(int index) {
        const auto c = b.cards.hand[index];
        b.addToBotCard(CardQueueItem(c, 0, b.player.energy));
        b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions();
    }
    void playEnlightenment(bool upgraded) {
        add(CardInstance(CardId::ENLIGHTENMENT, upgraded)); use(b.cards.cardsInHand - 1);
    }
};
void run() {
    // Synthetic boundary matrix: verifies independent caps, not natural reachability
    // of every cost pair. The actual-play zero-cost example follows separately.
    for (bool up : {false, true}) for (int base : {-2, -1, 0, 1, 2, 3, 4})
    for (int turn : {-2, -1, 0, 1, 2, 3, 4}) {
        Fixture f; CardInstance c(CardId::BLUDGEON);
        c.cost = base; c.costForTurn = turn; f.add(c);
        const int uid = f.b.cards.hand[0].uniqueId;
        f.playEnlightenment(up);
        check(f.b.cards.hand[0].cost == (up && base > 1 ? 1 : base), "combat cost independently capped");
        check(f.b.cards.hand[0].costForTurn == (turn > 1 ? 1 : turn), "turn cost independently capped");
        check(f.b.cards.hand[0].uniqueId == uid, "card identity preserved");
        check(f.b.player.energy == 20, "Enlightenment costs zero energy");
        check(f.b.cards.discardPile.size() == 1 && f.b.cards.exhaustPile.empty(), "Enlightenment discards without intrinsic exhaust");
    }
    for (bool up : {false, true}) {
        Fixture duration; duration.add(CardInstance(CardId::BLUDGEON));
        duration.playEnlightenment(up); duration.b.cards.resetAttributesAtEndOfTurn();
        check(duration.b.cards.hand[0].costForTurn == (up ? 1 : 3), "normal versus upgraded duration");

        Fixture zero; CardInstance power(CardId::DARK_EMBRACE);
        power.setCostForTurn(0); zero.add(power); zero.b.player.energy = 0;
        check(zero.b.cards.hand[0].canUseOnAnyTarget(zero.b), "temporary-zero power legal before Enlightenment");
        zero.playEnlightenment(up);
        check(zero.b.cards.hand[0].cost == (up ? 1 : 2) && zero.b.cards.hand[0].costForTurn == 0, "temporary-zero power retains zero turn cost");
        const bool legal = zero.b.cards.hand[0].canUseOnAnyTarget(zero.b);
        check(legal, "temporary-zero power legal at zero energy after Enlightenment");
        if (legal) zero.use(0); // Do not force an illegal card play on unpatched master.
        check(zero.b.player.getStatus<PS::DARK_EMBRACE>() == 1, "zero-energy power play actually applies Dark Embrace");
        check(zero.b.player.energy == 0, "zero-energy power play does not spend energy");

        Fixture scope;
        CardInstance outside(CardId::BLUDGEON); outside.setUniqueId(222);
        scope.b.cards.drawPile.push_back(outside);
        scope.b.cards.discardPile.push_back(outside);
        scope.b.cards.exhaustPile.push_back(outside);
        scope.playEnlightenment(up);
        check(scope.b.inputState == InputState::PLAYER_NORMAL, "empty remaining hand resumes normally");
        check(scope.b.cards.drawPile.back().cost == 3 && scope.b.cards.drawPile.back().costForTurn == 3, "draw pile unchanged");
        check(scope.b.cards.discardPile.front().cost == 3 && scope.b.cards.discardPile.front().costForTurn == 3, "discard pile unchanged");
        check(scope.b.cards.exhaustPile.front().cost == 3 && scope.b.cards.exhaustPile.front().costForTurn == 3, "exhaust pile unchanged");
    }
}
}
int main() {
    run();
    std::cout << "ENLIGHTENMENT_COSTS_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
