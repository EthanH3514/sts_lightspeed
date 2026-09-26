#include <array>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

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

    Fixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "strength fixture is not at player control");
    }
};

void setHand(BattleContext &battle, std::initializer_list<CardInstance> hand) {
    battle.cards = CardManager{};
    int uniqueId = 100;
    for (auto card : hand) {
        card.setUniqueId(uniqueId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
    }
    battle.cards.nextUniqueCardId = uniqueId;
}

void playFirst(BattleContext &battle) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, 0, false), "strength card is not playable");
    battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
    if (battle.inputState != InputState::PLAYER_NORMAL) {
        throw std::runtime_error(
            std::string("strength card did not return player control: ")
            + cardEnumStrings[static_cast<int>(card.id)]
            + " state=" + std::to_string(static_cast<int>(battle.inputState))
            + " outcome=" + std::to_string(static_cast<int>(battle.outcome))
        );
    }
}

void endTurn(BattleContext &battle) {
    search::Action action(search::ActionType::END_TURN);
    require(action.isValidAction(battle), "strength fixture end turn is illegal");
    action.execute(battle);
    require(battle.inputState == InputState::PLAYER_NORMAL,
            "strength fixture enemy turn did not resolve");
}

void verifyFlexVariants() {
    for (const bool upgraded : {false, true}) {
        Fixture fixture;
        setHand(fixture.battle, {
            CardInstance(CardId::FLEX, upgraded), CardInstance(CardId::STRIKE_RED),
        });
        const int amount = upgraded ? 4 : 2;
        const int energy = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(fixture.battle.player.energy == energy,
                "Flex unexpectedly spent energy");
        require(fixture.battle.player.getStatus<PS::STRENGTH>() == amount &&
                fixture.battle.player.getStatus<PS::LOSE_STRENGTH>() == amount,
                "Flex did not apply ordered Strength and Strength Down");
        require(fixture.battle.cards.discardPile.size() == 1 &&
                fixture.battle.cards.exhaustPile.empty(),
                "Flex moved to the wrong pile");
        const int monsterHp = fixture.battle.monsters.arr[0].curHp;
        playFirst(fixture.battle);
        require(monsterHp - fixture.battle.monsters.arr[0].curHp == 6 + amount,
                "Flex did not increase a same-turn attack");
        endTurn(fixture.battle);
        require(fixture.battle.player.getStatus<PS::STRENGTH>() == 0 &&
                fixture.battle.player.getStatus<PS::LOSE_STRENGTH>() == 0,
                "Flex Strength did not expire at end of turn");
    }
}

void verifyFlexArtifactInteraction() {
    Fixture initialArtifact;
    setHand(initialArtifact.battle, {CardInstance(CardId::FLEX)});
    initialArtifact.battle.player.artifact = 1;
    playFirst(initialArtifact.battle);
    require(initialArtifact.battle.player.getStatus<PS::STRENGTH>() == 2 &&
            initialArtifact.battle.player.getStatus<PS::LOSE_STRENGTH>() == 0 &&
            initialArtifact.battle.player.getStatus<PS::ARTIFACT>() == 0,
            "Artifact did not block Flex Strength Down");
    endTurn(initialArtifact.battle);
    require(initialArtifact.battle.player.getStatus<PS::STRENGTH>() == 2,
            "Artifact-protected Flex Strength did not persist");

    Fixture lateArtifact;
    setHand(lateArtifact.battle, {CardInstance(CardId::FLEX)});
    playFirst(lateArtifact.battle);
    lateArtifact.battle.player.artifact = 1;
    endTurn(lateArtifact.battle);
    require(lateArtifact.battle.player.getStatus<PS::STRENGTH>() == 2 &&
            lateArtifact.battle.player.getStatus<PS::LOSE_STRENGTH>() == 0 &&
            lateArtifact.battle.player.getStatus<PS::ARTIFACT>() == 0,
            "late Artifact did not block Flex end-turn Strength loss");
}

