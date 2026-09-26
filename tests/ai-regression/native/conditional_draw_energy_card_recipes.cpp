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

struct DropkickFixture {
    GameContext game;
    BattleContext battle;

    DropkickFixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "Dropkick fixture is not at player control");
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

void playFirst(BattleContext &battle) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, 0, false), "Dropkick is not playable");
    battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
    require(
        battle.inputState == InputState::PLAYER_NORMAL ||
        battle.outcome != Outcome::UNDECIDED,
        "Dropkick action did not resolve"
    );
}

std::vector<CardId> handIds(const BattleContext &battle) {
    std::vector<CardId> result;
    for (int idx = 0; idx < battle.cards.cardsInHand; ++idx) {
        result.push_back(battle.cards.hand[idx].id);
    }
    return result;
}

void verifyBaseAndUpgradedFalseBranch() {
    for (const bool upgraded : {false, true}) {
        DropkickFixture fixture;
        setZones(
            fixture.battle,
            {CardInstance(CardId::DROPKICK, upgraded)},
            {CardInstance(CardId::STRIKE_RED)}
        );
        const int hpBefore = fixture.battle.monsters.arr[0].curHp;
        const int energyBefore = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(
            hpBefore - fixture.battle.monsters.arr[0].curHp == (upgraded ? 8 : 5),
            "Dropkick base/upgraded damage changed"
        );
        require(fixture.battle.player.energy == energyBefore - 1,
                "non-Vulnerable Dropkick incorrectly refunded energy");
        require(fixture.battle.cards.cardsInHand == 0 &&
                fixture.battle.cards.drawPile.size() == 1,
                "non-Vulnerable Dropkick incorrectly drew a card");
    }
}

void verifyBaseAndUpgradedTrueBranch() {
    for (const bool upgraded : {false, true}) {
        DropkickFixture fixture;
        setZones(
            fixture.battle,
            {CardInstance(CardId::DROPKICK, upgraded)},
            {CardInstance(CardId::STRIKE_RED)}
        );
        fixture.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
        const int hpBefore = fixture.battle.monsters.arr[0].curHp;
        const int energyBefore = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(
            hpBefore - fixture.battle.monsters.arr[0].curHp == (upgraded ? 12 : 7),
            "Vulnerable Dropkick damage changed"
        );
        require(fixture.battle.player.energy == energyBefore,
                "Vulnerable Dropkick did not refund one energy");
        require(handIds(fixture.battle) == std::vector<CardId>{CardId::STRIKE_RED},
                "Vulnerable Dropkick did not draw one card");
    }
}

