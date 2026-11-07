#include <array>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

BattleContext battle(MonsterEncounter encounter) {
    GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
    game.enterBattle(encounter);
    BattleContext result;
    result.init(game);
    result.executeActions();
    require(result.inputState == InputState::PLAYER_NORMAL, "fixture is not at a decision");
    return result;
}

void setHand(BattleContext &context, std::initializer_list<CardInstance> cards) {
    context.cards.cardsInHand = static_cast<int>(cards.size());
    int index = 0;
    for (auto card : cards) {
        card.setUniqueId(100 + index);
        context.cards.hand[index++] = card;
    }
}

void play(BattleContext &context, int target=0) {
    const auto card = context.cards.hand[0];
    require(card.canUse(context, target, false), "fixture card is not playable");
    context.addToBotCard(CardQueueItem(card, target, context.player.energy));
    context.inputState = InputState::EXECUTING_ACTIONS;
    context.executeActions();
}

struct TargetRecipe {
    CardId card;
    std::array<int, 2> damage;
    std::array<int, 2> weak;
    std::array<int, 2> vulnerable;
};

struct AllEnemyRecipe {
    CardId card;
    std::array<int, 2> damage;
    std::array<int, 2> weak;
    std::array<int, 2> vulnerable;
    bool exhaust;
};

void targetedRecipesMatch() {
    const std::array<TargetRecipe, 3> recipes {{
        {CardId::BASH, {8, 10}, {0, 0}, {2, 3}},
        {CardId::CLOTHESLINE, {12, 14}, {2, 3}, {0, 0}},
        {CardId::UPPERCUT, {13, 13}, {1, 2}, {1, 2}},
    }};
    for (const auto &recipe : recipes) {
        for (int upgraded = 0; upgraded < 2; ++upgraded) {
            auto context = battle(MonsterEncounter::CULTIST);
            setHand(context, {CardInstance(recipe.card, upgraded != 0)});
            const int hpBefore = context.monsters.arr[0].curHp;
            play(context);
            require(
                hpBefore - context.monsters.arr[0].curHp == recipe.damage[upgraded],
                "targeted recipe damage/order changed"
            );
            require(
                context.monsters.arr[0].getStatus<MS::WEAK>() == recipe.weak[upgraded],
                "targeted recipe Weak amount changed"
            );
            require(
                !context.monsters.arr[0].wasJustApplied<MS::WEAK>(),
                "player-applied Weak incorrectly set the monster-source duration flag"
            );
            require(
                context.monsters.arr[0].getStatus<MS::VULNERABLE>()
                    == recipe.vulnerable[upgraded],
                "targeted recipe Vulnerable amount changed"
            );
            require(
                !context.monsters.arr[0].wasJustApplied<MS::VULNERABLE>(),
                "player-applied Vulnerable incorrectly set the monster-source duration flag"
            );
            require(context.cards.exhaustPile.empty(), "non-exhaust recipe exhausted");
            require(context.cards.discardPile.size() == 1, "non-exhaust recipe did not discard");
        }
    }
}

void allEnemyRecipesMatch() {
    const std::array<AllEnemyRecipe, 3> recipes {{
        {CardId::THUNDERCLAP, {4, 7}, {0, 0}, {1, 1}, false},
        {CardId::SHOCKWAVE, {0, 0}, {3, 5}, {3, 5}, true},
        {CardId::INTIMIDATE, {0, 0}, {1, 2}, {0, 0}, true},
    }};
    for (const auto &recipe : recipes) {
        for (int upgraded = 0; upgraded < 2; ++upgraded) {
            auto context = battle(MonsterEncounter::TWO_LOUSE);
            setHand(context, {CardInstance(recipe.card, upgraded != 0)});
            const int firstHp = context.monsters.arr[0].curHp;
            const int secondHp = context.monsters.arr[1].curHp;
            play(context);
            for (int monster = 0; monster < 2; ++monster) {
                const int before = monster == 0 ? firstHp : secondHp;
                require(
                    before - context.monsters.arr[monster].curHp == recipe.damage[upgraded],
                    "all-enemy recipe damage/order changed"
                );
                require(
                    context.monsters.arr[monster].getStatus<MS::WEAK>()
                        == recipe.weak[upgraded],
                    "all-enemy recipe Weak amount changed"
                );
                require(
                    !context.monsters.arr[monster].wasJustApplied<MS::WEAK>(),
                    "all-enemy Weak incorrectly set the monster-source duration flag"
                );
                require(
                    context.monsters.arr[monster].getStatus<MS::VULNERABLE>()
                        == recipe.vulnerable[upgraded],
                    "all-enemy recipe Vulnerable amount changed"
                );
                require(
                    !context.monsters.arr[monster].wasJustApplied<MS::VULNERABLE>(),
                    "all-enemy Vulnerable incorrectly set the monster-source duration flag"
                );
            }
            require(
                context.cards.exhaustPile.size() == (recipe.exhaust ? 1 : 0),
                "all-enemy recipe exhaust movement changed"
            );
            require(
                context.cards.discardPile.size() == (recipe.exhaust ? 0 : 1),
                "all-enemy recipe discard movement changed"
            );
        }
    }
}

void orderedDebuffsConsumeArtifact() {
    auto uppercut = battle(MonsterEncounter::CULTIST);
    setHand(uppercut, {CardInstance(CardId::UPPERCUT)});
    uppercut.monsters.arr[0].buff<MS::ARTIFACT>(1);
    play(uppercut);
    require(uppercut.monsters.arr[0].getStatus<MS::ARTIFACT>() == 0,
            "Uppercut did not consume Artifact");
    require(uppercut.monsters.arr[0].getStatus<MS::WEAK>() == 0,
            "Uppercut debuff order no longer starts with Weak");
    require(uppercut.monsters.arr[0].getStatus<MS::VULNERABLE>() == 1,
            "Uppercut Vulnerable did not follow blocked Weak");

    auto shockwave = battle(MonsterEncounter::TWO_LOUSE);
    setHand(shockwave, {CardInstance(CardId::SHOCKWAVE)});
    for (int monster = 0; monster < 2; ++monster) {
        shockwave.monsters.arr[monster].buff<MS::ARTIFACT>(1);
    }
    play(shockwave);
    for (int monster = 0; monster < 2; ++monster) {
        require(shockwave.monsters.arr[monster].getStatus<MS::WEAK>() == 0,
                "Shockwave debuff order no longer starts with Weak");
        require(shockwave.monsters.arr[monster].getStatus<MS::VULNERABLE>() == 3,
                "Shockwave Vulnerable did not follow blocked Weak");
    }
}
}

int main() {
    try {
        targetedRecipesMatch();
        allEnemyRecipesMatch();
        orderedDebuffsConsumeArtifact();
        std::cout << "WEAK_VULNERABLE_CARD_RECIPES_OK (6 cards, 12 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "WEAK_VULNERABLE_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
