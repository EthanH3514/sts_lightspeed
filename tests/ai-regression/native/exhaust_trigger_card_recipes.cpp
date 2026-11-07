#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    GameContext game;
    BattleContext battle;
    explicit Fixture(bool frozen = false) : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (frozen) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
        battle.player.energy = 20;
        for (int i = 0; i < 12; ++i) draw(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED);
    }
    void hand(CardId id, bool up = false) {
        CardInstance c(id, up);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = c;
    }
    void draw(CardId id) {
        CardInstance c(id);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.drawPile.push_back(c);
    }
    void execute() {
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }
    void play() {
        auto c = battle.cards.hand[0];
        require(c.canUse(battle, 0, false), "card not playable");
        battle.addToBotCard(CardQueueItem(c, 0, battle.player.energy));
        execute();
    }
    void powers(bool blockFirst, bool up = false) {
        hand(blockFirst ? CardId::FEEL_NO_PAIN : CardId::DARK_EMBRACE, up);
        hand(blockFirst ? CardId::DARK_EMBRACE : CardId::FEEL_NO_PAIN, up);
        play(); play();
    }
    void exhaust() {
        CardInstance c(CardId::WOUND);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.triggerAndMoveToExhaustPile(c);
    }
};

void verifyPowerVariants() {
    for (bool up : {false, true}) {
        Fixture f;
        f.powers(false, up);
        require(f.battle.player.energy == 20 - 1 - (up ? 1 : 2), "power cost/upgrade");
        require(f.battle.cards.cardsInHand == 0 && f.battle.cards.exhaustPile.empty() &&
                f.battle.cards.discardPile.empty(), "power card movement");
        require(f.battle.player.block == 0, "registration granted immediate block");
        f.battle.player.dexterity = 7;
        f.battle.player.debuff<PS::FRAIL>(2, false);
        f.hand(CardId::SLIMED);
        f.play();
        require(f.battle.player.block == (up ? 4 : 3), "power block incorrectly used Dexterity/Frail");
        require(f.battle.cards.cardsInHand == 1, "Dark Embrace upgrade changed draw amount");
        require(f.battle.cards.exhaustPile.size() == 1, "self exhaust duplicated");
    }
}

// Ordering-only regression: the first callback differs, while the ordinary
// fully drained case below has the same block/draw totals in either order.
// This does not establish an HP, card-count or victory impact. Keep upstream
// submission deferred until a functional divergence attributable to this order
// has a reproducer (see docs/upstream-fidelity.md).
void verifyOrder() {
    for (bool blockFirst : {false, true}) {
        for (bool stack : {false, true}) {
            for (bool reapply : {false, true}) {
                Fixture f;
                f.powers(blockFirst);
                if (stack) {
                    f.battle.player.buff<PS::FEEL_NO_PAIN>(4);
                    f.battle.player.buff<PS::DARK_EMBRACE>(1);
                }
                bool expectedBlockFirst = blockFirst;
                if (reapply) {
                    if (blockFirst) {
                        f.battle.player.removeStatus<PS::FEEL_NO_PAIN>();
                        f.battle.player.buff<PS::FEEL_NO_PAIN>(3);
                    } else {
                        f.battle.player.removeStatus<PS::DARK_EMBRACE>();
                        f.battle.player.buff<PS::DARK_EMBRACE>(1);
                    }
                    expectedBlockFirst = !blockFirst;
                }
                f.exhaust();
                require(f.battle.actionQueue.size == 2, "expected exactly two power callbacks");
                auto copy = f.battle;
                auto first = copy.actionQueue.popFront();
                first(copy);
                require((copy.player.block > 0) == expectedBlockFirst,
                        "exhaust callbacks ignored acquisition order");
                require((copy.cards.cardsInHand > 0) != expectedBlockFirst,
                        "wrong first exhaust callback");
                require(f.battle.player.block == 0 && f.battle.cards.cardsInHand == 0,
                        "callback copy mutated source");
                f.execute();
                require(f.battle.player.block == f.battle.player.getStatus<PS::FEEL_NO_PAIN>(),
                        "stacked block callback");
                require(f.battle.cards.cardsInHand == f.battle.player.getStatus<PS::DARK_EMBRACE>(),
                        "stacked draw callback");
            }
        }
    }
}

void verifyBoundsAndNestedDraw() {
    for (bool noDraw : {false, true}) {
        Fixture f;
        f.powers(true);
        int handSize = noDraw ? 3 : 10;
        for (int i = 0; i < handSize; ++i) f.hand(CardId::DEFEND_RED);
        if (noDraw) f.battle.player.debuff<PS::NO_DRAW>(1, false);
        f.exhaust(); f.execute();
        require(f.battle.cards.cardsInHand == handSize && f.battle.player.block == 3,
                "hand cap/No Draw suppressed block or exceeded cap");
    }
    Fixture chain;
    chain.powers(true);
    chain.hand(CardId::EVOLVE); chain.hand(CardId::FIRE_BREATHING);
    chain.play(); chain.play();
    chain.draw(CardId::WOUND);
    int hp = chain.battle.monsters.arr[0].curHp;
    chain.exhaust(); chain.execute();
    require(chain.battle.cards.cardsInHand == 2 && chain.battle.player.block == 3 &&
            hp - chain.battle.monsters.arr[0].curHp == 6, "nested exhaust/draw/status triggers");
    Fixture lethal;
    lethal.powers(false);
    lethal.hand(CardId::FIRE_BREATHING); lethal.play();
    lethal.draw(CardId::WOUND);
    lethal.battle.monsters.arr[0].curHp = 6;
    lethal.exhaust(); lethal.execute();
    require(lethal.battle.outcome == Outcome::PLAYER_VICTORY && lethal.battle.player.block == 3,
            "nested lethal cleanup lost exhaust block");
}

