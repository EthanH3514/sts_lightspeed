// Mayhem start-of-turn scheduling and ordinary (non-forced-exhaust) autoplay.
// Reference: MayhemPower.atStartOfTurn, MayhemPower$1.update and PlayTopCardAction.
#include <algorithm>
#include <array>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
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
    explicit Fixture(RelicId relic = RelicId::INVALID,
                     MonsterEncounter encounter = MonsterEncounter::CULTIST)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1; game.relics = {};
        if (relic != RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(encounter); b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 3; b.player.curHp = 70; b.player.maxHp = 80;
        for (int i = 0; i < b.monsters.monsterCount; ++i)
            b.monsters.arr[i].curHp = b.monsters.arr[i].maxHp = 1000;
    }
    int top(CardId id, bool up = false) {
        CardInstance c(id, up); c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.drawPile.push_back(c); return c.uniqueId;
    }
    void draws(int n = 5) { for (int i = 0; i < n; ++i) top(CardId::STRIKE_RED); }
    void run() { b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void turn() { b.skipMonsterTurn = true; b.afterMonsterTurns(); run(); }
    void play(CardId id, bool up = false) {
        b.cards.createTempCardInHand(CardInstance(id, up));
        b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy));
        run();
    }
};
int count(const std::vector<CardInstance> &pile, CardId id) {
    return std::count_if(pile.begin(), pile.end(), [=](const auto &c) { return c.id == id; });
}
void registrationAndOrdering() {
    for (bool up : {false, true}) {
        Fixture f; f.top(CardId::BLUDGEON); f.draws(); f.play(CardId::MAYHEM, up);
        check(f.b.player.energy == (up ? 2 : 1), "base costs two; upgraded costs one");
        check(f.b.player.getStatus<PS::MAYHEM>() == 1, "either variant registers one stack");
        check(f.b.monsters.arr[0].curHp == 1000 && f.b.cards.drawPile.size() == 6,
              "registration does not autoplay immediately");
        check(f.b.cards.discardPile.empty() && f.b.cards.exhaustPile.empty(), "Mayhem Power leaves play without exhausting");
        f.turn();
        check(f.b.cards.cardsInHand == 5, "normal turn draws five cards");
        bool strikes = true;
        for (int i = 0; i < f.b.cards.cardsInHand; ++i) strikes &= f.b.cards.hand[i].id == CardId::STRIKE_RED;
        check(strikes, "normal draws take the five Strikes before Mayhem takes Bludgeon");
        check(f.b.monsters.arr[0].curHp == 968, "sixth card Bludgeon, not the original top Strike, is played");
        check(f.b.player.energy == 3, "automatic expensive attack is free after energy recharge");
        check(count(f.b.cards.discardPile, CardId::BLUDGEON) == 1 && f.b.cards.exhaustPile.empty(),
              "Mayhem discards an ordinary attack without forced exhaustion");
        check(f.b.player.cardsPlayedThisTurn == 1, "draws are not plays and counters reset before autoplay");
    }
    Fixture strength; strength.top(CardId::STRIKE_RED); strength.draws();
    strength.b.player.buff<PS::MAYHEM>(1); strength.b.player.buff<PS::DEMON_FORM>(3); strength.turn();
    check(strength.b.monsters.arr[0].curHp == 991, "post-draw Demon Form Strength applies before Mayhem attack");
    Fixture brutality; brutality.top(CardId::BLUDGEON); brutality.draws(6);
    brutality.b.player.buff<PS::MAYHEM>(1); brutality.b.player.buff<PS::BRUTALITY>(1); brutality.turn();
    check(brutality.b.cards.cardsInHand == 6 && brutality.b.monsters.arr[0].curHp == 968,
          "post-draw Brutality draw precedes Mayhem top removal");
    check(brutality.b.player.curHp == 69, "Brutality HP loss still resolves once");
    Fixture stacked; stacked.top(CardId::STRIKE_RED); stacked.top(CardId::INFLAME); stacked.draws();
    stacked.b.player.buff<PS::MAYHEM>(2); stacked.turn();
    check(stacked.b.player.getStatus<PS::STRENGTH>() == 2 && stacked.b.monsters.arr[0].curHp == 992,
          "stacked wrappers preserve autoplay order; first Power affects second attack");
    check(stacked.b.player.cardsPlayedThisTurn == 2 && stacked.b.cards.exhaustPile.empty(), "each stacked autoplay counts once");
    Fixture timing(RelicId::INVALID, MonsterEncounter::SMALL_SLIMES);
    timing.top(CardId::STRIKE_RED); timing.b.player.buff<PS::MAYHEM>(2);
    const int counter = timing.b.cardRandomRng.counter;
    timing.b.player.applyStartOfTurnPowers(timing.b);
    check(timing.b.cardRandomRng.counter == counter, "target RNG is not consumed at trigger registration");
    // Execute exactly the two wrappers, not the subsequently appended top plays.
    timing.b.actionQueue.popFront()(timing.b); timing.b.actionQueue.popFront()(timing.b);
    check(timing.b.cardRandomRng.counter == counter + 2 && timing.b.cards.drawPile.size() == 1,
          "each wrapper chooses a target then appends a deferred top play");
}
void lifecycle() {
    // Isolate the shared queue-order fix from Mayhem's turn-start wrapper fix.
    for (bool exhausts : {false, true}) {
        Fixture fifo; fifo.top(CardId::STRIKE_RED); fifo.top(CardId::INFLAME);
        fifo.b.playTopCardInDrawPile(0, exhausts);
        fifo.b.playTopCardInDrawPile(0, exhausts);
        fifo.run();
        check(fifo.b.monsters.arr[0].curHp == 992 && fifo.b.player.getStatus<PS::STRENGTH>() == 2,
              "queued top plays use FIFO for both Mayhem and Havoc lifecycle flags");
        check((exhausts ? count(fifo.b.cards.exhaustPile, CardId::STRIKE_RED)
                        : count(fifo.b.cards.discardPile, CardId::STRIKE_RED)) == 1,
              "FIFO preserves forced-exhaust versus ordinary-discard behavior");
    }
    for (CardId id : {CardId::WOUND, CardId::BURN, CardId::DAZED, CardId::REGRET}) {
        Fixture f; f.top(id); f.draws(); f.b.player.buff<PS::MAYHEM>(1); f.b.player.buff<PS::FEEL_NO_PAIN>(3); f.turn();
        check(count(f.b.cards.discardPile, id) == 1 && f.b.cards.exhaustPile.empty(), "rejected autoplay discards, not forced exhaust or disappearance");
        check(f.b.player.cardsPlayedThisTurn == 0 && f.b.player.block == 0 && f.b.player.curHp == 70,
              "rejection has no play, exhaustion or end-turn curse/status callback");
    }
    Fixture exhaust; exhaust.top(CardId::SLIMED); exhaust.draws();
    exhaust.b.player.buff<PS::MAYHEM>(1); exhaust.b.player.buff<PS::FEEL_NO_PAIN>(3); exhaust.turn();
    check(count(exhaust.b.cards.exhaustPile, CardId::SLIMED) == 1 && exhaust.b.player.block == 3,
          "intrinsic exhaustion still triggers Feel No Pain");
    Fixture corruption; corruption.top(CardId::DEFEND_RED); corruption.draws();
    corruption.b.player.buff<PS::MAYHEM>(1); corruption.b.player.buff<PS::CORRUPTION>();
    corruption.b.player.buff<PS::FEEL_NO_PAIN>(3); corruption.turn();
    check(corruption.b.player.block == 8 && count(corruption.b.cards.exhaustPile, CardId::DEFEND_RED) == 1,
          "Corruption exhausts the automatic Skill exactly once");
    for (bool up : {false, true}) {
        Fixture x; x.top(CardId::WHIRLWIND, up); x.draws(); x.b.player.buff<PS::MAYHEM>(1); x.turn();
        check(x.b.player.energy == 3 && x.b.monsters.arr[0].curHp == 1000 - 3 * (up ? 8 : 5),
              "automatic X attack uses recharged energy without consuming it");
    }
    Fixture noDraw; noDraw.top(CardId::DEFEND_RED); noDraw.b.player.buff<PS::NO_DRAW>();
    noDraw.b.player.buff<PS::MAYHEM>(1); noDraw.turn();
    check(noDraw.b.player.block == 5 && noDraw.b.cards.cardsInHand == 0, "No Draw does not prevent top autoplay");
    Fixture full; full.top(CardId::DEFEND_RED);
    for (int i = 0; i < 10; ++i) full.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
    full.b.player.buff<PS::MAYHEM>(1); full.turn();
    check(full.b.player.block == 5 && full.b.cards.cardsInHand == 10, "full hand does not prevent top autoplay");
    Fixture shuffle; shuffle.top(CardId::BLUDGEON); shuffle.b.cards.discardPile = shuffle.b.cards.drawPile;
    shuffle.b.cards.drawPile.clear(); shuffle.draws(); shuffle.b.player.buff<PS::MAYHEM>(1); shuffle.turn();
    check(shuffle.b.monsters.arr[0].curHp == 968 && count(shuffle.b.cards.discardPile, CardId::BLUDGEON) == 1,
          "Mayhem shuffles discard if normal draw empties draw pile");
    Fixture empty; empty.draws(); empty.b.player.buff<PS::MAYHEM>(1); empty.turn();
    check(empty.b.player.cardsPlayedThisTurn == 0 && empty.b.cards.cardsInHand == 5,
          "empty remaining piles cause no phantom autoplay");
    Fixture selection; selection.top(CardId::TRUE_GRIT, true); selection.draws();
    selection.b.player.buff<PS::MAYHEM>(1); selection.turn();
    check(selection.b.inputState == InputState::CARD_SELECT, "automatic card can suspend on a real selection");
    bool refused = false;
    try { public_sampling::resampleCombatContinuation(selection.game, selection.b, {11,22,33,44,55,66,77}); }
    catch (const std::runtime_error &) { refused = true; }
    check(refused, "pending autoplay selection cannot be resampled");
}
void sampledRoots() {
    for (bool frozen : {false, true}) for (int seed = 1; seed <= 24; ++seed) {
        Fixture f(frozen ? RelicId::FROZEN_EYE : RelicId::INVALID, MonsterEncounter::SMALL_SLIMES);
        for (int i = 0; i < 9; ++i) f.top(i % 2 ? CardId::BASH : CardId::STRIKE_RED);
        f.b.player.buff<PS::MAYHEM>(2);
        auto twin = f.b;
        if (!frozen) std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        twin.cardRandomRng = Random(99999); twin.shuffleRng = Random(88888);
        std::array<std::uint64_t, 7> seeds = {11, static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        f.turn(); twin.skipMonsterTurn = true; twin.afterMonsterTurns();
        twin.inputState = InputState::EXECUTING_ACTIONS; twin.executeActions();
        check(f.b.cards.cardsInHand == twin.cards.cardsInHand && f.b.cards.discardPile.size() == 2,
              "paired sampled roots draw normally and perform both autoplays");
        for (int i = 0; i < 2; ++i)
            check(f.b.monsters.arr[i].curHp == twin.monsters.arr[i].curHp,
                  "paired targets and damage ignore original hidden RNG/order");
        for (int i = 0; i < f.b.cards.cardsInHand; ++i)
            check(f.b.cards.hand[i].uniqueId == twin.cards.hand[i].uniqueId, "paired draws preserve sampled identities");
    }
}
}
int main() {
    registrationAndOrdering(); lifecycle(); sampledRoots();
    std::cout << "MAYHEM_FOUNDATION_" << (failures ? "FAILED" : "OK") << " ("
              << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
