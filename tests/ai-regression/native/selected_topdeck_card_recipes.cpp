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

struct HeadbuttFixture {
    GameContext game;
    BattleContext battle;

    explicit HeadbuttFixture(bool frozenEye = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (frozenEye) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "Headbutt fixture is not at player control");
    }
};

void setZones(
    BattleContext &battle,
    std::initializer_list<CardInstance> hand,
    std::initializer_list<CardInstance> draw = {},
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

void playHeadbutt(BattleContext &battle) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, 0, false), "Headbutt is not playable");
    battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
}

void finishSelection(BattleContext &battle, int discardIndex) {
    battle.chooseHeadbuttCard(discardIndex);
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
    require(battle.inputState == InputState::PLAYER_NORMAL,
            "Headbutt selection did not return control");
}

std::vector<CardId> pileIds(const std::vector<CardInstance> &pile) {
    std::vector<CardId> result;
    for (const auto &card : pile) result.push_back(card.id);
    return result;
}

std::vector<int> pileUniqueIds(const std::vector<CardInstance> &pile) {
    std::vector<int> result;
    for (const auto &card : pile) result.push_back(card.getUniqueId());
    return result;
}

void verifyBaseUpgradedAndDamageModifiers() {
    for (const bool upgraded : {false, true}) {
        HeadbuttFixture fixture;
        setZones(fixture.battle, {CardInstance(CardId::HEADBUTT, upgraded)});
        const int hpBefore = fixture.battle.monsters.arr[0].curHp;
        playHeadbutt(fixture.battle);
        require(fixture.battle.inputState == InputState::PLAYER_NORMAL,
                "empty discard pile opened a selection");
        require(
            hpBefore - fixture.battle.monsters.arr[0].curHp == (upgraded ? 12 : 9),
            "Headbutt base/upgraded damage changed"
        );
        require(fixture.battle.cards.drawPile.empty(),
                "empty discard pile changed the draw pile");
    }

    HeadbuttFixture modified;
    setZones(modified.battle, {CardInstance(CardId::HEADBUTT)});
    modified.battle.player.debuff<PS::WEAK>(1, false);
    modified.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(1, false);
    const int hpBefore = modified.battle.monsters.arr[0].curHp;
    playHeadbutt(modified.battle);
    const int modifiedDamage = hpBefore - modified.battle.monsters.arr[0].curHp;
    if (modifiedDamage != 10) {
        throw std::runtime_error(
            "Headbutt Weak/Vulnerable calculation changed: " +
            std::to_string(modifiedDamage)
        );
    }
}

void verifyAutomaticAndSelectedMovement() {
    HeadbuttFixture automatic;
    setZones(
        automatic.battle,
        {CardInstance(CardId::HEADBUTT)},
        {CardInstance(CardId::DEFEND_RED)},
        {CardInstance(CardId::BASH)}
    );
    const int selectedId = automatic.battle.cards.discardPile[0].getUniqueId();
    playHeadbutt(automatic.battle);
    require(automatic.battle.inputState == InputState::PLAYER_NORMAL,
            "single discard card did not auto-resolve");
    require(automatic.battle.cards.drawPile.back().id == CardId::BASH &&
            automatic.battle.cards.drawPile.back().getUniqueId() == selectedId,
            "automatic Headbutt movement lost card identity or top position");

    HeadbuttFixture selected;
    setZones(
        selected.battle,
        {CardInstance(CardId::HEADBUTT)},
        {CardInstance(CardId::DEFEND_RED)},
        {CardInstance(CardId::WOUND), CardInstance(CardId::BASH)}
    );
    const int woundId = selected.battle.cards.discardPile[0].getUniqueId();
    const int bashId = selected.battle.cards.discardPile[1].getUniqueId();
    playHeadbutt(selected.battle);
    require(selected.battle.inputState == InputState::CARD_SELECT &&
            selected.battle.cardSelectInfo.cardSelectTask == CardSelectTask::HEADBUTT,
            "multiple discard cards did not open Headbutt selection");
    finishSelection(selected.battle, 1);
    require(selected.battle.cards.drawPile.back().id == CardId::BASH &&
            selected.battle.cards.drawPile.back().getUniqueId() == bashId,
            "chosen Headbutt card is not the exact draw-pile top instance");
    require(selected.battle.cards.discardPile.size() == 2,
            "Headbutt selection removed the wrong number of discard cards");
    require(std::any_of(
        selected.battle.cards.discardPile.begin(), selected.battle.cards.discardPile.end(),
        [woundId](const CardInstance &card) { return card.getUniqueId() == woundId; }
    ), "Headbutt selection removed an unchosen discard card");

    selected.battle.drawCards(1);
    require(selected.battle.cards.cardsInHand == 1 &&
            selected.battle.cards.hand[0].getUniqueId() == bashId,
            "the next draw did not reveal the selected Headbutt card");
}

