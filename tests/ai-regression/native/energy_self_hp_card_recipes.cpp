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

struct Fixture {
    GameContext game;
    BattleContext battle;

    Fixture(std::initializer_list<RelicId> relics = {})
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        for (const auto relic : relics) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "energy/self-HP fixture is not at player control");
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

void playFirst(BattleContext &battle) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, 0, false), "energy/self-HP card is not playable");
    battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
    require(
        battle.inputState == InputState::PLAYER_NORMAL ||
        battle.outcome != Outcome::UNDECIDED,
        "energy/self-HP card did not resolve"
    );
}

void verifySeeingRedVariants() {
    for (const bool upgraded : {false, true}) {
        Fixture fixture;
        setZones(fixture.battle, {CardInstance(CardId::SEEING_RED, upgraded)});
        const int before = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(fixture.battle.player.energy == before + (upgraded ? 2 : 1),
                "Seeing Red base/upgraded net energy changed");
        require(fixture.battle.cards.cardsInHand == 0 &&
                fixture.battle.cards.exhaustPile.size() == 1 &&
                fixture.battle.cards.discardPile.empty(),
                "Seeing Red did not exhaust after use");
    }
}

void verifyBloodlettingVariants() {
    for (const bool upgraded : {false, true}) {
        Fixture fixture;
        setZones(fixture.battle, {CardInstance(CardId::BLOODLETTING, upgraded)});
        const int hpBefore = fixture.battle.player.curHp;
        const int energyBefore = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(fixture.battle.player.curHp == hpBefore - 3,
                "Bloodletting self-HP loss changed");
        require(fixture.battle.player.energy == energyBefore + (upgraded ? 3 : 2),
                "Bloodletting base/upgraded energy changed");
        require(fixture.battle.cards.discardPile.size() == 1 &&
                fixture.battle.cards.exhaustPile.empty(),
                "Bloodletting moved to the wrong pile");
    }
}

