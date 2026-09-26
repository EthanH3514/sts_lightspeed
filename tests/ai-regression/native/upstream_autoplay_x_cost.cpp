// Standalone upstream regression: no first-party simulator extensions required.
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
struct Fixture {
    GameContext game;
    BattleContext b;
    explicit Fixture(bool nunchaku = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1; game.relics = {};
        if (nunchaku) game.relics.add({RelicId::NUNCHAKU});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.curHp = 70; b.player.maxHp = 80;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
        for (int i = 0; i < 10; ++i)
            b.cards.createTempCardInDrawPile(0, CardInstance(CardId::DEFEND_RED));
    }
    void play(CardId id, bool up = false, bool autoplay = false, int queuedX = -1) {
        b.cards.createTempCardInHand(CardInstance(id, up));
        auto c = b.cards.hand[b.cards.cardsInHand - 1];
        auto item = CardQueueItem(c, 0, queuedX < 0 ? b.player.energy : queuedX);
        item.autoplay = autoplay; item.ignoreEnergyTotal = autoplay;
        b.addToBotCard(item); b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions();
    }
};
void directPlays() {
    for (bool up : {false, true}) {
        for (bool autoplay : {false, true}) {
            for (int energy : {0, 1, 3}) {
                Fixture f; f.b.player.energy = energy;
                f.play(CardId::WHIRLWIND, up, autoplay);
                check(f.b.player.energy == (autoplay ? energy : 0),
                      "Whirlwind spends energy only on a manual play");
                check(1000 - f.b.monsters.arr[0].curHp == energy * (up ? 8 : 5),
                      "free autoplay still executes the full X effect");
            }
            Fixture fixed; fixed.b.player.energy = 3;
            fixed.play(CardId::STRIKE_RED, up, autoplay);
            check(fixed.b.player.energy == (autoplay ? 3 : 2),
                  "fixed-cost manual/autoplay energy handling remains unchanged");
        }
        Fixture f; f.b.player.energy = 1;
        f.play(CardId::WHIRLWIND, up, true, 3);
        check(f.b.player.energy == 1, "autoplay preserves newly available energy");
        check(1000 - f.b.monsters.arr[0].curHp == 3 * (up ? 8 : 5),
              "autoplay uses queued X rather than current energy");
    }
}
void doubleTapNunchaku() {
    for (bool up : {false, true}) {
        // Constructed public states: counter 9 grants energy after the original
        // Whirlwind, whereas counter 8 grants it after the replay.
        for (int counter : {8, 9}) {
            Fixture f(true); f.b.player.nunchakuCounter = counter; f.b.player.energy = 4;
            f.play(CardId::DOUBLE_TAP);
            check(f.b.player.energy == 3, "Double Tap costs one energy");
            f.play(CardId::WHIRLWIND, up);
            check(f.b.player.energy == 1, "free X replay preserves Nunchaku energy");
            check(1000 - f.b.monsters.arr[0].curHp == 6 * (up ? 8 : 5),
                  "both Whirlwinds execute the original X=3");
            check(f.b.player.nunchakuCounter == counter - 8,
                  "original and replay each trigger Nunchaku");
            int copies = 0;
            for (const auto &c : f.b.cards.discardPile) copies += c.id == CardId::WHIRLWIND;
            check(copies == 1, "purged replay does not create an extra pile card");
        }
        Fixture f; f.b.player.energy = 4;
        f.play(CardId::DOUBLE_TAP); f.play(CardId::WHIRLWIND, up);
        check(f.b.player.energy == 0, "manual X still spends its energy with Double Tap");
        check(1000 - f.b.monsters.arr[0].curHp == 6 * (up ? 8 : 5),
              "ordinary Double Tap preserves the original X effect");
    }
}
}
int main() {
    directPlays(); doubleTapNunchaku();
    std::cout << "AUTOPLAY_X_COST_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