void verifyWeakVulnerableAndNoDraw() {
    DropkickFixture modifiers;
    setZones(
        modifiers.battle,
        {CardInstance(CardId::DROPKICK)},
        {CardInstance(CardId::DEFEND_RED)}
    );
    modifiers.battle.player.debuff<PS::WEAK>(1, false);
    modifiers.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    const int hpBefore = modifiers.battle.monsters.arr[0].curHp;
    playFirst(modifiers.battle);
    require(hpBefore - modifiers.battle.monsters.arr[0].curHp == 5,
            "Dropkick Weak/Vulnerable calculation changed");

    DropkickFixture noDraw;
    setZones(
        noDraw.battle,
        {CardInstance(CardId::DROPKICK)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    noDraw.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    noDraw.battle.player.buff<PS::NO_DRAW>(1);
    const int energyBefore = noDraw.battle.player.energy;
    playFirst(noDraw.battle);
    require(noDraw.battle.player.energy == energyBefore,
            "No Draw incorrectly suppressed Dropkick energy refund");
    require(noDraw.battle.cards.cardsInHand == 0 &&
            noDraw.battle.cards.drawPile.size() == 1,
            "No Draw did not preserve the undrawn Dropkick card");
}

void verifyEmptySupplyShuffleAndLethalResolution() {
    DropkickFixture empty;
    setZones(empty.battle, {CardInstance(CardId::DROPKICK)}, {});
    empty.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    const int emptyEnergy = empty.battle.player.energy;
    playFirst(empty.battle);
    require(empty.battle.player.energy == emptyEnergy,
            "empty supply suppressed Dropkick energy refund");
    require(empty.battle.cards.cardsInHand == 0,
            "empty supply created a Dropkick draw");

    DropkickFixture shuffle;
    setZones(
        shuffle.battle,
        {CardInstance(CardId::DROPKICK)},
        {},
        {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED)}
    );
    shuffle.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    playFirst(shuffle.battle);
    require(shuffle.battle.cards.cardsInHand == 1,
            "Dropkick did not draw after shuffling discard");
    require(shuffle.battle.cards.drawPile.size() == 1,
            "Dropkick shuffle did not preserve the undrawn card");
    require(shuffle.battle.cards.discardPile.size() == 1 &&
            shuffle.battle.cards.discardPile[0].id == CardId::DROPKICK,
            "played Dropkick entered discard before its queued draw");

    DropkickFixture lethal;
    setZones(
        lethal.battle,
        {CardInstance(CardId::DROPKICK)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    lethal.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    lethal.battle.monsters.arr[0].curHp = 1;
    const int lethalEnergy = lethal.battle.player.energy;
    playFirst(lethal.battle);
    require(lethal.battle.outcome == Outcome::PLAYER_VICTORY,
            "lethal Dropkick did not end combat");
    // The base game's DropkickAction also queues damage above energy and draw;
    // post-combat cleanup removes GainEnergyAction and DrawCardAction. The native
    // simulator therefore intentionally leaves both irrelevant terminal effects
    // unresolved after the final monster dies.
    require(lethal.battle.player.energy == lethalEnergy - 1,
            "lethal Dropkick unexpectedly resolved post-combat energy");
    require(lethal.battle.cards.cardsInHand == 0 &&
            lethal.battle.cards.drawPile.size() == 1,
            "lethal Dropkick unexpectedly resolved a post-combat draw");
}

void verifyPublicConditionSurvivesResampling() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    DropkickFixture first;
    DropkickFixture reversed;
    setZones(
        first.battle,
        {CardInstance(CardId::DROPKICK)},
        {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED)}
    );
    setZones(
        reversed.battle,
        {CardInstance(CardId::DROPKICK)},
        {CardInstance(CardId::DEFEND_RED), CardInstance(CardId::STRIKE_RED)}
    );
    first.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    reversed.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    public_sampling::resampleCombatContinuation(first.game, first.battle, seeds);
    public_sampling::resampleCombatContinuation(reversed.game, reversed.battle, seeds);
    require(first.battle.monsters.arr[0].hasStatus<MS::VULNERABLE>() &&
            reversed.battle.monsters.arr[0].hasStatus<MS::VULNERABLE>(),
            "resampling changed the public Vulnerable predicate");
    playFirst(first.battle);
    playFirst(reversed.battle);
    require(handIds(first.battle) == handIds(reversed.battle),
            "equal public Dropkick samples produced different observed draws");
    require(first.battle.player.energy == reversed.battle.player.energy,
            "equal public Dropkick samples produced different energy");

    DropkickFixture falseBranch;
    setZones(
        falseBranch.battle,
        {CardInstance(CardId::DROPKICK)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    public_sampling::resampleCombatContinuation(
        falseBranch.game, falseBranch.battle, seeds
    );
    const int energyBefore = falseBranch.battle.player.energy;
    playFirst(falseBranch.battle);
    require(falseBranch.battle.player.energy == energyBefore - 1 &&
            falseBranch.battle.cards.cardsInHand == 0,
            "resampling changed the false Dropkick branch");
}

}  // namespace

int main() {
    try {
        verifyBaseAndUpgradedFalseBranch();
        verifyBaseAndUpgradedTrueBranch();
        verifyWeakVulnerableAndNoDraw();
        verifyEmptySupplyShuffleAndLethalResolution();
        verifyPublicConditionSurvivesResampling();
        std::cout << "CONDITIONAL_DRAW_ENERGY_CARD_RECIPES_OK (1 card, 2 variants, 2 branches)\n";
    } catch (const std::exception &error) {
        std::cerr << "CONDITIONAL_DRAW_ENERGY_CARD_RECIPES_FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
