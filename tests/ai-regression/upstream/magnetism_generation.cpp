// Standalone regression: upstream master plus only the Magnetism fix.
#include <algorithm>
#include <iostream>
#include <vector>
#include "combat/BattleContext.h"
#include "game/Game.h"
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
    GameContext game;
    BattleContext b;
    Fixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1; game.relics = {};
        game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 20; b.player.curHp = 70; b.player.maxHp = 80;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
    }
    void run() { b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(bool upgraded) {
        b.cards.createTempCardInHand(CardInstance(CardId::MAGNETISM, upgraded));
        b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy));
        run();
    }
};
std::vector<CardId> expected(Random &rng, int count) {
    std::vector<CardId> result;
    for (int i = 0; i < count; ++i) result.push_back(getTrulyRandomColorlessCardInCombat(rng));
    return result;
}
void ordinaryTurns() {
    for (bool up : {false, true}) for (int stacks : {1, 2, 3}) for (int seed = 1; seed <= 16; ++seed) {
        Fixture f;
        for (int i = 0; i < 10; ++i) f.b.cards.createTempCardInDrawPile(i, CardInstance(CardId::DEFEND_RED));
        for (int i = 0; i < stacks; ++i) f.play(up);
        check(f.b.player.energy == 20 - stacks * (up ? 1 : 2), "upgrade changes source cost only");
        check(f.b.player.getStatus<PS::MAGNETISM>() == stacks, "each play adds one stack");
        check(f.b.cards.cardsInHand == 0, "power does not generate immediately");
        check(f.b.cards.discardPile.empty() && f.b.cards.exhaustPile.empty(), "power leaves active card piles");
        f.b.cardRandomRng = Random(seed);
        for (int turn = 0; turn < 2; ++turn) {
            auto rng = f.b.cardRandomRng;
            const auto ids = expected(rng, stacks);
            const int nextId = f.b.cards.nextUniqueCardId;
            search::Action(search::ActionType::END_TURN).execute(f.b);
            check(f.b.cards.cardsInHand == stacks + 5, "every turn generates per stack then draws five");
            check(f.b.cardRandomRng.counter == rng.counter, "generation consumes one identity roll per stack");
            for (int i = 0; i < stacks; ++i) {
                const auto *card = f.b.cards.cardsInHand > i ? &f.b.cards.hand[i] : nullptr;
                check(card && card->id == ids[i] && !card->upgraded, "base random copies precede normal draws");
                check(card && card->cost == CardInstance(ids[i]).cost && card->costForTurn == card->cost,
                      "generated card retains normal cost, including X");
                check(card && card->uniqueId == nextId + i, "each generated card has a fresh combat identity");
            }
            check(f.b.cards.cardsInHand > stacks && f.b.cards.hand[stacks].id == CardId::DEFEND_RED,
                  "ordinary draw follows insertion");
            check(f.b.player.getStatus<PS::MAGNETISM>() == stacks, "generation does not consume power stacks");
        }
    }
}
void overflowAndTiming() {
    for (int occupied : {0, 9, 10}) {
        Fixture f;
        f.b.player.buff<PS::MAGNETISM>(3);
        f.b.player.buff<PS::NO_DRAW>();
        f.b.player.buff<PS::CONFUSED>();
        for (int i = 0; i < occupied; ++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        auto rng = f.b.cardRandomRng;
        const auto ids = expected(rng, 3);
        f.b.player.applyStartOfTurnPowers(f.b);
        check(f.b.cards.cardsInHand == occupied, "generation insertion is queued, not immediate");
        check(f.b.cardRandomRng.counter == rng.counter, "identity selection occurs in start-turn callback");
        f.run();
        const int inHand = std::min(3, 10 - occupied);
        check(f.b.cards.cardsInHand == occupied + inHand, "No Draw does not prevent generation");
        check(f.b.cards.discardPile.size() == static_cast<unsigned>(3 - inHand), "full hand overflows to discard");
        check(f.b.cardRandomRng.counter == rng.counter, "generation does not trigger Confusion draw rolls");
        for (int i = 0; i < 3; ++i) {
            const CardInstance *card = nullptr;
            if (i < inHand && f.b.cards.cardsInHand > occupied + i) card = &f.b.cards.hand[occupied + i];
            if (i >= inHand && f.b.cards.discardPile.size() > static_cast<unsigned>(i - inHand))
                card = &f.b.cards.discardPile[i - inHand];
            check(card && card->id == ids[i], "selection order is retained through overflow");
            check(card && card->costForTurn == CardInstance(ids[i]).cost, "generated cards have no temporary discount");
        }
    }
}
void terminalControls() {
    Fixture empty; empty.play(false);
    const bool canContinue = empty.b.outcome == Outcome::UNDECIDED && empty.b.inputState == InputState::PLAYER_NORMAL;
    check(canContinue, "active Magnetism prevents premature no-cards loss");
    if (canContinue) search::Action(search::ActionType::END_TURN).execute(empty.b);
    check(canContinue && empty.b.cards.cardsInHand == 1, "empty deck can generate next turn");
    std::cout << "Empty deck with Magnetism: can_continue=" << canContinue
              << ", next_hand=" << empty.b.cards.cardsInHand << '\n';

    Fixture noPower; noPower.run();
    check(noPower.b.outcome == Outcome::PLAYER_LOSS, "no-generator empty-deck shortcut remains unchanged");
    Fixture dead; dead.b.player.buff<PS::MAGNETISM>();
    dead.b.monsters.arr[0].curHp = 0; dead.b.monsters.monstersAlive = 0;
    const auto counter = dead.b.cardRandomRng.counter;
    dead.b.player.applyStartOfTurnPowers(dead.b);
    check(dead.b.cardRandomRng.counter == counter && dead.b.actionQueue.isEmpty(),
          "no generation when monsters are basically dead");
}
}
int main() {
    ordinaryTurns(); overflowAndTiming(); terminalControls();
    std::cout << "MAGNETISM_GENERATION_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
