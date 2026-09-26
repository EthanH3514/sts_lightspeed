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

struct DrawFixture {
    GameContext game;
    BattleContext battle;

    explicit DrawFixture(bool frozenEye = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (frozenEye) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "draw fixture is not at player control");
    }
};

void setZones(
    BattleContext &battle,
    std::initializer_list<CardInstance> hand,
    std::initializer_list<CardInstance> draw,
    std::initializer_list<CardInstance> discard = {}
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
    battle.cards.nextUniqueCardId = uniqueId;
}

void playFirst(BattleContext &battle, int target = 0) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, target, false), "draw fixture card is not playable");
    battle.addToBotCard(CardQueueItem(card, target, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
    require(battle.inputState == InputState::PLAYER_NORMAL,
            "draw-card action did not return control");
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

std::vector<CardId> sortedPileIds(const std::vector<CardInstance> &pile) {
    auto result = pileIds(pile);
    std::sort(result.begin(), result.end());
    return result;
}

void verifyBaseAndUpgradedRecipes() {
    for (const bool upgraded : {false, true}) {
        DrawFixture pommel;
        setZones(
            pommel.battle,
            {CardInstance(CardId::POMMEL_STRIKE, upgraded)},
            {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED)}
        );
        const int hpBefore = pommel.battle.monsters.arr[0].curHp;
        playFirst(pommel.battle);
        require(
            hpBefore - pommel.battle.monsters.arr[0].curHp == (upgraded ? 10 : 9),
            "Pommel Strike damage changed"
        );
        require(
            pommel.battle.cards.cardsInHand == (upgraded ? 2 : 1),
            "Pommel Strike draw amount changed"
        );
        require(pommel.battle.cards.discardPile.size() == 1,
                "Pommel Strike did not enter discard after drawing");

        DrawFixture shrug;
        setZones(
            shrug.battle,
            {CardInstance(CardId::SHRUG_IT_OFF, upgraded)},
            {CardInstance(CardId::STRIKE_RED)}
        );
        playFirst(shrug.battle);
        require(shrug.battle.player.block == (upgraded ? 11 : 8),
                "Shrug It Off block changed");
        require(shrug.battle.cards.cardsInHand == 1,
                "Shrug It Off draw amount changed");
        require(shrug.battle.cards.discardPile.size() == 1,
                "Shrug It Off did not enter discard after drawing");
    }
}

void verifyPublicPowerModifiers() {
    DrawFixture pommel;
    setZones(
        pommel.battle,
        {CardInstance(CardId::POMMEL_STRIKE)},
        {CardInstance(CardId::DEFEND_RED)}
    );
    pommel.battle.player.debuff<PS::WEAK>(1, false);
    pommel.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(1, false);
    const int hpBefore = pommel.battle.monsters.arr[0].curHp;
    playFirst(pommel.battle);
    require(hpBefore - pommel.battle.monsters.arr[0].curHp == 10,
            "Pommel Strike Weak/Vulnerable calculation changed");

    DrawFixture shrug;
    setZones(
        shrug.battle,
        {CardInstance(CardId::SHRUG_IT_OFF)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    shrug.battle.player.dexterity = 2;
    shrug.battle.player.debuff<PS::FRAIL>(1, false);
    playFirst(shrug.battle);
    require(shrug.battle.player.block == 7,
            "Shrug It Off Dexterity/Frail calculation changed");
}

void verifyShuffleAndFiniteSupply() {
    DrawFixture shuffle;
    setZones(
        shuffle.battle,
        {CardInstance(CardId::POMMEL_STRIKE, true)},
        {CardInstance(CardId::DEFEND_RED)},
        {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::BASH)}
    );
    playFirst(shuffle.battle);
    require(shuffle.battle.cards.cardsInHand == 2,
            "upgraded Pommel Strike did not continue drawing after a shuffle");
    require(shuffle.battle.cards.discardPile.size() == 1 &&
            shuffle.battle.cards.discardPile[0].id == CardId::POMMEL_STRIKE,
            "pre-draw discard pile or played-card ordering changed");
    require(shuffle.battle.cards.drawPile.size() == 1,
            "shuffle draw did not preserve the undrawn card");

    DrawFixture empty;
    setZones(empty.battle, {CardInstance(CardId::SHRUG_IT_OFF)}, {});
    playFirst(empty.battle);
    require(empty.battle.cards.cardsInHand == 0,
            "empty piles produced a card during draw");
    require(empty.battle.player.block == 8,
            "empty draw piles suppressed Shrug It Off block");

    DrawFixture noDraw;
    setZones(
        noDraw.battle,
        {CardInstance(CardId::SHRUG_IT_OFF)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    noDraw.battle.player.buff<PS::NO_DRAW>(1);
    playFirst(noDraw.battle);
    require(noDraw.battle.cards.cardsInHand == 0 &&
            noDraw.battle.cards.drawPile.size() == 1,
            "No Draw did not preserve the undrawn card");
    require(noDraw.battle.player.block == 8,
            "No Draw suppressed Shrug It Off block");
}

void verifyHandLimitAfterPlayedCardLeaves() {
    DrawFixture fixture;
    setZones(
        fixture.battle,
        {
            CardInstance(CardId::POMMEL_STRIKE, true),
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::STRIKE_RED),
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::STRIKE_RED),
            CardInstance(CardId::DEFEND_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::DEFEND_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::BASH),
        },
        {CardInstance(CardId::DEFEND_RED), CardInstance(CardId::BASH)}
    );
    playFirst(fixture.battle);
    require(fixture.battle.cards.cardsInHand == 10,
            "draw did not use the hand slot freed by the played card");
    require(fixture.battle.cards.drawPile.size() == 1,
            "draw exceeded the ten-card hand limit");
    require(fixture.battle.cards.drawPile[0].id == CardId::DEFEND_RED,
            "hand-limit draw changed the remaining top-order semantics");
}

void verifyResamplingBeforeAndAfterObservedDraw() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    DrawFixture first;
    DrawFixture reversed;
    setZones(
        first.battle,
        {CardInstance(CardId::POMMEL_STRIKE)},
        {
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::BASH),
        }
    );
    setZones(
        reversed.battle,
        {CardInstance(CardId::POMMEL_STRIKE)},
        {
            CardInstance(CardId::BASH), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::STRIKE_RED),
        }
    );
    public_sampling::resampleCombatContinuation(first.game, first.battle, seeds);
    public_sampling::resampleCombatContinuation(reversed.game, reversed.battle, seeds);
    require(pileIds(first.battle.cards.drawPile) == pileIds(reversed.battle.cards.drawPile),
            "draw-card sample depends on the source hidden pile order");
    playFirst(first.battle);
    playFirst(reversed.battle);
    require(handIds(first.battle) == handIds(reversed.battle),
            "equal public samples produced different observed draws");

    const auto observedHand = handIds(first.battle);
    const auto remainingCards = sortedPileIds(first.battle.cards.drawPile);
    BattleContext after(first.battle);
    public_sampling::resampleCombatContinuation(first.game, after, seeds);
    require(handIds(after) == observedHand,
            "post-draw sampling changed the already observed card");
    require(sortedPileIds(after.cards.drawPile) == remainingCards,
            "post-draw sampling changed the remaining public multiset");

    DrawFixture frozen(true);
    setZones(
        frozen.battle,
        {CardInstance(CardId::POMMEL_STRIKE)},
        {
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::BASH),
        }
    );
    const auto visibleOrder = pileIds(frozen.battle.cards.drawPile);
    public_sampling::resampleCombatContinuation(frozen.game, frozen.battle, seeds);
    require(pileIds(frozen.battle.cards.drawPile) == visibleOrder,
            "Frozen Eye order changed before a draw-card action");
    playFirst(frozen.battle);
    require(handIds(frozen.battle) == std::vector<CardId>{CardId::BASH},
            "Frozen Eye did not draw the publicly visible top card");
    const auto visibleRemaining = pileIds(frozen.battle.cards.drawPile);
    BattleContext frozenAfter(frozen.battle);
    public_sampling::resampleCombatContinuation(frozen.game, frozenAfter, seeds);
    require(pileIds(frozenAfter.cards.drawPile) == visibleRemaining,
            "Frozen Eye remaining order changed after an observed draw");
}

}  // namespace

int main() {
    try {
        verifyBaseAndUpgradedRecipes();
        verifyPublicPowerModifiers();
        verifyShuffleAndFiniteSupply();
        verifyHandLimitAfterPlayedCardLeaves();
        verifyResamplingBeforeAndAfterObservedDraw();
        std::cout << "DETERMINISTIC_DRAW_CARD_RECIPES_OK (2 cards, 4 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "DETERMINISTIC_DRAW_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