void setSelectedFixture(HeadbuttFixture &fixture) {
    setZones(
        fixture.battle,
        {CardInstance(CardId::HEADBUTT)},
        {
            CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
            CardInstance(CardId::ANGER),
        },
        {CardInstance(CardId::WOUND), CardInstance(CardId::BASH)}
    );
    playHeadbutt(fixture.battle);
    finishSelection(fixture.battle, 1);
}

void verifyKnownTopResampling() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    HeadbuttFixture fixture;
    setSelectedFixture(fixture);
    const int knownTop = fixture.battle.cards.drawPile.back().getUniqueId();
    const auto publicCards = pileIds(fixture.battle.cards.drawPile);
    BattleContext canonical(fixture.battle);
    BattleContext reordered(fixture.battle);
    std::reverse(
        reordered.cards.drawPile.begin(), reordered.cards.drawPile.end() - 1
    );
    public_sampling::resampleCombatContinuation(
        fixture.game, canonical, seeds, knownTop
    );
    public_sampling::resampleCombatContinuation(
        fixture.game, reordered, seeds, knownTop
    );
    require(pileUniqueIds(canonical.cards.drawPile) ==
            pileUniqueIds(reordered.cards.drawPile),
            "known-top sample depends on hidden order below the top card");
    require(canonical.cards.drawPile.back().getUniqueId() == knownTop,
            "known Headbutt top moved during resampling");
    auto sampledCards = pileIds(canonical.cards.drawPile);
    std::sort(sampledCards.begin(), sampledCards.end());
    auto sortedPublicCards = publicCards;
    std::sort(sortedPublicCards.begin(), sortedPublicCards.end());
    require(sampledCards == sortedPublicCards,
            "known-top sampling changed public draw-pile membership");

    bool rejected = false;
    try {
        BattleContext invalid(fixture.battle);
        public_sampling::resampleCombatContinuation(
            fixture.game, invalid, seeds, knownTop + 1000
        );
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "stale known-top identity did not fail closed");
}

void verifyFrozenEyePreservesFullOrder() {
    const std::array<std::uint64_t, 7> seeds {101, 202, 303, 404, 505, 606, 707};
    HeadbuttFixture fixture(true);
    setSelectedFixture(fixture);
    const int knownTop = fixture.battle.cards.drawPile.back().getUniqueId();
    const auto visibleOrder = pileUniqueIds(fixture.battle.cards.drawPile);
    public_sampling::resampleCombatContinuation(
        fixture.game, fixture.battle, seeds, knownTop
    );
    require(pileUniqueIds(fixture.battle.cards.drawPile) == visibleOrder,
            "Frozen Eye visible order changed with a known Headbutt top");
}

}  // namespace

int main() {
    try {
        verifyBaseUpgradedAndDamageModifiers();
        verifyAutomaticAndSelectedMovement();
        verifyKnownTopResampling();
        verifyFrozenEyePreservesFullOrder();
        std::cout << "SELECTED_TOPDECK_CARD_RECIPES_OK (1 card, 2 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "SELECTED_TOPDECK_CARD_RECIPES_FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
