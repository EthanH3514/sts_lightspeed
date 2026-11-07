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

struct GeneratedCardFixture {
    GameContext game;
    BattleContext battle;

    explicit GeneratedCardFixture(bool frozenEye = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (frozenEye) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "generated-card fixture is not at player control");
    }
};

void setZones(
    BattleContext &battle,
    std::initializer_list<CardInstance> hand,
    std::initializer_list<CardInstance> draw = {}
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
    battle.cards.nextUniqueCardId = uniqueId;
}

void playFirst(BattleContext &battle, int target = 0) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, target, false),
            "generated-card fixture card is not playable");
    battle.addToBotCard(CardQueueItem(card, target, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
    require(battle.inputState == InputState::PLAYER_NORMAL,
            "generated-card action did not return control");
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

void verifyAngerCopiesPlayedVariant() {
    for (const bool upgraded : {false, true}) {
        GeneratedCardFixture fixture;
        setZones(fixture.battle, {CardInstance(CardId::ANGER, upgraded)});
        const int hpBefore = fixture.battle.monsters.arr[0].curHp;
        playFirst(fixture.battle);
        require(
            hpBefore - fixture.battle.monsters.arr[0].curHp == (upgraded ? 8 : 6),
            "Anger damage changed"
        );
        require(fixture.battle.cards.discardPile.size() == 2,
                "Anger did not leave the played card plus one generated copy");
        const auto &first = fixture.battle.cards.discardPile[0];
        const auto &second = fixture.battle.cards.discardPile[1];
        require(first.id == CardId::ANGER && second.id == CardId::ANGER,
                "Anger generated the wrong card identity");
        require(first.isUpgraded() == upgraded && second.isUpgraded() == upgraded,
                "Anger copy did not preserve the played upgrade state");
        require(first.getUniqueId() != second.getUniqueId(),
                "Anger copy reused the played card instance identity");
    }
}

void verifyWildStrikeCreatesBaseWoundInDrawPile() {
    for (const bool upgraded : {false, true}) {
        GeneratedCardFixture fixture;
        setZones(
            fixture.battle,
            {CardInstance(CardId::WILD_STRIKE, upgraded)},
            {
                CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
                CardInstance(CardId::BASH),
            }
        );
        const int hpBefore = fixture.battle.monsters.arr[0].curHp;
        playFirst(fixture.battle);
        require(
            hpBefore - fixture.battle.monsters.arr[0].curHp == (upgraded ? 17 : 12),
            "Wild Strike damage changed"
        );
        require(fixture.battle.cards.drawPile.size() == 4,
                "Wild Strike did not add exactly one card to the draw pile");
        const auto wound = std::find_if(
            fixture.battle.cards.drawPile.begin(),
            fixture.battle.cards.drawPile.end(),
            [](const CardInstance &card) { return card.id == CardId::WOUND; }
        );
        require(wound != fixture.battle.cards.drawPile.end(),
                "Wild Strike generated no Wound");
        require(!wound->isUpgraded(),
                "upgraded Wild Strike incorrectly generated an upgraded Wound");
        require(fixture.battle.cards.discardPile.size() == 1 &&
                fixture.battle.cards.discardPile[0].id == CardId::WILD_STRIKE,
                "Wild Strike did not enter discard after generating the Wound");
    }
}

void verifyDamageModifiersDoNotChangeGeneration() {
    GeneratedCardFixture anger;
    setZones(anger.battle, {CardInstance(CardId::ANGER)});
    anger.battle.player.debuff<PS::WEAK>(1, false);
    anger.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(1, false);
    const int angerHp = anger.battle.monsters.arr[0].curHp;
    playFirst(anger.battle);
    require(angerHp - anger.battle.monsters.arr[0].curHp == 6,
            "Anger Weak/Vulnerable damage changed");
    require(anger.battle.cards.discardPile.size() == 2,
            "damage modifiers changed Anger copy generation");

    GeneratedCardFixture wild;
    setZones(wild.battle, {CardInstance(CardId::WILD_STRIKE)});
    wild.battle.player.debuff<PS::WEAK>(1, false);
    wild.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(1, false);
    const int wildHp = wild.battle.monsters.arr[0].curHp;
    playFirst(wild.battle);
    require(wildHp - wild.battle.monsters.arr[0].curHp == 13,
            "Wild Strike Weak/Vulnerable damage changed");
    require(wild.battle.cards.drawPile.size() == 1 &&
            wild.battle.cards.drawPile[0].id == CardId::WOUND,
            "empty draw pile did not accept the generated Wound");
}

void verifyUnknownOrderSamplingConditionsOnlyOnMultiset() {
    const std::array<std::uint64_t, 7> beforeSeeds {11, 22, 33, 44, 55, 66, 77};
    const std::array<std::uint64_t, 7> afterSeeds {101, 202, 303, 404, 505, 606, 707};
    GeneratedCardFixture first;
    GeneratedCardFixture reversed;
    setZones(
        first.battle,
        {CardInstance(CardId::WILD_STRIKE)},
        {
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::BASH),
        }
    );
    setZones(
        reversed.battle,
        {CardInstance(CardId::WILD_STRIKE)},
        {
            CardInstance(CardId::BASH), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::STRIKE_RED),
        }
    );
    public_sampling::resampleCombatContinuation(first.game, first.battle, beforeSeeds);
    public_sampling::resampleCombatContinuation(
        reversed.game, reversed.battle, beforeSeeds
    );
    require(pileIds(first.battle.cards.drawPile) ==
            pileIds(reversed.battle.cards.drawPile),
            "pre-insertion sample depends on hidden source order");
    playFirst(first.battle);
    playFirst(reversed.battle);
    require(pileIds(first.battle.cards.drawPile) ==
            pileIds(reversed.battle.cards.drawPile),
            "equal public worlds inserted Wound at different sampled positions");
    const auto publicMultiset = sortedPileIds(first.battle.cards.drawPile);

    BattleContext reordered(first.battle);
    std::reverse(reordered.cards.drawPile.begin(), reordered.cards.drawPile.end());
    BattleContext canonical(first.battle);
    public_sampling::resampleCombatContinuation(first.game, canonical, afterSeeds);
    public_sampling::resampleCombatContinuation(first.game, reordered, afterSeeds);
    require(pileIds(canonical.cards.drawPile) == pileIds(reordered.cards.drawPile),
            "post-insertion sample retained the hidden generated-card position");
    require(sortedPileIds(canonical.cards.drawPile) == publicMultiset,
            "post-insertion sampling changed the public draw-pile multiset");
}

void verifyFrozenEyePreservesVisibleInsertionOrder() {
    const std::array<std::uint64_t, 7> seeds {101, 202, 303, 404, 505, 606, 707};
    GeneratedCardFixture fixture(true);
    setZones(
        fixture.battle,
        {CardInstance(CardId::WILD_STRIKE)},
        {
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::BASH),
        }
    );
    playFirst(fixture.battle);
    const auto visibleOrder = pileIds(fixture.battle.cards.drawPile);
    BattleContext after(fixture.battle);
    public_sampling::resampleCombatContinuation(fixture.game, after, seeds);
    require(pileIds(after.cards.drawPile) == visibleOrder,
            "Frozen Eye visible post-insertion order changed during sampling");
}

}  // namespace

int main() {
    try {
        verifyAngerCopiesPlayedVariant();
        verifyWildStrikeCreatesBaseWoundInDrawPile();
        verifyDamageModifiersDoNotChangeGeneration();
        verifyUnknownOrderSamplingConditionsOnlyOnMultiset();
        verifyFrozenEyePreservesVisibleInsertionOrder();
        std::cout << "GENERATED_CARD_INSERTION_RECIPES_OK (2 cards, 4 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "GENERATED_CARD_INSERTION_RECIPES_FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