void verifyHandSelectionMultipleAndShuffle() {
    for (CardId source : {CardId::TRUE_GRIT, CardId::BURNING_PACT}) {
        Fixture f;
        f.powers(true);
        f.hand(source, true);
        f.hand(CardId::WOUND);
        const int woundId = f.battle.cards.hand[1].getUniqueId();
        f.hand(CardId::DEFEND_RED);
        f.play();
        require(f.battle.inputState == InputState::CARD_SELECT &&
                f.battle.cardSelectInfo.cardSelectTask == CardSelectTask::EXHAUST_ONE,
                "chosen exhaust boundary missing");
        f.battle.chooseExhaustOneCard(0); f.execute();
        require(f.battle.cards.exhaustPile.size() == 1 &&
                f.battle.cards.exhaustPile[0].getUniqueId() == woundId,
                "chosen exhaust identity changed");
        require(f.battle.player.block == (source == CardId::TRUE_GRIT ? 12 : 3),
                "chosen exhaust block callback");
        require(f.battle.cards.cardsInHand == (source == CardId::BURNING_PACT ? 5 : 2),
                "chosen exhaust draw count");
    }
    Fixture many;
    many.powers(false);
    many.hand(CardId::WOUND); many.hand(CardId::DAZED);
    many.battle.addToBot(Actions::ExhaustRandomCardInHand(2)); many.execute();
    require(many.battle.cards.exhaustPile.size() == 2 && many.battle.cards.cardsInHand == 2 &&
            many.battle.player.block == 6, "multiple exhaust must trigger once per card");
    Fixture shuffled;
    shuffled.powers(true);
    shuffled.battle.cards.discardPile = shuffled.battle.cards.drawPile;
    shuffled.battle.cards.drawPile.clear();
    shuffled.exhaust(); shuffled.execute();
    require(shuffled.battle.cards.cardsInHand == 1 && shuffled.battle.player.block == 3,
            "exhaust draw failed to reshuffle");
    Fixture empty;
    empty.powers(false);
    empty.hand(CardId::STRIKE_RED);
    empty.battle.cards.drawPile.clear();
    empty.exhaust(); empty.execute();
    require(empty.battle.cards.cardsInHand == 1 && empty.battle.player.block == 3,
            "empty supply suppressed block or invented a draw");
    Fixture generated;
    generated.powers(true);
    generated.battle.addToBot(Actions::MakeTempCardInHand(CardId::WOUND, false, 2));
    generated.execute();
    require(generated.battle.player.block == 0 && generated.battle.cards.cardsInHand == 2,
            "card generation incorrectly triggered exhaust powers");
    Fixture persistent;
    persistent.powers(true);
    search::Action end(search::ActionType::END_TURN);
    end.execute(persistent.battle);
    require(persistent.battle.player.getStatus<PS::FEEL_NO_PAIN>() == 3 &&
            persistent.battle.player.getStatus<PS::DARK_EMBRACE>() == 1,
            "exhaust powers failed to persist across turns");
    int before = persistent.battle.cards.cardsInHand;
    persistent.exhaust(); persistent.execute();
    require(persistent.battle.player.block == 3 && persistent.battle.cards.cardsInHand == before + 1,
            "persistent exhaust callback after turn boundary");
}

void verifySampling() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    for (bool frozen : {false, true}) {
        for (bool blockFirst : {false, true}) {
            Fixture f(frozen);
            f.powers(blockFirst);
            f.hand(CardId::SLIMED); f.play();
            auto sample = f.battle;
            auto reordered = f.battle;
            if (!frozen) std::reverse(reordered.cards.drawPile.begin(), reordered.cards.drawPile.end());
            public_sampling::resampleCombatContinuation(f.game, sample, seeds);
            public_sampling::resampleCombatContinuation(f.game, reordered, seeds);
            require(sample.cards.hand[0].getUniqueId() == f.battle.cards.hand[0].getUniqueId() &&
                    sample.player.block == 3 && sample.cards.exhaustPile.size() == 1,
                    "sample changed observed exhaust/draw/block");
            for (size_t i = 0; i < sample.cards.drawPile.size(); ++i) {
                require(sample.cards.drawPile[i].getUniqueId() == reordered.cards.drawPile[i].getUniqueId(),
                        "sample depends on hidden order");
                if (frozen) require(sample.cards.drawPile[i].getUniqueId() == f.battle.cards.drawPile[i].getUniqueId(),
                                    "Frozen Eye order changed");
            }
            sample.triggerAndMoveToExhaustPile(CardInstance(CardId::WOUND));
            auto first = sample.actionQueue.popFront();
            first(sample);
            require((sample.player.block > 3) == blockFirst, "sample lost public callback order");
        }
    }
}
}
int main() {
    try {
        verifyPowerVariants();
        verifyOrder();
        verifyBoundsAndNestedDraw();
        verifyHandSelectionMultipleAndShuffle();
        verifySampling();
        std::cout << "EXHAUST_TRIGGER_CARD_RECIPES_OK (2 cards, 4 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "EXHAUST_TRIGGER_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
