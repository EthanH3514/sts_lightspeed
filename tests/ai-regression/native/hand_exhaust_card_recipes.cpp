#include <algorithm>
#include <array>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"

using namespace sts;

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct ExhaustFixture {
    GameContext game;
    BattleContext battle;

    ExhaustFixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "exhaust fixture is not at player control");
    }
};

void setZones(
    BattleContext &battle,
    std::initializer_list<CardInstance> hand,
    std::initializer_list<CardInstance> draw = {},
    std::initializer_list<CardInstance> discard = {},
    std::initializer_list<CardInstance> exhaust = {}
) {
    battle.cards = CardManager{};
    int uniqueId = 100;
    for (auto card : hand) {
        card.setUniqueId(uniqueId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
    }
    for (auto card : draw) {
        card.setUniqueId(uniqueId++);
        battle.cards.drawPile.push_back(card);
    }
    for (auto card : discard) {
        card.setUniqueId(uniqueId++);
        battle.cards.discardPile.push_back(card);
    }
    for (auto card : exhaust) {
        card.setUniqueId(uniqueId++);
        battle.cards.exhaustPile.push_back(card);
    }
    battle.cards.nextUniqueCardId = uniqueId;
}

void playFirst(BattleContext &battle) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, 0, false), "exhaust fixture card is not playable");
    battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
}

void chooseExhaust(BattleContext &battle, int handIndex) {
    require(battle.inputState == InputState::CARD_SELECT,
            "exhaust choice was not requested");
    require(battle.cardSelectInfo.cardSelectTask == CardSelectTask::EXHAUST_ONE,
            "wrong card-selection task for chosen exhaust");
    battle.chooseExhaustOneCard(handIndex);
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
}

std::vector<CardId> handIds(const BattleContext &battle) {
    std::vector<CardId> result;
    for (int idx = 0; idx < battle.cards.cardsInHand; ++idx) {
        result.push_back(battle.cards.hand[idx].id);
    }
    return result;
}

std::vector<CardId> pileIds(const std::vector<CardInstance> &pile) {
    std::vector<CardId> result;
    for (const auto &card : pile) result.push_back(card.id);
    return result;
}

void verifyTrueGritVariantsAndModifiers() {
    ExhaustFixture base;
    setZones(base.battle, {
        CardInstance(CardId::TRUE_GRIT), CardInstance(CardId::STRIKE_RED),
        CardInstance(CardId::DEFEND_RED), CardInstance(CardId::WOUND),
    });
    base.battle.player.dexterity = 2;
    base.battle.player.debuff<PS::FRAIL>(1, false);
    playFirst(base.battle);
    require(base.battle.inputState == InputState::PLAYER_NORMAL,
            "base True Grit unexpectedly requested a choice");
    require(base.battle.player.block == 6,
            "base True Grit Dexterity/Frail block changed");
    require(base.battle.cards.cardsInHand == 2,
            "base True Grit did not randomly exhaust exactly one remaining card");
    require(base.battle.cards.exhaustPile.size() == 1,
            "base True Grit random exhaust did not enter exhaust pile");
    require(base.battle.cards.exhaustPile[0].id != CardId::TRUE_GRIT,
            "base True Grit exhausted the already played card");
    require(base.battle.cards.discardPile.size() == 1 &&
            base.battle.cards.discardPile[0].id == CardId::TRUE_GRIT,
            "base True Grit did not enter discard");

    ExhaustFixture upgraded;
    setZones(upgraded.battle, {
        CardInstance(CardId::TRUE_GRIT, true), CardInstance(CardId::STRIKE_RED),
        CardInstance(CardId::WOUND),
    });
    playFirst(upgraded.battle);
    require(upgraded.battle.inputState == InputState::CARD_SELECT,
            "upgraded True Grit did not request a selection");
    require(upgraded.battle.cardSelectInfo.cardSelectTask == CardSelectTask::EXHAUST_ONE,
            "upgraded True Grit exposed the wrong selection task");
    require(upgraded.battle.player.block == 9,
            "upgraded True Grit block changed");
    chooseExhaust(upgraded.battle, 1);
    require(upgraded.battle.inputState == InputState::PLAYER_NORMAL,
            "upgraded True Grit did not resume player control");
    require(upgraded.battle.cards.exhaustPile.size() == 1 &&
            upgraded.battle.cards.exhaustPile[0].id == CardId::WOUND,
            "upgraded True Grit exhausted the wrong selected card");
    require(handIds(upgraded.battle) == std::vector<CardId>{CardId::STRIKE_RED},
            "upgraded True Grit changed an unselected hand card");
}