void verifyInflameVariants() {
    for (const bool upgraded : {false, true}) {
        Fixture fixture;
        setHand(fixture.battle, {
            CardInstance(CardId::INFLAME, upgraded), CardInstance(CardId::STRIKE_RED),
        });
        const int amount = upgraded ? 3 : 2;
        const int energy = fixture.battle.player.energy;
        playFirst(fixture.battle);
        require(fixture.battle.player.energy == energy - 1 &&
                fixture.battle.player.getStatus<PS::STRENGTH>() == amount &&
                fixture.battle.player.getStatus<PS::LOSE_STRENGTH>() == 0,
                "Inflame base/upgraded permanent Strength changed");
        require(fixture.battle.cards.cardsInHand == 1 &&
                fixture.battle.cards.discardPile.empty() &&
                fixture.battle.cards.exhaustPile.empty(),
                "played Inflame power unexpectedly entered a card pile");
        endTurn(fixture.battle);
        require(fixture.battle.player.getStatus<PS::STRENGTH>() == amount,
                "Inflame Strength did not persist into the next turn");
    }
}

void verifySpotWeaknessBranches() {
    for (const bool upgraded : {false, true}) {
        Fixture falseBranch;
        require(!falseBranch.battle.monsters.arr[0].isAttacking(),
                "Cultist opening move is unexpectedly an attack");
        setHand(falseBranch.battle, {CardInstance(CardId::SPOT_WEAKNESS, upgraded)});
        const int falseEnergy = falseBranch.battle.player.energy;
        playFirst(falseBranch.battle);
        require(falseBranch.battle.player.energy == falseEnergy - 1 &&
                falseBranch.battle.player.getStatus<PS::STRENGTH>() == 0,
                "Spot Weakness gained Strength on a non-attack intent");

        Fixture trueBranch;
        endTurn(trueBranch.battle);
        require(trueBranch.battle.monsters.arr[0].isAttacking(),
                "Cultist did not display an attack after Incantation");
        setHand(trueBranch.battle, {CardInstance(CardId::SPOT_WEAKNESS, upgraded)});
        const int trueEnergy = trueBranch.battle.player.energy;
        playFirst(trueBranch.battle);
        require(trueBranch.battle.player.energy == trueEnergy - 1 &&
                trueBranch.battle.player.getStatus<PS::STRENGTH>() ==
                    (upgraded ? 4 : 3),
                "Spot Weakness base/upgraded attack-intent Strength changed");
        require(trueBranch.battle.cards.discardPile.size() == 1,
                "Spot Weakness did not enter discard");
    }
}

void verifyPublicRootResampling() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    Fixture flex;
    setHand(flex.battle, {CardInstance(CardId::FLEX)});
    public_sampling::resampleCombatContinuation(flex.game, flex.battle, seeds);
    playFirst(flex.battle);
    require(flex.battle.player.getStatus<PS::STRENGTH>() == 2 &&
            flex.battle.player.getStatus<PS::LOSE_STRENGTH>() == 2,
            "resampling changed the public Flex power pair");

    Fixture spot;
    endTurn(spot.battle);
    setHand(spot.battle, {CardInstance(CardId::SPOT_WEAKNESS)});
    require(spot.battle.monsters.arr[0].isAttacking(),
            "Spot Weakness resampling root lacks attack intent");
    public_sampling::resampleCombatContinuation(spot.game, spot.battle, seeds);
    require(spot.battle.monsters.arr[0].isAttacking(),
            "resampling changed the current public attack intent");
    playFirst(spot.battle);
    require(spot.battle.player.getStatus<PS::STRENGTH>() == 3,
            "resampling changed Spot Weakness attack branch");
}

}  // namespace

int main() {
    try {
        verifyFlexVariants();
        verifyFlexArtifactInteraction();
        verifyInflameVariants();
        verifySpotWeaknessBranches();
        verifyPublicRootResampling();
        std::cout << "SELF_STRENGTH_CARD_RECIPES_OK (3 cards, 6 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "SELF_STRENGTH_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
