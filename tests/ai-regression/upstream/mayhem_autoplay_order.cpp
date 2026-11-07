// Standalone regression for Mayhem scheduling and FIFO top-card autoplay.
// Uses upstream interfaces only; no dependency on other pending fixes.
#include <algorithm>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 24) std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    BattleContext b;
    explicit Fixture(int seed = 123456789,
                     MonsterEncounter encounter = MonsterEncounter::CULTIST) {
        GameContext game(CharacterClass::IRONCLAD, seed, 0);
        game.floorNum = 1; game.relics = {}; game.enterBattle(encounter);
        b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 3; b.player.curHp = 70; b.player.maxHp = 80;
        for (int i = 0; i < b.monsters.monsterCount; ++i)
            b.monsters.arr[i].curHp = b.monsters.arr[i].maxHp = 1000;
    }
    void top(CardId id, bool up = false) {
        CardInstance c(id, up); c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.drawPile.push_back(c);
    }
    void draws() { for (int i = 0; i < 5; ++i) top(CardId::STRIKE_RED); }
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
void turnOrdering(int seed) {
    for (bool up : {false, true}) {
        Fixture f(seed); f.top(CardId::BLUDGEON); f.draws(); f.play(CardId::MAYHEM, up);
        check(f.b.player.energy == (up ? 2 : 1), "Mayhem upgrade changes cost two to one");
        check(f.b.player.getStatus<PS::MAYHEM>() == 1, "both Mayhem variants grant one stack");
        check(f.b.cards.drawPile.size() == 6 && f.b.monsters.arr[0].curHp == 1000,
              "playing Mayhem does not autoplay immediately");
        f.turn();
        bool strikes = f.b.cards.cardsInHand == 5;
        for (int i = 0; i < f.b.cards.cardsInHand; ++i)
            strikes &= f.b.cards.hand[i].id == CardId::STRIKE_RED;
        check(strikes, "normal draw takes the five Strikes before autoplay");
        check(f.b.monsters.arr[0].curHp == 968, "Mayhem plays sixth-card Bludgeon for 32, not first-card Strike for 6");
        check(f.b.player.energy == 3, "automatic positive-cost card preserves recharged energy");
        check(count(f.b.cards.discardPile, CardId::BLUDGEON) == 1 && f.b.cards.exhaustPile.empty(),
              "Mayhem does not force exhaustion");
        check(f.b.player.cardsPlayedThisTurn == 1, "automatic card is counted once");
    }
    Fixture stacked(seed); stacked.top(CardId::STRIKE_RED); stacked.top(CardId::INFLAME); stacked.draws();
    stacked.b.player.buff<PS::MAYHEM>(2); stacked.turn();
    check(stacked.b.player.getStatus<PS::STRENGTH>() == 2 && stacked.b.monsters.arr[0].curHp == 992,
          "stacked Mayhem plays Inflame before Strike, dealing 8 rather than 6");
    check(stacked.b.player.cardsPlayedThisTurn == 2 && stacked.b.cards.cardsInHand == 5,
          "two stacks perform two plays after five draws");
    check(count(stacked.b.cards.discardPile, CardId::STRIKE_RED) == 1 && stacked.b.cards.exhaustPile.empty(),
          "Power removal and ordinary attack discard stay distinct");
}
void wrapperTiming() {
    Fixture f(1, MonsterEncounter::SMALL_SLIMES);
    f.top(CardId::BLUDGEON); f.top(CardId::DEFEND_RED); f.top(CardId::STRIKE_RED);
    f.b.player.buff<PS::MAYHEM>(2);
    const int rng = f.b.cardRandomRng.counter;
    f.b.player.applyStartOfTurnPowers(f.b);
    check(f.b.cardRandomRng.counter == rng, "registering turn triggers does not select random targets yet");
    check(f.b.actionQueue.size == 2, "one wrapper is queued per stack");
    // Model another already queued start/post-draw effect without depending on
    // any other Power's implementation or on its independent pending PR.
    f.b.addToBot(Actions::DrawCards(1));
    f.run();
    check(f.b.cards.cardsInHand == 1 && f.b.cards.hand[0].id == CardId::STRIKE_RED,
          "wrapper appends actual top plays behind the already queued draw");
    check(f.b.player.block == 5, "Defend after the draw is played, not drawn");
    check(f.b.monsters.arr[0].curHp + f.b.monsters.arr[1].curHp == 1968,
          "one live random target receives Bludgeon damage");
    check(f.b.cardRandomRng.counter == rng + 2, "each wrapper selects one target");
}
void fifoAndControls() {
    for (bool exhausts : {false, true}) {
        Fixture f; f.top(CardId::STRIKE_RED); f.top(CardId::INFLAME);
        f.b.playTopCardInDrawPile(0, exhausts);
        f.b.playTopCardInDrawPile(0, exhausts);
        f.run();
        check(f.b.monsters.arr[0].curHp == 992, "top plays append FIFO for both lifecycle flags");
        check(f.b.player.getStatus<PS::STRENGTH>() == 2, "first queued Power applies");
        check((exhausts ? count(f.b.cards.exhaustPile, CardId::STRIKE_RED)
                        : count(f.b.cards.discardPile, CardId::STRIKE_RED)) == 1,
              "FIFO retains exhaust/discard lifecycle");
    }
    for (bool up : {false, true}) {
        Fixture havoc; havoc.top(CardId::BLUDGEON); havoc.play(CardId::HAVOC, up);
        check(havoc.b.monsters.arr[0].curHp == 968 && havoc.b.player.energy == (up ? 3 : 2),
              "ordinary Havoc remains immediate and only pays its own cost");
        check(count(havoc.b.cards.exhaustPile, CardId::BLUDGEON) == 1,
              "Havoc still forces exhaustion");
    }
    Fixture full; full.top(CardId::DEFEND_RED);
    for (int i = 0; i < 10; ++i) full.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
    full.b.player.buff<PS::MAYHEM>(1); full.turn();
    check(full.b.player.block == 5 && full.b.cards.cardsInHand == 10, "full hand does not block top autoplay");
    Fixture shuffle; shuffle.top(CardId::BLUDGEON);
    shuffle.b.cards.discardPile = shuffle.b.cards.drawPile; shuffle.b.cards.drawPile.clear(); shuffle.draws();
    shuffle.b.player.buff<PS::MAYHEM>(1); shuffle.turn();
    check(shuffle.b.monsters.arr[0].curHp == 968 && count(shuffle.b.cards.discardPile, CardId::BLUDGEON) == 1,
          "autoplay shuffles discard when the normal draw empties the draw pile");
    Fixture empty; empty.draws(); empty.b.player.buff<PS::MAYHEM>(1); empty.turn();
    check(empty.b.cards.cardsInHand == 5 && empty.b.player.cardsPlayedThisTurn == 0,
          "empty remaining piles cause no phantom play");
    Fixture plain; plain.top(CardId::BLUDGEON); plain.draws(); plain.turn();
    check(plain.b.cards.cardsInHand == 5 && plain.b.cards.drawPile.size() == 1
          && plain.b.monsters.arr[0].curHp == 1000, "turn without Mayhem only draws normally");
}
}
int main() {
    for (int seed = 1; seed <= 16; ++seed) turnOrdering(seed);
    wrapperTiming(); fifoAndControls();
    std::cout << "MAYHEM_AUTOPLAY_ORDER_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