void verifyBurningPactSelectionThenDraw() {
    for (const bool upgraded : {false, true}) {
        ExhaustFixture fixture;
        setZones(
            fixture.battle,
            {
                CardInstance(CardId::BURNING_PACT, upgraded),
                CardInstance(CardId::STRIKE_RED), CardInstance(CardId::WOUND),
            },
            {
                CardInstance(CardId::DEFEND_RED), CardInstance(CardId::BASH),
                CardInstance(CardId::CLEAVE),
            }
        );
        const auto drawBefore = pileIds(fixture.battle.cards.drawPile);
        playFirst(fixture.battle);
        require(fixture.battle.inputState == InputState::CARD_SELECT,
                "Burning Pact did not request an exhaust selection");
        require(pileIds(fixture.battle.cards.drawPile) == drawBefore,
                "Burning Pact drew before its exhaust selection resolved");
        chooseExhaust(fixture.battle, 1);
        require(fixture.battle.inputState == InputState::PLAYER_NORMAL,
                "Burning Pact did not resume player control");
        require(fixture.battle.cards.exhaustPile.size() == 1 &&
                fixture.battle.cards.exhaustPile[0].id == CardId::WOUND,
                "Burning Pact exhausted the wrong selected card");
        require(
            fixture.battle.cards.cardsInHand == (upgraded ? 4 : 3),
            "Burning Pact draw amount changed"
        );
        require(
            fixture.battle.cards.drawPile.size() == (upgraded ? 0 : 1),
            "Burning Pact did not draw after the selected exhaust"
        );
        require(fixture.battle.cards.discardPile.size() == 1 &&
                fixture.battle.cards.discardPile[0].id == CardId::BURNING_PACT,
                "Burning Pact did not enter discard");
    }
}

void verifyAutomaticSelectionEdges() {
    ExhaustFixture one;
    setZones(
        one.battle,
        {CardInstance(CardId::BURNING_PACT), CardInstance(CardId::WOUND)},
        {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED)}
    );
    playFirst(one.battle);
    require(one.battle.inputState == InputState::PLAYER_NORMAL,
            "single-card Burning Pact should auto-resolve its exhaust");
    require(one.battle.cards.exhaustPile.size() == 1 &&
            one.battle.cards.exhaustPile[0].id == CardId::WOUND,
            "single-card Burning Pact did not auto-exhaust the only card");
    require(one.battle.cards.cardsInHand == 2,
            "single-card Burning Pact did not draw after auto-exhaust");

    ExhaustFixture empty;
    setZones(
        empty.battle,
        {CardInstance(CardId::BURNING_PACT, true)},
        {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED)}
    );
    playFirst(empty.battle);
    require(empty.battle.inputState == InputState::PLAYER_NORMAL,
            "empty-hand Burning Pact should skip selection");
    require(empty.battle.cards.exhaustPile.empty(),
            "empty-hand Burning Pact invented an exhausted card");
    require(empty.battle.cards.cardsInHand == 2,
            "empty-hand upgraded Burning Pact did not draw finite supply");
}

void verifyObservedExhaustSurvivesResampling() {
    const std::array<std::uint64_t, 7> firstSeeds {11, 22, 33, 44, 55, 66, 77};
    const std::array<std::uint64_t, 7> laterSeeds {101, 202, 303, 404, 505, 606, 707};
    ExhaustFixture fixture;
    setZones(fixture.battle, {
        CardInstance(CardId::TRUE_GRIT), CardInstance(CardId::STRIKE_RED),
        CardInstance(CardId::DEFEND_RED), CardInstance(CardId::WOUND),
    });
    public_sampling::resampleCombatContinuation(fixture.game, fixture.battle, firstSeeds);
    playFirst(fixture.battle);
    const auto observedHand = handIds(fixture.battle);
    const auto observedExhaust = pileIds(fixture.battle.cards.exhaustPile);
    BattleContext after(fixture.battle);
    public_sampling::resampleCombatContinuation(fixture.game, after, laterSeeds);
    require(handIds(after) == observedHand,
            "post-random-exhaust sampling changed the observed hand");
    require(pileIds(after.cards.exhaustPile) == observedExhaust,
            "post-random-exhaust sampling changed the observed exhaust pile");

    ExhaustFixture chosen;
    setZones(chosen.battle, {
        CardInstance(CardId::TRUE_GRIT, true), CardInstance(CardId::STRIKE_RED),
        CardInstance(CardId::WOUND),
    });
    playFirst(chosen.battle);
    chooseExhaust(chosen.battle, 1);
    const auto chosenHand = handIds(chosen.battle);
    const auto chosenExhaust = pileIds(chosen.battle.cards.exhaustPile);
    BattleContext chosenAfter(chosen.battle);
    public_sampling::resampleCombatContinuation(chosen.game, chosenAfter, laterSeeds);
    require(handIds(chosenAfter) == chosenHand,
            "post-selected-exhaust sampling changed the observed hand");
    require(pileIds(chosenAfter.cards.exhaustPile) == chosenExhaust,
            "post-selected-exhaust sampling changed the observed exhaust pile");
}

void verifyEmptyMultiExhaustDoesNotDeadlock() {
    ExhaustFixture fixture;
    setZones(
        fixture.battle,
        {CardInstance(CardId::PURITY)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    playFirst(fixture.battle);
    require(fixture.battle.inputState == InputState::PLAYER_NORMAL,
            "empty-hand ExhaustMany opened an impossible card selection");
    require(fixture.battle.cards.cardsInHand == 0,
            "empty-hand Purity invented a selectable card");
    require(fixture.battle.cards.exhaustPile.size() == 1 &&
            fixture.battle.cards.exhaustPile[0].id == CardId::PURITY,
            "Purity did not exhaust itself after skipping an empty selection");
}

}  // namespace

int main() {
    try {
        verifyTrueGritVariantsAndModifiers();
        verifyBurningPactSelectionThenDraw();
        verifyAutomaticSelectionEdges();
        verifyObservedExhaustSurvivesResampling();
        verifyEmptyMultiExhaustDoesNotDeadlock();
        std::cout << "HAND_EXHAUST_CARD_RECIPES_OK (2 cards, 4 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "HAND_EXHAUST_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
