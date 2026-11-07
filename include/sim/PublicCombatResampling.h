#ifndef STS_PUBLIC_COMBAT_RESAMPLING_H
#define STS_PUBLIC_COMBAT_RESAMPLING_H

#include <algorithm>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "game/GameContext.h"
#include "combat/BattleContext.h"
#include "sim/PublicEnemyHistory.h"

namespace sts::public_sampling {

enum class IntentHistoryConstraint {
    KNOWN_MOVES,
    ALTERNATING,
    FIXED_WITHOUT_HISTORY_SHIFT,
};

struct MonsterBehaviorRecipe {
    MonsterId id;
    std::initializer_list<MMID> moves;
    int moveCount;
    IntentHistoryConstraint history;
    MMID requiredRevealingMove;
};

struct EncounterCompositionRecipe {
    MonsterEncounter encounter;
    int minimumAscension;
    int maximumAscension;
    std::array<MonsterId, 5> monsters;
    int monsterCount;
};

static constexpr MonsterBehaviorRecipe monsterBehaviorRecipes[] {
    {MonsterId::AWAKENED_ONE, {MMID::AWAKENED_ONE_SLASH, MMID::AWAKENED_ONE_SOUL_STRIKE, MMID::AWAKENED_ONE_REBIRTH, MMID::AWAKENED_ONE_DARK_ECHO, MMID::AWAKENED_ONE_SLUDGE, MMID::AWAKENED_ONE_TACKLE},
     6, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::BEAR, {MMID::BEAR_BEAR_HUG, MMID::BEAR_LUNGE, MMID::BEAR_MAUL},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::BRONZE_AUTOMATON, {MMID::BRONZE_AUTOMATON_BOOST, MMID::BRONZE_AUTOMATON_FLAIL, MMID::BRONZE_AUTOMATON_HYPER_BEAM, MMID::BRONZE_AUTOMATON_SPAWN_ORBS, MMID::BRONZE_AUTOMATON_STUNNED},
     5, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::BRONZE_ORB, {MMID::BRONZE_ORB_BEAM, MMID::BRONZE_ORB_STASIS, MMID::BRONZE_ORB_SUPPORT_BEAM},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::CORRUPT_HEART, {MMID::CORRUPT_HEART_DEBILITATE, MMID::CORRUPT_HEART_BLOOD_SHOTS, MMID::CORRUPT_HEART_ECHO, MMID::CORRUPT_HEART_BUFF},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::DAGGER, {MMID::DAGGER_STAB, MMID::DAGGER_EXPLODE},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::DARKLING, {MMID::DARKLING_NIP, MMID::DARKLING_CHOMP, MMID::DARKLING_HARDEN, MMID::DARKLING_REINCARNATE, MMID::DARKLING_REGROW},
     5, IntentHistoryConstraint::KNOWN_MOVES, MMID::DARKLING_NIP},
    {MonsterId::DECA, {MMID::DECA_SQUARE_OF_PROTECTION, MMID::DECA_BEAM},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::DONU, {MMID::DONU_CIRCLE_OF_POWER, MMID::DONU_BEAM},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::EXPLODER, {MMID::EXPLODER_SLAM, MMID::EXPLODER_EXPLODE},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::GIANT_HEAD, {MMID::GIANT_HEAD_COUNT, MMID::GIANT_HEAD_GLARE, MMID::GIANT_HEAD_IT_IS_TIME},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::GREMLIN_LEADER, {MMID::GREMLIN_LEADER_ENCOURAGE, MMID::GREMLIN_LEADER_RALLY, MMID::GREMLIN_LEADER_STAB},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::NEMESIS, {MMID::NEMESIS_DEBUFF, MMID::NEMESIS_ATTACK, MMID::NEMESIS_SCYTHE},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::ORB_WALKER, {MMID::ORB_WALKER_LASER, MMID::ORB_WALKER_CLAW},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::POINTY, {MMID::POINTY_ATTACK},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::REPTOMANCER, {MMID::REPTOMANCER_SUMMON, MMID::REPTOMANCER_SNAKE_STRIKE, MMID::REPTOMANCER_BIG_BITE},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::REPULSOR, {MMID::REPULSOR_BASH, MMID::REPULSOR_REPULSE},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::ROMEO, {MMID::ROMEO_MOCK, MMID::ROMEO_AGONIZING_SLASH, MMID::ROMEO_CROSS_SLASH},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPIKER, {MMID::SPIKER_CUT, MMID::SPIKER_SPIKE},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPIRE_GROWTH, {MMID::SPIRE_GROWTH_QUICK_TACKLE, MMID::SPIRE_GROWTH_SMASH, MMID::SPIRE_GROWTH_CONSTRICT},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPIRE_SHIELD, {MMID::SPIRE_SHIELD_BASH, MMID::SPIRE_SHIELD_FORTIFY, MMID::SPIRE_SHIELD_SMASH},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPIRE_SPEAR, {MMID::SPIRE_SPEAR_BURN_STRIKE, MMID::SPIRE_SPEAR_PIERCER, MMID::SPIRE_SPEAR_SKEWER},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::THE_CHAMP, {MMID::THE_CHAMP_DEFENSIVE_STANCE, MMID::THE_CHAMP_FACE_SLAP, MMID::THE_CHAMP_TAUNT, MMID::THE_CHAMP_HEAVY_SLASH, MMID::THE_CHAMP_GLOAT, MMID::THE_CHAMP_EXECUTE, MMID::THE_CHAMP_ANGER},
     7, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::THE_COLLECTOR, {MMID::THE_COLLECTOR_BUFF, MMID::THE_COLLECTOR_FIREBALL, MMID::THE_COLLECTOR_MEGA_DEBUFF, MMID::THE_COLLECTOR_SPAWN},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::THE_MAW, {MMID::THE_MAW_ROAR, MMID::THE_MAW_DROOL, MMID::THE_MAW_SLAM, MMID::THE_MAW_NOM},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::TIME_EATER, {MMID::TIME_EATER_REVERBERATE, MMID::TIME_EATER_HEAD_SLAM, MMID::TIME_EATER_RIPPLE, MMID::TIME_EATER_HASTE},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::TORCH_HEAD, {MMID::TORCH_HEAD_TACKLE},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::TRANSIENT, {MMID::TRANSIENT_ATTACK},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::WRITHING_MASS, {MMID::WRITHING_MASS_IMPLANT, MMID::WRITHING_MASS_FLAIL, MMID::WRITHING_MASS_WITHER, MMID::WRITHING_MASS_MULTI_STRIKE, MMID::WRITHING_MASS_STRONG_STRIKE},
     5, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::BOOK_OF_STABBING,
     {MMID::BOOK_OF_STABBING_MULTI_STAB, MMID::BOOK_OF_STABBING_SINGLE_STAB},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::TASKMASTER, {MMID::TASKMASTER_SCOURING_WHIP},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::CENTURION,
     {MMID::CENTURION_DEFEND, MMID::CENTURION_FURY, MMID::CENTURION_SLASH},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::MYSTIC,
     {MMID::MYSTIC_ATTACK_DEBUFF, MMID::MYSTIC_BUFF, MMID::MYSTIC_HEAL},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SNECKO,
     {MMID::SNECKO_PERPLEXING_GLARE, MMID::SNECKO_BITE, MMID::SNECKO_TAIL_WHIP},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::MUGGER, {MMID::MUGGER_MUG, MMID::MUGGER_LUNGE,
      MMID::MUGGER_SMOKE_BOMB, MMID::MUGGER_ESCAPE},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::BYRD,
     {MMID::BYRD_CAW, MMID::BYRD_FLY, MMID::BYRD_HEADBUTT,
      MMID::BYRD_PECK, MMID::BYRD_STUNNED, MMID::BYRD_SWOOP},
     6, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::CHOSEN,
     {MMID::CHOSEN_DEBILITATE, MMID::CHOSEN_DRAIN, MMID::CHOSEN_HEX,
      MMID::CHOSEN_POKE, MMID::CHOSEN_ZAP},
     5, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPHERIC_GUARDIAN,
     {MMID::SPHERIC_GUARDIAN_ACTIVATE, MMID::SPHERIC_GUARDIAN_ATTACK_DEBUFF,
      MMID::SPHERIC_GUARDIAN_SLAM, MMID::SPHERIC_GUARDIAN_HARDEN},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SHELLED_PARASITE,
     {MMID::SHELLED_PARASITE_DOUBLE_STRIKE, MMID::SHELLED_PARASITE_FELL,
      MMID::SHELLED_PARASITE_SUCK, MMID::SHELLED_PARASITE_STUNNED},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SNAKE_PLANT, {MMID::SNAKE_PLANT_CHOMP, MMID::SNAKE_PLANT_ENFEEBLING_SPORES},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::BLUE_SLAVER, {MMID::BLUE_SLAVER_STAB, MMID::BLUE_SLAVER_RAKE},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::RED_SLAVER,
     {MMID::RED_SLAVER_STAB, MMID::RED_SLAVER_SCRAPE, MMID::RED_SLAVER_ENTANGLE},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::LOOTER, {MMID::LOOTER_MUG, MMID::LOOTER_LUNGE,
      MMID::LOOTER_SMOKE_BOMB, MMID::LOOTER_ESCAPE},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::MAD_GREMLIN, {MMID::MAD_GREMLIN_SCRATCH},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::SNEAKY_GREMLIN, {MMID::SNEAKY_GREMLIN_PUNCTURE},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::FAT_GREMLIN, {MMID::FAT_GREMLIN_SMASH},
     1, IntentHistoryConstraint::FIXED_WITHOUT_HISTORY_SHIFT, MMID::INVALID},
    {MonsterId::SHIELD_GREMLIN,
     {MMID::SHIELD_GREMLIN_PROTECT, MMID::SHIELD_GREMLIN_SHIELD_BASH},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::GREMLIN_WIZARD,
     {MMID::GREMLIN_WIZARD_CHARGING, MMID::GREMLIN_WIZARD_ULTIMATE_BLAST},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::FUNGI_BEAST,
     {MMID::FUNGI_BEAST_BITE, MMID::FUNGI_BEAST_GROW},
     2, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::THE_GUARDIAN,
     {MMID::THE_GUARDIAN_CHARGING_UP, MMID::THE_GUARDIAN_FIERCE_BASH,
      MMID::THE_GUARDIAN_VENT_STEAM, MMID::THE_GUARDIAN_WHIRLWIND,
      MMID::THE_GUARDIAN_DEFENSIVE_MODE, MMID::THE_GUARDIAN_ROLL_ATTACK,
      MMID::THE_GUARDIAN_TWIN_SLAM},
     7, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::HEXAGHOST,
     {MMID::HEXAGHOST_ACTIVATE, MMID::HEXAGHOST_DIVIDER, MMID::HEXAGHOST_SEAR,
      MMID::HEXAGHOST_TACKLE, MMID::HEXAGHOST_INFLAME, MMID::HEXAGHOST_INFERNO},
     6, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SLIME_BOSS,
     {MMID::SLIME_BOSS_GOOP_SPRAY, MMID::SLIME_BOSS_PREPARING,
      MMID::SLIME_BOSS_SLAM, MMID::SLIME_BOSS_SPLIT},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::ACID_SLIME_L,
     {MMID::ACID_SLIME_L_CORROSIVE_SPIT, MMID::ACID_SLIME_L_TACKLE,
      MMID::ACID_SLIME_L_LICK, MMID::ACID_SLIME_L_SPLIT},
     4, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
    {MonsterId::SPIKE_SLIME_L,
     {MMID::SPIKE_SLIME_L_FLAME_TACKLE, MMID::SPIKE_SLIME_L_LICK,
      MMID::SPIKE_SLIME_L_SPLIT},
     3, IntentHistoryConstraint::KNOWN_MOVES, MMID::INVALID},
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

inline const std::vector<EncounterCompositionRecipe> encounterCompositionRecipes = [] {
    std::vector<EncounterCompositionRecipe> recipes {
    {MonsterEncounter::CHAMP, 0, 0, {MonsterId::THE_CHAMP}, 1},
    {MonsterEncounter::THREE_DARKLINGS, 0, 0, {MonsterId::DARKLING, MonsterId::DARKLING, MonsterId::DARKLING}, 3},
    {MonsterEncounter::ORB_WALKER, 0, 0, {MonsterId::ORB_WALKER}, 1},
    {MonsterEncounter::SPIRE_GROWTH, 0, 0, {MonsterId::SPIRE_GROWTH}, 1},
    {MonsterEncounter::TRANSIENT, 0, 0, {MonsterId::TRANSIENT}, 1},
    {MonsterEncounter::MAW, 0, 0, {MonsterId::THE_MAW}, 1},
    {MonsterEncounter::JAW_WORM_HORDE, 0, 0, {MonsterId::JAW_WORM, MonsterId::JAW_WORM, MonsterId::JAW_WORM}, 3},
    {MonsterEncounter::WRITHING_MASS, 0, 0, {MonsterId::WRITHING_MASS}, 1},
    {MonsterEncounter::GIANT_HEAD, 0, 0, {MonsterId::GIANT_HEAD}, 1},
    {MonsterEncounter::NEMESIS, 0, 0, {MonsterId::NEMESIS}, 1},
    {MonsterEncounter::AWAKENED_ONE, 0, 0, {MonsterId::CULTIST, MonsterId::CULTIST, MonsterId::AWAKENED_ONE}, 3},
    {MonsterEncounter::TIME_EATER, 0, 0, {MonsterId::TIME_EATER}, 1},
    {MonsterEncounter::DONU_AND_DECA, 0, 0, {MonsterId::DECA, MonsterId::DONU}, 2},
    {MonsterEncounter::SHIELD_AND_SPEAR, 0, 0, {MonsterId::SPIRE_SHIELD, MonsterId::SPIRE_SPEAR}, 2},
    {MonsterEncounter::THE_HEART, 0, 0, {MonsterId::CORRUPT_HEART}, 1},
    {MonsterEncounter::COLOSSEUM_EVENT_SLAVERS, 0, 0, {MonsterId::BLUE_SLAVER, MonsterId::RED_SLAVER}, 2},
    {MonsterEncounter::COLOSSEUM_EVENT_NOBS, 0, 0, {MonsterId::TASKMASTER, MonsterId::GREMLIN_NOB}, 2},
    {MonsterEncounter::MASKED_BANDITS_EVENT, 0, 0, {MonsterId::POINTY, MonsterId::ROMEO, MonsterId::BEAR}, 3},
    {MonsterEncounter::MYSTERIOUS_SPHERE_EVENT, 0, 0, {MonsterId::ORB_WALKER, MonsterId::ORB_WALKER}, 2},
    {MonsterEncounter::SLAVERS, 0, 0, {MonsterId::BLUE_SLAVER, MonsterId::TASKMASTER, MonsterId::RED_SLAVER}, 3},
    {MonsterEncounter::BOOK_OF_STABBING, 0, 0, {MonsterId::BOOK_OF_STABBING}, 1},
    {MonsterEncounter::CENTURION_AND_HEALER, 0, 0, {MonsterId::CENTURION, MonsterId::MYSTIC}, 2},
    {MonsterEncounter::SNECKO, 0, 0, {MonsterId::SNECKO}, 1},
    {MonsterEncounter::TWO_THIEVES, 0, 0, {MonsterId::LOOTER, MonsterId::MUGGER}, 2},
    {MonsterEncounter::CHOSEN, 0, 0, {MonsterId::CHOSEN}, 1},
    {MonsterEncounter::THREE_BYRDS, 0, 0, {MonsterId::BYRD, MonsterId::BYRD, MonsterId::BYRD}, 3},
    {MonsterEncounter::CHOSEN_AND_BYRDS, 0, 0, {MonsterId::BYRD, MonsterId::CHOSEN}, 2},
    {MonsterEncounter::CULTIST_AND_CHOSEN, 0, 0, {MonsterId::CULTIST, MonsterId::CHOSEN}, 2},
    {MonsterEncounter::SPHERIC_GUARDIAN, 0, 0, {MonsterId::SPHERIC_GUARDIAN}, 1},
    {MonsterEncounter::SHELL_PARASITE, 0, 0, {MonsterId::SHELLED_PARASITE}, 1},
    {MonsterEncounter::SNAKE_PLANT, 0, 0, {MonsterId::SNAKE_PLANT}, 1},
    {MonsterEncounter::SENTRY_AND_SPHERE, 0, 0, {MonsterId::SENTRY, MonsterId::SPHERIC_GUARDIAN}, 2},
    {MonsterEncounter::SHELLED_PARASITE_AND_FUNGI, 0, 0, {MonsterId::SHELLED_PARASITE, MonsterId::FUNGI_BEAST}, 2},
    {MonsterEncounter::THREE_CULTIST, 0, 0, {MonsterId::CULTIST, MonsterId::CULTIST, MonsterId::CULTIST}, 3},
    {MonsterEncounter::BLUE_SLAVER, 0, 0, {MonsterId::BLUE_SLAVER}, 1},
    {MonsterEncounter::RED_SLAVER, 0, 0, {MonsterId::RED_SLAVER}, 1},
    {MonsterEncounter::LOOTER, 0, 0, {MonsterId::LOOTER}, 1},
    {MonsterEncounter::LARGE_SLIME, 0, 0, {MonsterId::ACID_SLIME_L}, 1},
    {MonsterEncounter::LARGE_SLIME, 0, 0, {MonsterId::SPIKE_SLIME_L}, 1},
    {MonsterEncounter::LARGE_SLIME, 0, 0,
     {MonsterId::ACID_SLIME_M, MonsterId::ACID_SLIME_M}, 2},
    {MonsterEncounter::LARGE_SLIME, 0, 0,
     {MonsterId::SPIKE_SLIME_M, MonsterId::SPIKE_SLIME_M}, 2},
    {MonsterEncounter::TWO_FUNGI_BEASTS, 0, 0,
     {MonsterId::FUNGI_BEAST, MonsterId::FUNGI_BEAST}, 2},
    {MonsterEncounter::MUSHROOMS_EVENT, 0, 0,
     {MonsterId::FUNGI_BEAST, MonsterId::FUNGI_BEAST, MonsterId::FUNGI_BEAST}, 3},
    {MonsterEncounter::LAGAVULIN_EVENT, 0, 0, {MonsterId::LAGAVULIN}, 1},
    {MonsterEncounter::THE_GUARDIAN, 0, 0, {MonsterId::THE_GUARDIAN}, 1},
    {MonsterEncounter::HEXAGHOST, 0, 0, {MonsterId::HEXAGHOST}, 1},
    {MonsterEncounter::SLIME_BOSS, 0, 0, {MonsterId::SLIME_BOSS}, 1},
    // Native slots are stable target indexes; a split can leave an empty slot.
    {MonsterEncounter::SLIME_BOSS, 0, 0,
     {MonsterId::SPIKE_SLIME_L, MonsterId::INVALID, MonsterId::ACID_SLIME_L}, 3},
    {MonsterEncounter::SLIME_BOSS, 0, 0,
     {MonsterId::SPIKE_SLIME_M, MonsterId::SPIKE_SLIME_M, MonsterId::ACID_SLIME_L,
      MonsterId::INVALID}, 4},
    {MonsterEncounter::SLIME_BOSS, 0, 0,
     {MonsterId::SPIKE_SLIME_L, MonsterId::INVALID, MonsterId::ACID_SLIME_M,
      MonsterId::ACID_SLIME_M}, 4},
    {MonsterEncounter::SLIME_BOSS, 0, 0,
     {MonsterId::SPIKE_SLIME_M, MonsterId::SPIKE_SLIME_M, MonsterId::ACID_SLIME_M,
      MonsterId::ACID_SLIME_M}, 4},
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
    // Generate product/permutation families without an individually written row
    // for every color/order. Still validate exact public target-slot composition.
    for (const auto a : {MonsterId::GREEN_LOUSE, MonsterId::RED_LOUSE}) {
        for (const auto b : {MonsterId::GREEN_LOUSE, MonsterId::RED_LOUSE}) {
            for (const auto c : {MonsterId::GREEN_LOUSE, MonsterId::RED_LOUSE}) {
                recipes.push_back({MonsterEncounter::THREE_LOUSE, 0, 0, {a, b, c}, 3});
            }
        }
    }
    std::array<MonsterId, 5> slimes {MonsterId::ACID_SLIME_S, MonsterId::ACID_SLIME_S,
        MonsterId::SPIKE_SLIME_S, MonsterId::SPIKE_SLIME_S, MonsterId::SPIKE_SLIME_S};
    std::sort(slimes.begin(), slimes.end());
    do {
        recipes.push_back({MonsterEncounter::LOTS_OF_SLIMES, 0, 0, slimes, 5});
    } while (std::next_permutation(slimes.begin(), slimes.end()));
    for (const auto strong : {MonsterId::FUNGI_BEAST, MonsterId::JAW_WORM}) {
        for (const auto weak : {MonsterId::GREEN_LOUSE, MonsterId::RED_LOUSE,
                               MonsterId::ACID_SLIME_M, MonsterId::SPIKE_SLIME_M}) {
            recipes.push_back({MonsterEncounter::EXORDIUM_WILDLIFE, 0, 0, {strong, weak}, 2});
        }
    }
    for (const auto weak : {MonsterId::GREEN_LOUSE, MonsterId::RED_LOUSE,
                           MonsterId::ACID_SLIME_M, MonsterId::SPIKE_SLIME_M}) {
        for (const auto strong : {MonsterId::CULTIST, MonsterId::BLUE_SLAVER,
                                 MonsterId::RED_SLAVER, MonsterId::LOOTER}) {
            recipes.push_back({MonsterEncounter::EXORDIUM_THUGS, 0, 0, {weak, strong}, 2});
        }
    }
    // Ordered draws without replacement from a multiset. Generate all354 legal
    // four-slot compositions; repeated archetypes still represent distinct foes.
    const std::array<MonsterId, 5> pool {MonsterId::MAD_GREMLIN, MonsterId::SNEAKY_GREMLIN,
        MonsterId::FAT_GREMLIN, MonsterId::SHIELD_GREMLIN, MonsterId::GREMLIN_WIZARD};
    std::array<int, 5> remaining {2, 2, 2, 1, 1};
    std::array<MonsterId, 5> slots {};
    auto enumerate = [&](auto &&self, int depth) -> void {
        if (depth == 4) {
            recipes.push_back({MonsterEncounter::GREMLIN_GANG, 0, 0, slots, 4});
            return;
        }
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (remaining[i] == 0) continue;
            --remaining[i]; slots[depth] = pool[i];
            self(self, depth + 1); ++remaining[i];
        }
    };
    enumerate(enumerate, 0);
    // Summons keep target slots, including empty and dead prior occupants.
    const std::array<MonsterId, 6> gremlins {MonsterId::INVALID, MonsterId::MAD_GREMLIN,
        MonsterId::SNEAKY_GREMLIN, MonsterId::FAT_GREMLIN, MonsterId::SHIELD_GREMLIN,
        MonsterId::GREMLIN_WIZARD};
    for (auto a : gremlins) for (auto b : gremlins) for (auto c : gremlins)
        recipes.push_back({ME::GREMLIN_LEADER, 0, 0, {a,b,c,MonsterId::GREMLIN_LEADER}, 4});
    for (auto a : {MonsterId::INVALID, MonsterId::BRONZE_ORB})
        for (auto b : {MonsterId::INVALID, MonsterId::BRONZE_ORB})
            recipes.push_back({ME::AUTOMATON, 0, 0, {a,MonsterId::BRONZE_AUTOMATON,b}, 3});
    for (auto a : {MonsterId::INVALID, MonsterId::TORCH_HEAD})
        for (auto b : {MonsterId::INVALID, MonsterId::TORCH_HEAD})
            recipes.push_back({ME::COLLECTOR, 0, 0, {a,b,MonsterId::THE_COLLECTOR}, 3});
    const std::array<MonsterId,2> daggers {MonsterId::INVALID,MonsterId::DAGGER};
    for (auto a : daggers) for (auto b : daggers) for (auto c : daggers) for (auto d : daggers)
        recipes.push_back({ME::REPTOMANCER, 0, 0, {a,b,MonsterId::REPTOMANCER,c,d}, 5});
    const std::array<MonsterId,3> shapes {MonsterId::REPULSOR,MonsterId::EXPLODER,MonsterId::SPIKER};
    for (auto a : shapes) for (auto b : shapes) {
        recipes.push_back({ME::SPHERE_AND_TWO_SHAPES, 0, 0, {a,b,MonsterId::SPHERIC_GUARDIAN}, 3});
        for (auto c : shapes) {
            if (!(a==b && b==c)) recipes.push_back({ME::THREE_SHAPES, 0, 0, {a,b,c}, 3});
            for (auto d : shapes) {
                const std::array<MonsterId,5> group {a,b,c,d};
                bool valid=true;
                for (auto id : shapes) if (std::count(group.begin(),group.begin()+4,id)>2) valid=false;
                if (valid) recipes.push_back({ME::FOUR_SHAPES, 0, 0, group, 4});
            }
        }
    }
    // All declared compositions are verified through A10. Keep the existing
    // wider opening-encounter coverage; A11+ remains gated for other recipes.
    for (auto &recipe : recipes) {
        if (recipe.minimumAscension == 0 && recipe.maximumAscension == 0)
            recipe.maximumAscension = 10;
    }
    return recipes;
}();

inline const MonsterBehaviorRecipe *findMonsterBehaviorRecipe(MonsterId id) {
    for (const auto &recipe : monsterBehaviorRecipes) {
        if (recipe.id == id) return &recipe;
    }
    return nullptr;
}

inline bool recipeContainsMove(const MonsterBehaviorRecipe &recipe, MMID move) {
    for (int idx = 0; idx < recipe.moveCount; ++idx) {
        if (*(recipe.moves.begin() + idx) == move) return true;
    }
    return false;
}

inline bool compositionMatches(
    const BattleContext &battle, const EncounterCompositionRecipe &recipe
) {
    if (battle.monsters.monsterCount != recipe.monsterCount) return false;
    for (int idx = 0; idx < recipe.monsterCount; ++idx) {
        if (battle.monsters.arr[idx].id != recipe.monsters[idx]) return false;
        if (recipe.monsters[idx] == MonsterId::INVALID &&
            (!battle.monsters.arr[idx].isDeadOrEscaped() ||
             battle.monsters.arr[idx].moveHistory[0] != MMID::INVALID)) return false;
    }
    return true;
}

inline void validateEncounterRecipe(const BattleContext &battle,
                                    const PublicEnemyHistory *enemyHistory = nullptr) {
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
        throw std::runtime_error("encounter has no public resampling recipe in v11");
    }
    if (!ascensionDeclared) {
        throw std::runtime_error("encounter ascension is outside its public recipe in v11");
    }
    if (!compositionDeclared) {
        throw std::runtime_error("encounter composition does not match its public recipe");
    }

    for (int idx = 0; idx < battle.monsters.monsterCount; ++idx) {
        const auto &monster = battle.monsters.arr[idx];
        if (monster.id == MonsterId::INVALID) continue; // validated empty composition slot
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
            current != recipe->requiredRevealingMove &&
            !(enemyHistory != nullptr && enemyHistory->isCurrent(battle))) {
            throw std::runtime_error("hidden monster damage has not been publicly revealed");
        }
    }
}

// Approximate continuation sampling, not exact conditioning on public history.
// Caller must reject roots with unsupported previously revealed order constraints.
inline void resampleCombatContinuation(
    const GameContext &game, BattleContext &battle,
    const std::array<std::uint64_t, 7> &seeds, int knownDrawTopUniqueId = -1,
    const PublicEnemyHistory *enemyHistory = nullptr
) {
    if (game.screenState != ScreenState::BATTLE ||
        battle.outcome != Outcome::UNDECIDED || battle.inputState != InputState::PLAYER_NORMAL ||
        battle.actionQueue.size != 0 || battle.cardQueue.size != 0 ||
        !battle.cards.pendingDiscardCostResets.empty()) {
        throw std::runtime_error("combat sampling requires a resolved PLAYER_NORMAL decision");
    }
    if (game.cc != CharacterClass::IRONCLAD) {
        throw std::runtime_error("combat resampler v11 supports Ironclad only");
    }
    if (game.hasRelic(RelicId::RUNIC_DOME)) {
        throw std::runtime_error("Runic Dome hides enemy intents");
    }
    if (needsPublicEnemyHistory(battle.encounter) ||
        (enemyHistory != nullptr && tracksPublicEnemyHistory(battle.encounter))) {
        if (enemyHistory == nullptr) throw std::runtime_error("public enemy history is required");
        enemyHistory->reconstruct(battle, seeds[3]);
    }
    validateEncounterRecipe(battle, enemyHistory);
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
    const auto &top = battle.cards.publicKnownTopIds;
    if (battle.cards.publicTopOrderUncertain) throw std::runtime_error("top order after random insertion is unsupported");
    if (top.size() > pile.size()) throw std::runtime_error("invalid known top size");
    for (std::size_t i = 0; i < top.size(); ++i) {
        if (pile[pile.size()-top.size()+i].uniqueId != top[i]) throw std::runtime_error("known top identity mismatch");
    }
    auto unknownEnd = pile.begin() + std::max(bottom.size(), pile.size()-top.size());
    if (top.empty() && knownDrawTopUniqueId >= 0 && unknownBegin != unknownEnd) {
        --unknownEnd;
    }
    const auto &opening = battle.cards.publicOpeningPriorityIds;
    if (battle.cards.publicOpeningOrderUncertain) {
        throw std::runtime_error("opening order after random insertion is unsupported");
    }
    if (!std::is_sorted(opening.begin(), opening.end()) ||
        std::adjacent_find(opening.begin(), opening.end()) != opening.end()) {
        throw std::runtime_error("invalid public opening-priority identities");
    }
    auto isOpening = [&](const CardInstance &c) {
        return std::binary_search(opening.begin(), opening.end(), int(c.uniqueId));
    };
    for (int uid : opening) {
        if (uid < 0 || std::count_if(pile.begin(), pile.end(),
                [&](const CardInstance &c) { return c.uniqueId == uid; }) != 1 ||
            std::find(bottom.begin(), bottom.end(), uid) != bottom.end()) {
            throw std::runtime_error("opening-priority identity not in its public draw segment");
        }
    }
    const auto priorityCount = std::count_if(unknownBegin, unknownEnd, isOpening);
    auto priorityBegin = unknownEnd - priorityCount;
    for (auto it = unknownBegin; it != unknownEnd; ++it) {
        if (isOpening(*it) != (it >= priorityBegin)) {
            throw std::runtime_error("public opening-priority group is not a draw suffix");
        }
    }
    battle.aiRng = Random(seeds[0]);
    battle.cardRandomRng = Random(seeds[1]);
    battle.miscRng = Random(seeds[2]);
    battle.monsterHpRng = Random(seeds[3]);
    battle.potionRng = Random(seeds[4]);
    battle.shuffleRng = Random(seeds[5]);
    if (!frozenEye) {
        const auto canonical = [](const CardInstance &a, const CardInstance &b) {
            return std::tie(a.id, a.upgraded, a.specialData, a.cost, a.costForTurn,
                            a.freeToPlayOnce, a.retain, a.uniqueId) <
                   std::tie(b.id, b.upgraded, b.specialData, b.cost, b.costForTurn,
                            b.freeToPlayOnce, b.retain, b.uniqueId);
        };
        // Independently permute the ordinary and opening-priority bands. Known
        // top/bottom placements stay fixed; no original hidden order is retained.
        std::sort(unknownBegin, priorityBegin, canonical);
        std::sort(priorityBegin, unknownEnd, canonical);
        java::Random orderRng(seeds[6]);
        // Collections::shuffle copies its RNG. Share one advancing local stream
        // here so equal-sized bands do not receive identical permutations.
        auto shuffleBand = [&](auto begin, auto end) {
            for (int n = static_cast<int>(end - begin); n > 1; --n) {
                std::iter_swap(begin + n - 1, begin + orderRng.nextInt(n));
            }
        };
        shuffleBand(unknownBegin, priorityBegin);
        shuffleBand(priorityBegin, unknownEnd);
    }
}
}
#endif
