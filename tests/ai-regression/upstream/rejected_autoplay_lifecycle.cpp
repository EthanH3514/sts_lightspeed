// Standalone regression: requires only upstream master and the rejected-autoplay fix.
#include <algorithm>
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
    explicit Fixture(RelicId relic = RelicId::INVALID)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        if (relic != RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game);
        b.executeActions();
        b.cards = CardManager{};
        b.cards.nextUniqueCardId = 100;
        b.player.energy = 3;
        b.player.curHp = 70;
        b.player.maxHp = 80;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
    }
    int top(CardId id) {
        CardInstance c(id);
        c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.drawPile.push_back(c);
        return c.uniqueId;
    }
    void run() {
        b.inputState = InputState::EXECUTING_ACTIONS;
        b.executeActions();
    }
    void play(CardId id = CardId::HAVOC, bool upgraded = false) {
        b.cards.createTempCardInHand(CardInstance(id, upgraded));
        b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy));
        run();
    }
};
bool contains(const std::vector<CardInstance> &pile, int uid) {
    return std::any_of(pile.begin(), pile.end(), [=](const auto &c) { return c.uniqueId == uid; });
}
void rejectedHavoc() {
    for (bool upgraded : {false, true}) {
        for (CardId id : {CardId::WOUND, CardId::BURN, CardId::DAZED, CardId::REGRET, CardId::CLASH}) {
            Fixture f;
            const int uid = f.top(id);
            f.b.player.buff<PS::FEEL_NO_PAIN>(3);
            // Also prevents Clash from being playable.
            f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
            f.play(CardId::HAVOC, upgraded);
            check(f.b.cards.exhaustPile.size() == 1 && contains(f.b.cards.exhaustPile, uid),
                  "rejected top card exhausts once with its original identity");
            check(f.b.player.block == 3, "rejected autoplay triggers Feel No Pain");
            check(f.b.player.cardsPlayedThisTurn == 1 && f.b.player.attacksPlayedThisTurn == 0,
                  "rejected autoplay does not increment play counters");
            check(f.b.player.curHp == 70, "rejected Burn/Regret do not execute end-turn damage");
            check(f.b.monsters.arr[0].curHp == 1000, "rejected card has no attack effect");
            check(f.b.player.energy == (upgraded ? 3 : 2), "only Havoc spends energy");
            check(f.b.cards.drawPile.empty() && f.b.cards.discardPile.size() == 1,
                  "top removed and only Havoc discarded");
        }
    }
}
void restrictionsAndCallbacks() {
    Fixture entangled;
    int uid = entangled.top(CardId::STRIKE_RED);
    entangled.b.player.buff<PS::ENTANGLED>();
    entangled.b.player.buff<PS::FEEL_NO_PAIN>(3);
    entangled.b.monsters.arr[0].buff<MS::THORNS>(3);
    entangled.play();
    check(contains(entangled.b.cards.exhaustPile, uid) && entangled.b.player.block == 3,
          "Entangled rejection still exhausts");
    check(entangled.b.monsters.arr[0].curHp == 1000 && entangled.b.player.curHp == 70,
          "rejected attack triggers neither damage nor Thorns");
    check(entangled.b.player.cardsPlayedThisTurn == 1 && entangled.b.player.attacksPlayedThisTurn == 0,
          "Entangled rejection has no play counters");

    Fixture draw;
    const int drawn = draw.top(CardId::DEFEND_RED);
    uid = draw.top(CardId::WOUND);
    draw.b.player.buff<PS::DARK_EMBRACE>(1);
    draw.b.player.buff<PS::FEEL_NO_PAIN>(3);
    draw.play();
    check(contains(draw.b.cards.exhaustPile, uid) && draw.b.player.block == 3,
          "rejected Wound invokes exhaust callbacks");
    check(draw.b.cards.cardsInHand == 1 && draw.b.cards.hand[0].uniqueId == drawn,
          "Dark Embrace draws after rejected autoplay exhausts");
}
void controls() {
    Fixture discard;
    int uid = discard.top(CardId::WOUND);
    discard.b.playTopCardInDrawPile(0, false);
    discard.run();
    check(contains(discard.b.cards.discardPile, uid) && discard.b.cards.exhaustPile.empty(),
          "rejected non-exhausting autoplay discards");
    check(discard.b.player.cardsPlayedThisTurn == 0 && discard.b.player.energy == 3,
          "non-exhausting rejection has no play or energy effects");

    Fixture intrinsic;
    uid = intrinsic.top(CardId::PUMMEL);
    intrinsic.b.player.buff<PS::ENTANGLED>();
    intrinsic.b.player.buff<PS::FEEL_NO_PAIN>(3);
    intrinsic.b.playTopCardInDrawPile(0, false);
    intrinsic.run();
    check(contains(intrinsic.b.cards.exhaustPile, uid) && intrinsic.b.player.block == 3,
          "rejected autoplay preserves intrinsic exhaust");
    check(intrinsic.b.monsters.arr[0].curHp == 1000 && intrinsic.b.player.energy == 3,
          "rejected Pummel does not execute its effects");

    Fixture manual;
    manual.play(CardId::WOUND);
    check(manual.b.cards.cardsInHand == 1 && manual.b.cards.hand[0].id == CardId::WOUND,
          "ordinary manual rejection leaves card in hand");
    check(manual.b.cards.exhaustPile.empty() && manual.b.cards.discardPile.empty(),
          "manual rejection does not invoke autoplay cleanup");

    Fixture success;
    uid = success.top(CardId::STRIKE_RED);
    success.b.player.buff<PS::FEEL_NO_PAIN>(3);
    success.play();
    check(success.b.monsters.arr[0].curHp == 994 && success.b.player.block == 3,
          "successful autoplay still executes attack and exhaust");
    check(success.b.cards.exhaustPile.size() == 1 && contains(success.b.cards.exhaustPile, uid),
          "successful autoplay exhausts exactly once");
    check(success.b.player.cardsPlayedThisTurn == 2 && success.b.player.attacksPlayedThisTurn == 1,
          "successful autoplay still increments counters");
}
}
int main() {
    rejectedHavoc();
    restrictionsAndCallbacks();
    controls();
    std::cout << "REJECTED_AUTOPLAY_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
