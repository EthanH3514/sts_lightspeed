// Standalone regression: upstream master plus only the random-draw cost fix.
#include <algorithm>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/Game.h"
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
        game.floorNum = 1;
        game.relics = {};
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
    void run() {
        b.inputState = InputState::EXECUTING_ACTIONS;
        b.executeActions();
    }
    void play(CardId id, bool upgraded) {
        b.cards.createTempCardInHand(CardInstance(id, upgraded));
        b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy));
        run();
    }
};
int seedFor(CardType type, CardId wanted) {
    // Test-only fixture selection, not policy access to a real game's RNG.
    for (int seed = 1; seed < 10000; ++seed) {
        Random rng(seed);
        if (getTrulyRandomCardInCombat(rng, CharacterClass::IRONCLAD, type) == wanted) return seed;
    }
    check(false, "fixed seed corpus reaches requested card");
    return 1;
}
void costControls() {
    for (CardId wanted : {CardId::WHIRLWIND, CardId::BLUDGEON, CardId::ANGER,
                          CardId::IMPERVIOUS, CardId::FLEX}) {
        const auto type = CardInstance(wanted).getType();
        const auto generator = type == CardType::ATTACK ? CardId::METAMORPHOSIS : CardId::CHRYSALIS;
        for (bool upgraded : {false, true}) {
            Fixture f;
            f.b.cardRandomRng = Random(seedFor(type, wanted));
            f.play(generator, upgraded);
            check(f.b.player.energy == 1, "generator still costs two energy");
            check(f.b.cards.drawPile.size() == (upgraded ? 5u : 3u), "generator retains base/upgraded count");
            check(f.b.cards.exhaustPile.size() == 1, "generator still exhausts");
            check(f.b.cards.drawPile.back().id == wanted, "selected first card stays on top of initially empty pile");
            for (const auto &c : f.b.cards.drawPile) {
                const int expected = std::min<int>(0, CardInstance(c.id).cost);
                check(c.cost == expected && c.costForTurn == expected,
                      "only positive costs become zero; X and existing zero stay unchanged");
                check(!c.upgraded, "upgraded generator still creates base cards");
            }
            f.b.cards.resetAttributesAtEndOfTurn();
            for (const auto &c : f.b.cards.drawPile) {
                check(c.costForTurn == std::min<int>(0, CardInstance(c.id).cost),
                      "generated costs remain correct after end-turn reset");
            }
        }
    }
}
void generatedWhirlwindPlay() {
    for (bool upgraded : {false, true}) for (int energy : {0, 1, 3, 5}) {
        Fixture f;
        f.b.player.buff<PS::DARK_EMBRACE>(1);
        f.b.cardRandomRng = Random(seedFor(CardType::ATTACK, CardId::WHIRLWIND));
        f.play(CardId::METAMORPHOSIS, upgraded);
        check(f.b.cards.cardsInHand == 1 && f.b.cards.hand[0].id == CardId::WHIRLWIND,
              "Metamorphosis exhaust draws generated Whirlwind through Dark Embrace");
        check(f.b.cards.hand[0].cost == -1 && f.b.cards.hand[0].costForTurn == -1,
              "drawn generated Whirlwind remains X-cost");
        f.b.player.energy = energy;
        const int hp = f.b.monsters.arr[0].curHp;
        f.b.addToBotCard(CardQueueItem(f.b.cards.hand[0], 0, energy));
        f.run();
        check(f.b.player.energy == 0, "manual generated Whirlwind spends available energy");
        check(f.b.monsters.arr[0].curHp == hp - 5 * energy,
              "generated Whirlwind deals one unupgraded attack per energy");
        check(f.b.cards.discardPile.size() == 1 && f.b.cards.discardPile[0].id == CardId::WHIRLWIND,
              "manual generated Whirlwind settles normally");
        if (!upgraded && energy == 3) {
            std::cout << "Generated Whirlwind at energy3: energy_after=" << f.b.player.energy
                      << ", damage=" << hp - f.b.monsters.arr[0].curHp << '\n';
        }
    }
}
void confusionInteraction() {
    for (bool upgraded : {false, true}) {
        Fixture f;
        f.b.cardRandomRng = Random(seedFor(CardType::ATTACK, CardId::WHIRLWIND));
        f.play(CardId::METAMORPHOSIS, upgraded);
        f.b.player.buff<PS::CONFUSED>();
        // Choose an audit-only draw seed whose first ordinary confusion roll is 3.
        // A true X-cost card must not consume this roll at all.
        int drawSeed = 1;
        for (; drawSeed < 10000; ++drawSeed) {
            Random candidate(drawSeed);
            if (candidate.random(3) == 3) break;
        }
        check(drawSeed < 10000, "fixed draw-seed corpus reaches cost three");
        f.b.cardRandomRng = Random(drawSeed);
        const auto counter = f.b.cardRandomRng.counter;
        f.b.cards.draw(f.b, 1);
        const auto &card = f.b.cards.hand[0];
        check(card.id == CardId::WHIRLWIND, "confusion fixture draws generated Whirlwind");
        check(card.cost == -1 && card.costForTurn == -1, "Confusion must leave generated X cost alone");
        check(f.b.cardRandomRng.counter == counter, "drawing X must not consume a Confusion cost roll");
        f.b.player.energy = 0;
        check(card.canUse(f.b, 0, false), "generated Whirlwind remains playable at zero energy under Confusion");
        std::cout << "Generated Whirlwind under Confusion: cost=" << int(card.cost)
                  << ", turn_cost=" << int(card.costForTurn)
                  << ", extra_rng=" << f.b.cardRandomRng.counter - counter << '\n';
    }
}
}
int main() {
    costControls();
    generatedWhirlwindPlay();
    confusionInteraction();
    std::cout << "RANDOM_DRAW_X_COST_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
