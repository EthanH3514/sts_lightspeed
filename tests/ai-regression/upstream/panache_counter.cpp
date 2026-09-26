// Standalone regression: upstream master plus only the Panache counter fix.
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
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
        b.player.energy = 100; b.player.curHp = b.player.maxHp = 200;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 10000;
        for (int i = 0; i < 12; ++i)
            b.cards.createTempCardInDrawPile(i, CardInstance(CardId::DEFEND_RED));
    }
    void play(CardId id, bool upgraded = false) {
        b.cards.createTempCardInHand(CardInstance(id, upgraded));
        auto card = b.cards.hand[b.cards.cardsInHand - 1];
        check(card.canUse(b, 0, false), "fixture card must be playable");
        b.addToBotCard(CardQueueItem(card, 0, b.player.energy));
        b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions();
    }
};
void sequence(int seed, bool upgraded) {
    Fixture f(seed); f.play(CardId::PANACHE, upgraded);
    const int damage = upgraded ? 14 : 10;
    check(f.b.player.panacheCounter == 5, "initial power starts at five, excluding its own play");
    check(f.b.monsters.arr[0].curHp == 10000, "registration does not deal damage");
    check(f.b.player.getStatus<PS::PANACHE>() == damage, "registered damage");
    check(f.b.player.energy == 100, "Panache costs zero");
    for (int played = 1; played <= 12; ++played) {
        f.play(CardId::DEFEND_RED);
        check(f.b.player.panacheCounter == 5 - played % 5, "counter cycles every five plays");
        check(f.b.monsters.arr[0].curHp == 10000 - (played / 5) * damage,
              "only fifth and tenth plays trigger damage");
    }
    search::Action(search::ActionType::END_TURN).execute(f.b);
    check(f.b.player.panacheCounter == 5, "turn start resets unfinished counter");
    check(f.b.player.getStatus<PS::PANACHE>() == damage, "turn start preserves damage");
}
void everyPath(int seed, bool upgraded) {
    for (auto id : {CardId::STRIKE_RED, CardId::DEFEND_RED, CardId::INFLAME, CardId::SLIMED}) {
        Fixture f(seed); f.play(CardId::PANACHE, upgraded);
        f.b.player.panacheCounter = 1; // Isolate trigger reset from initialization.
        f.play(id);
        check(f.b.player.panacheCounter == 5, "attack/skill/power/status path resets counter");
        const int damage = (upgraded ? 14 : 10) + (id == CardId::STRIKE_RED ? 6 : 0);
        check(f.b.monsters.arr[0].curHp == 10000 - damage, "threshold play deals one burst");
        f.play(CardId::DEFEND_RED);
        check(f.b.player.panacheCounter == 4, "first play after trigger counts down from five");
        check(f.b.monsters.arr[0].curHp == 10000 - damage, "sixth play has no extra burst");
    }
}
void stackingAndRepeats(int seed) {
    Fixture f(seed); f.play(CardId::PANACHE); f.play(CardId::DEFEND_RED);
    f.play(CardId::PANACHE, true);
    check(f.b.player.panacheCounter == 3, "stacking counts a play but does not reset progress");
    check(f.b.player.getStatus<PS::PANACHE>() == 24, "damage stacks independently of counter");
    for (int i = 0; i < 3; ++i) f.play(CardId::DEFEND_RED);
    check(f.b.player.panacheCounter == 5 && f.b.monsters.arr[0].curHp == 9976,
          "stacked damage triggers at the existing threshold");
    Fixture threshold(seed); threshold.play(CardId::PANACHE);
    threshold.b.player.panacheCounter = 1; threshold.play(CardId::PANACHE, true);
    check(threshold.b.player.panacheCounter == 5, "stacking on threshold resets once");
    check(threshold.b.player.getStatus<PS::PANACHE>() == 24, "threshold stacking retains new damage");
    check(threshold.b.monsters.arr[0].curHp == 9990, "queued burst uses damage from existing power");
    Fixture repeat(seed); repeat.play(CardId::PANACHE); repeat.b.player.panacheCounter = 3;
    repeat.play(CardId::DOUBLE_TAP); repeat.play(CardId::TWIN_STRIKE);
    check(repeat.b.player.panacheCounter == 5, "replay counts once, multi-hit does not count twice");
    check(repeat.b.monsters.arr[0].curHp == 9970, "four attack hits and one Panache burst");
}
}
int main() {
    for (int seed : {1, 42, 123456789}) {
        for (bool upgraded : {false, true}) { sequence(seed, upgraded); everyPath(seed, upgraded); }
        stackingAndRepeats(seed);
    }
    std::cout << "PANACHE_COUNTER_OK (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
