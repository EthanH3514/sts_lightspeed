#ifndef STS_PUBLIC_COMBAT_RESAMPLING_H
#define STS_PUBLIC_COMBAT_RESAMPLING_H

#include <algorithm>
#include <array>
#include <stdexcept>
#include <tuple>

#include "game/GameContext.h"
#include "combat/BattleContext.h"

namespace sts::public_sampling {

enum class IntentHistoryConstraint {
    KNOWN_MOVES,
    ALTERNATING,
    FIXED_WITHOUT_HISTORY_SHIFT,
};

struct MonsterBehaviorRecipe {
    MonsterId id;
    std::array<MMID, 4> moves;
    int moveCount;
    IntentHistoryConstraint history;
    MMID requiredRevealingMove;
};

struct EncounterCompositionRecipe {
    MonsterEncounter encounter;
    int minimumAscension;
    int maximumAscension;
    std::array<MonsterId, 4> monsters;
    int monsterCount;
};

static constexpr MonsterBehaviorRecipe monsterBehaviorRecipes[] {
    {MonsterId::CULTIST,
     {MMID::CULTIST_INCANTATION, MMID::CULTIST_DARK_STRIKE, MMID::INVALID, MMID::INVALID},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::JAW_WORM,
     {MMID::JAW_WORM_CHOMP, MMID::JAW_WORM_THRASH, MMID::JAW_WORM_BELLOW, MMID::INVALID},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::GREEN_LOUSE,
     {MMID::GREEN_LOUSE_BITE, MMID::GREEN_LOUSE_SPIT_WEB, MMID::INVALID, MMID::INVALID},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::GREEN_LOUSE_BITE},
    {MonsterId::RED_LOUSE,
     {MMID::RED_LOUSE_BITE, MMID::RED_LOUSE_GROW, MMID::INVALID, MMID::INVALID},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::RED_LOUSE_BITE},
    {MonsterId::ACID_SLIME_S,
     {MMID::ACID_SLIME_S_LICK, MMID::ACID_SLIME_S_TACKLE, MMID::INVALID, MMID::INVALID},
     2, IntentHistoryConstraint::ALTERNATING, MMID::INVALID},
    {MonsterId::ACID_SLIME_M,
     {MMID::ACID_SLIME_M_CORROSIVE_SPIT, MMID::ACID_SLIME_M_LICK,
      MMID::ACID_SLIME_M_TACKLE, MMID::INVALID},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPIKE_SLIME_S,
     {MMID::SPIKE_SLIME_S_TACKLE, MMID::INVALID, MMID::INVALID, MMID::INVALID},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::SPIKE_SLIME_M,
     {MMID::SPIKE_SLIME_M_FLAME_TACKLE, MMID::SPIKE_SLIME_M_LICK,
      MMID::INVALID, MMID::INVALID},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::GREMLIN_NOB,
     {MMID::GREMLIN_NOB_BELLOW, MMID::GREMLIN_NOB_RUSH,
      MMID::GREMLIN_NOB_SKULL_BASH, MMID::INVALID},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::LAGAVULIN,
     {MMID::LAGAVULIN_SLEEP, MMID::LAGAVULIN_ATTACK,
      MMID::LAGAVULIN_SIPHON_SOUL, MMID::INVALID},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SENTRY,
     {MMID::SENTRY_BEAM, MMID::SENTRY_BOLT, MMID::INVALID, MMID::INVALID},
     2, IntentHistoryConstraint::ALTERNATING, MMID::INVALID},
};

static constexpr EncounterCompositionRecipe encounterCompositionRecipes[] {
    {MonsterEncounter::CULTIST, 0, 20,
     {MonsterId::CULTIST, MonsterId::INVALID, MonsterId::INVALID, MonsterId::INVALID}, 1},
    {MonsterEncounter::JAW_WORM, 0, 20,
     {MonsterId::JAW_WORM, MonsterId::INVALID, MonsterId::INVALID, MonsterId::INVALID}, 1},
    {MonsterEncounter::TWO_LOUSE, 0, 20,
     {MonsterId::GREEN_LOUSE, MonsterId::GREEN_LOUSE, MonsterId::INVALID, MonsterId::INVALID}, 2},
    {MonsterEncounter::TWO_LOUSE, 0, 20,
     {MonsterId::GREEN_LOUSE, MonsterId::RED_LOUSE, MonsterId::INVALID, MonsterId::INVALID}, 2},
    {MonsterEncounter::TWO_LOUSE, 0, 20,
     {MonsterId::RED_LOUSE, MonsterId::GREEN_LOUSE, MonsterId::INVALID, MonsterId::INVALID}, 2},
    {MonsterEncounter::TWO_LOUSE, 0, 20,
     {MonsterId::RED_LOUSE, MonsterId::RED_LOUSE, MonsterId::INVALID, MonsterId::INVALID}, 2},
    {MonsterEncounter::SMALL_SLIMES, 0, 0,
     {MonsterId::ACID_SLIME_S, MonsterId::SPIKE_SLIME_M, MonsterId::INVALID, MonsterId::INVALID}, 2},
    {MonsterEncounter::SMALL_SLIMES, 0, 0,
     {MonsterId::SPIKE_SLIME_S, MonsterId::ACID_SLIME_M, MonsterId::INVALID, MonsterId::INVALID}, 2},
    {MonsterEncounter::GREMLIN_NOB, 0, 0,
     {MonsterId::GREMLIN_NOB, MonsterId::INVALID, MonsterId::INVALID, MonsterId::INVALID}, 1},
    {MonsterEncounter::LAGAVULIN, 0, 0,
     {MonsterId::LAGAVULIN, MonsterId::INVALID, MonsterId::INVALID, MonsterId::INVALID}, 1},
    {MonsterEncounter::THREE_SENTRIES, 0, 0,
     {MonsterId::SENTRY, MonsterId::SENTRY, MonsterId::SENTRY, MonsterId::INVALID}, 3},
};

inline const MonsterBehaviorRecipe *findMonsterBehaviorRecipe(MonsterId id) {
    for (const auto &recipe : monsterBehaviorRecipes) {
        if (recipe.id == id) return &recipe;
    }
    return nullptr;
}

inline bool recipeContainsMove(const MonsterBehaviorRecipe &recipe, MMID move) {
    for (int idx = 0; idx < recipe.moveCount; ++idx) {
        if (recipe.moves[idx] == move) return true;
    }
    return false;
}

inline bool compositionMatches(
    const BattleContext &battle, const EncounterCompositionRecipe &recipe
) {
    if (battle.monsters.monsterCount != recipe.monsterCount) return false;
    for (int idx = 0; idx < recipe.monsterCount; ++idx) {
        if (battle.monsters.arr[idx].id != recipe.monsters[idx]) return false;
    }
    return true;
}

inline void validateEncounterRecipe(const BattleContext &battle) {
    bool encounterDeclared = false;
    bool ascensionDeclared = false;
    bool compositionDeclared = false;
    for (const auto &recipe : encounterCompositionRecipes) {
        if (recipe.encounter != battle.encounter) continue;
        encounterDeclared = true;
        if (battle.ascension < recipe.minimumAscension ||
            battle.ascension > recipe.maximumAscension) continue;
        ascensionDeclared = true;
        if (compositionMatches(battle, recipe)) compositionDeclared = true;
    }
    if (!encounterDeclared) {
        throw std::runtime_error("encounter has no public resampling recipe in v3");
    }
    if (!ascensionDeclared) {
        throw std::runtime_error("encounter ascension is outside its public recipe in v3");
    }
    if (!compositionDeclared) {
        throw std::runtime_error("encounter composition does not match its public recipe");
    }

    for (int idx = 0; idx < battle.monsters.monsterCount; ++idx) {
        const auto &monster = battle.monsters.arr[idx];
        const auto *recipe = findMonsterBehaviorRecipe(monster.id);
        if (recipe == nullptr) {
            throw std::runtime_error("encounter references an undeclared monster recipe");
        }
        const auto current = monster.moveHistory[0];
        const auto previous = monster.moveHistory[1];
        if (!recipeContainsMove(*recipe, current)) {
            throw std::runtime_error("current intent is outside the monster recipe");
        }
        if (previous != MMID::INVALID && !recipeContainsMove(*recipe, previous)) {
            throw std::runtime_error("public intent history is outside the monster recipe");
        }
        if (recipe->history == IntentHistoryConstraint::ALTERNATING &&
            previous != MMID::INVALID && previous == current) {
            throw std::runtime_error("monster recipe requires alternating public intents");
        }
        if (recipe->history == IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT &&
            previous != MMID::INVALID) {
            throw std::runtime_error("fixed monster recipe cannot shift public intent history");
        }
        if (!monster.isDeadOrEscaped() && recipe->requiredRevealingMove != MMID::INVALID &&
            current != recipe->requiredRevealingMove) {
            throw std::runtime_error("hidden monster damage has not been publicly revealed");
        }
    }
}

// Approximate continuation sampling, not exact conditioning on public history.
// Caller must reject roots with unsupported previously revealed order constraints.
inline void resampleCombatContinuation(
    const GameContext &game, BattleContext &battle,
    const std::array<std::uint64_t, 7> &seeds, int knownDrawTopUniqueId = -1
) {
    if (game.screenState != ScreenState::BATTLE ||
        battle.outcome != Outcome::UNDECIDED || battle.inputState != InputState::PLAYER_NORMAL ||
        battle.actionQueue.size != 0 || battle.cardQueue.size != 0) {
        throw std::runtime_error("combat sampling requires a resolved PLAYER_NORMAL decision");
    }
    if (game.cc != CharacterClass::IRONCLAD) {
        throw std::runtime_error("combat resampler v3 supports Ironclad only");
    }
    validateEncounterRecipe(battle);
    const bool frozenEye = game.hasRelic(RelicId::FROZEN_EYE);
    auto &pile = battle.cards.drawPile;
    if (knownDrawTopUniqueId >= 0 &&
        (pile.empty() || pile.back().uniqueId != knownDrawTopUniqueId)) {
        throw std::runtime_error("known public draw-pile top does not match native state");
    }
    if (battle.cards.publicBottomOrderUncertain) {
        throw std::runtime_error("bottom order after random insertion is not yet supported");
    }
    const auto &bottom = battle.cards.publicKnownBottomIds;
    if (bottom.size() > pile.size()) throw std::runtime_error("invalid known bottom size");
    for (std::size_t i = 0; i < bottom.size(); ++i) {
        if (pile[i].uniqueId != bottom[i]) throw std::runtime_error("known bottom identity mismatch");
    }
    auto unknownBegin = pile.begin() + bottom.size();
    auto unknownEnd = pile.end();
    if (knownDrawTopUniqueId >= 0 && unknownBegin != unknownEnd) {
        --unknownEnd;
    }
    if (!frozenEye) {
        for (auto it = unknownBegin; it != unknownEnd; ++it) {
            const auto &card = *it;
            const int idx = card.uniqueId;
            if (idx >= 0 && idx < game.deck.size() &&
                (game.deck.cards[idx].isInnate() ||
                 std::find(game.deck.bottleIdxs.begin(), game.deck.bottleIdxs.end(), idx) !=
                 game.deck.bottleIdxs.end())) {
                throw std::runtime_error("undrawn innate/bottled order constraints are unsupported");
            }
        }
    }
    battle.aiRng = Random(seeds[0]);
    battle.cardRandomRng = Random(seeds[1]);
    battle.miscRng = Random(seeds[2]);
    battle.monsterHpRng = Random(seeds[3]);
    battle.potionRng = Random(seeds[4]);
    battle.shuffleRng = Random(seeds[5]);
    if (!frozenEye) {
        std::sort(unknownBegin, unknownEnd, [](const CardInstance &a, const CardInstance &b) {
            return std::tie(a.id, a.upgraded, a.specialData, a.cost, a.costForTurn,
                            a.freeToPlayOnce, a.retain, a.uniqueId) <
                   std::tie(b.id, b.upgraded, b.specialData, b.cost, b.costForTurn,
                            b.freeToPlayOnce, b.retain, b.uniqueId);
        });
        java::Collections::shuffle(unknownBegin, unknownEnd, java::Random(seeds[6]));
    }
}
}
#endif