void verifyOfferingVariantsAndDrawLimits() {
    for (const bool upgraded : {false, true}) {
        Fixture fixture;
        setZones(
            fixture.battle,
            {CardInstance(CardId::OFFERING, upgraded)},
            {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED),
             CardInstance(CardId::BASH), CardInstance(CardId::ANGER),
             CardInstance(CardId::CLEAVE)}
        );
        const int hpBefore = fixture.battle.player.curHp;
        const int energyBefore = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(fixture.battle.player.curHp == hpBefore - 6,
                "Offering self-HP loss changed");
        require(fixture.battle.player.energy == energyBefore + 2,
                "Offering energy changed");
        require(fixture.battle.cards.cardsInHand == (upgraded ? 5 : 3),
                "Offering base/upgraded draw changed");
        require(fixture.battle.cards.exhaustPile.size() == 1,
                "Offering did not exhaust");
    }

    Fixture noDraw;
    setZones(
        noDraw.battle,
        {CardInstance(CardId::OFFERING)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    noDraw.battle.player.buff<PS::NO_DRAW>(1);
    const int hpBefore = noDraw.battle.player.curHp;
    const int energyBefore = noDraw.battle.player.energy;
    playFirst(noDraw.battle);
    require(noDraw.battle.player.curHp == hpBefore - 6 &&
            noDraw.battle.player.energy == energyBefore + 2,
            "No Draw incorrectly suppressed Offering HP/energy effects");
    require(noDraw.battle.cards.cardsInHand == 0 &&
            noDraw.battle.cards.drawPile.size() == 1,
            "No Draw did not preserve Offering draw supply");
}

void verifySelfHpModifiersAndRupture() {
    Fixture buffer;
    setZones(buffer.battle, {CardInstance(CardId::OFFERING)});
    buffer.battle.player.buff<PS::BUFFER>(1);
    buffer.battle.player.buff<PS::RUPTURE>(2);
    const int bufferHp = buffer.battle.player.curHp;
    const int bufferStrength = buffer.battle.player.getStatus<PS::STRENGTH>();
    playFirst(buffer.battle);
    require(buffer.battle.player.curHp == bufferHp &&
            !buffer.battle.player.hasStatus<PS::BUFFER>(),
            "Buffer did not intercept and consume on Offering");
    require(buffer.battle.player.getStatus<PS::STRENGTH>() == bufferStrength,
            "prevented HP loss incorrectly triggered Rupture");

    Fixture blocked;
    setZones(blocked.battle, {CardInstance(CardId::BLOODLETTING)});
    blocked.battle.player.block = 20;
    const int blockedHp = blocked.battle.player.curHp;
    playFirst(blocked.battle);
    require(blocked.battle.player.curHp == blockedHp - 3 &&
            blocked.battle.player.block == 20,
            "ordinary Block incorrectly absorbed Bloodletting HP loss");

    Fixture intangible;
    setZones(intangible.battle, {CardInstance(CardId::OFFERING)});
    intangible.battle.player.buff<PS::INTANGIBLE>(1);
    const int intangibleHp = intangible.battle.player.curHp;
    playFirst(intangible.battle);
    require(intangible.battle.player.curHp == intangibleHp - 1,
            "Intangible did not reduce Offering HP loss to one");

    Fixture rod({RelicId::TUNGSTEN_ROD});
    setZones(rod.battle, {CardInstance(CardId::BLOODLETTING)});
    const int rodHp = rod.battle.player.curHp;
    playFirst(rod.battle);
    require(rod.battle.player.curHp == rodHp - 2,
            "Tungsten Rod did not reduce Bloodletting HP loss");

    Fixture prevented({RelicId::TUNGSTEN_ROD});
    setZones(
        prevented.battle,
        {CardInstance(CardId::OFFERING)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    prevented.battle.player.buff<PS::INTANGIBLE>(1);
    prevented.battle.player.curHp = 1;
    playFirst(prevented.battle);
    require(prevented.battle.outcome == Outcome::UNDECIDED &&
            prevented.battle.player.curHp == 1,
            "Intangible plus Tungsten Rod did not prevent Offering HP loss");

    Fixture rupture;
    setZones(rupture.battle, {CardInstance(CardId::BLOODLETTING)});
    rupture.battle.player.buff<PS::RUPTURE>(2);
    const int strengthBefore = rupture.battle.player.getStatus<PS::STRENGTH>();
    playFirst(rupture.battle);
    require(rupture.battle.player.getStatus<PS::STRENGTH>() == strengthBefore + 2,
            "Bloodletting did not trigger Rupture after real HP loss");
}

void verifyLethalHpLossInterruptsLaterEffects() {
    Fixture bloodletting;
    setZones(bloodletting.battle, {CardInstance(CardId::BLOODLETTING)});
    bloodletting.battle.player.curHp = 3;
    const int bloodEnergy = bloodletting.battle.player.energy;
    playFirst(bloodletting.battle);
    require(bloodletting.battle.outcome == Outcome::PLAYER_LOSS &&
            bloodletting.battle.player.energy == bloodEnergy,
            "lethal Bloodletting did not interrupt queued energy");

    Fixture offering;
    setZones(
        offering.battle,
        {CardInstance(CardId::OFFERING)},
        {CardInstance(CardId::STRIKE_RED)}
    );
    offering.battle.player.curHp = 6;
    const int offeringEnergy = offering.battle.player.energy;
    playFirst(offering.battle);
    require(offering.battle.outcome == Outcome::PLAYER_LOSS &&
            offering.battle.player.energy == offeringEnergy &&
            offering.battle.cards.cardsInHand == 0,
            "lethal Offering did not interrupt queued energy/draw");
}

void verifyPublicStateSurvivesResampling() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    Fixture first({RelicId::TUNGSTEN_ROD});
    Fixture reversed({RelicId::TUNGSTEN_ROD});
    setZones(
        first.battle,
        {CardInstance(CardId::OFFERING)},
        {CardInstance(CardId::STRIKE_RED), CardInstance(CardId::DEFEND_RED)}
    );
    setZones(
        reversed.battle,
        {CardInstance(CardId::OFFERING)},
        {CardInstance(CardId::DEFEND_RED), CardInstance(CardId::STRIKE_RED)}
    );
    first.battle.player.buff<PS::RUPTURE>(2);
    reversed.battle.player.buff<PS::RUPTURE>(2);
    public_sampling::resampleCombatContinuation(first.game, first.battle, seeds);
    public_sampling::resampleCombatContinuation(reversed.game, reversed.battle, seeds);
    const int firstHp = first.battle.player.curHp;
    const int reversedHp = reversed.battle.player.curHp;
    playFirst(first.battle);
    playFirst(reversed.battle);
    require(firstHp - first.battle.player.curHp == 5 &&
            reversedHp - reversed.battle.player.curHp == 5,
            "resampling changed public Tungsten Rod self-HP loss");
    require(first.battle.player.getStatus<PS::STRENGTH>() == 2 &&
            reversed.battle.player.getStatus<PS::STRENGTH>() == 2,
            "resampling changed public Rupture response");
    require(first.battle.cards.cardsInHand == 2 &&
            reversed.battle.cards.cardsInHand == 2,
            "resampling changed Offering draw count");
}

}  // namespace

int main() {
    try {
        verifySeeingRedVariants();
        verifyBloodlettingVariants();
        verifyOfferingVariantsAndDrawLimits();
        verifySelfHpModifiersAndRupture();
        verifyLethalHpLossInterruptsLaterEffects();
        verifyPublicStateSurvivesResampling();
        std::cout << "ENERGY_SELF_HP_CARD_RECIPES_OK (3 cards, 6 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "ENERGY_SELF_HP_CARD_RECIPES_FAILED: "
                  << error.what() << '\n';
        return 1;
    }
}
