#ifndef STS_PUBLIC_ENEMY_HISTORY_H
#define STS_PUBLIC_ENEMY_HISTORY_H

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <tuple>
#include "combat/BattleContext.h"
#include "PublicTheftEvents.h"
#include <optional>

namespace sts::public_sampling {

// An observation-only ledger. Never copy moveHistory[1], miscInfo, RNG, or a
// private phase counter into this frame. A bridge can supply these same public
// fields without access to a BattleContext.
struct PublicEnemySlot {
    MonsterId id = MonsterId::INVALID;
    MMID intent = MMID::INVALID;
    int attackDamage = 0;
    bool gone = true;
    int thievery = 0;
    int attackHits = 1;
    bool halfDead = false;
    int generation = 0;

    bool operator==(const PublicEnemySlot &other) const {
        return std::tie(id, intent, attackDamage, gone, thievery, attackHits, halfDead, generation) ==
               std::tie(other.id, other.intent, other.attackDamage, other.gone, other.thievery, other.attackHits, other.halfDead, other.generation);
    }
};

struct PublicEnemyFrame {
    MonsterEncounter encounter = MonsterEncounter::INVALID;
    int ascension = 0, turn = 0, count = 0;
    int playerGold = 0;
    std::array<PublicEnemySlot, 5> slots {};

    bool operator==(const PublicEnemyFrame &other) const {
        return std::tie(encounter, ascension, turn, count, playerGold, slots) ==
               std::tie(other.encounter, other.ascension, other.turn, other.count, other.playerGold, other.slots);
    }
};

inline bool extendedPublicEncounter(MonsterEncounter encounter) {
    switch (encounter) {
        case MonsterEncounter::GREMLIN_LEADER:
        case MonsterEncounter::AUTOMATON:
        case MonsterEncounter::COLLECTOR:
        case MonsterEncounter::CHAMP:
        case MonsterEncounter::THREE_DARKLINGS:
        case MonsterEncounter::ORB_WALKER:
        case MonsterEncounter::THREE_SHAPES:
        case MonsterEncounter::SPIRE_GROWTH:
        case MonsterEncounter::TRANSIENT:
        case MonsterEncounter::FOUR_SHAPES:
        case MonsterEncounter::MAW:
        case MonsterEncounter::SPHERE_AND_TWO_SHAPES:
        case MonsterEncounter::JAW_WORM_HORDE:
        case MonsterEncounter::WRITHING_MASS:
        case MonsterEncounter::GIANT_HEAD:
        case MonsterEncounter::NEMESIS:
        case MonsterEncounter::REPTOMANCER:
        case MonsterEncounter::AWAKENED_ONE:
        case MonsterEncounter::TIME_EATER:
        case MonsterEncounter::DONU_AND_DECA:
        case MonsterEncounter::SHIELD_AND_SPEAR:
        case MonsterEncounter::THE_HEART:
        case MonsterEncounter::COLOSSEUM_EVENT_SLAVERS:
        case MonsterEncounter::COLOSSEUM_EVENT_NOBS:
        case MonsterEncounter::MASKED_BANDITS_EVENT:
        case MonsterEncounter::MYSTERIOUS_SPHERE_EVENT:
            return true;
        default: return false;
    }
}

inline bool needsPublicEnemyHistory(MonsterEncounter encounter) {
    if (extendedPublicEncounter(encounter)) return true;
    return encounter == MonsterEncounter::THE_GUARDIAN ||
           encounter == MonsterEncounter::HEXAGHOST ||
           encounter == MonsterEncounter::SLIME_BOSS ||
           encounter == MonsterEncounter::LARGE_SLIME ||
           encounter == MonsterEncounter::LOTS_OF_SLIMES ||
           encounter == MonsterEncounter::THREE_LOUSE ||
           encounter == MonsterEncounter::TWO_FUNGI_BEASTS ||
           encounter == MonsterEncounter::EXORDIUM_WILDLIFE ||
           encounter == MonsterEncounter::LAGAVULIN_EVENT ||
           encounter == MonsterEncounter::MUSHROOMS_EVENT ||
           encounter == MonsterEncounter::BLUE_SLAVER ||
           encounter == MonsterEncounter::RED_SLAVER ||
           encounter == MonsterEncounter::LOOTER ||
           encounter == MonsterEncounter::GREMLIN_GANG ||
           encounter == MonsterEncounter::EXORDIUM_THUGS ||
           encounter == MonsterEncounter::SPHERIC_GUARDIAN ||
           encounter == MonsterEncounter::SHELL_PARASITE ||
           encounter == MonsterEncounter::SNAKE_PLANT ||
           encounter == MonsterEncounter::SENTRY_AND_SPHERE ||
           encounter == MonsterEncounter::SHELLED_PARASITE_AND_FUNGI ||
           encounter == MonsterEncounter::THREE_CULTIST ||
           encounter == MonsterEncounter::CHOSEN ||
           encounter == MonsterEncounter::THREE_BYRDS ||
           encounter == MonsterEncounter::CHOSEN_AND_BYRDS ||
           encounter == MonsterEncounter::CULTIST_AND_CHOSEN ||
           encounter == MonsterEncounter::CENTURION_AND_HEALER ||
           encounter == MonsterEncounter::SNECKO ||
           encounter == MonsterEncounter::TWO_THIEVES ||
           encounter == MonsterEncounter::SLAVERS ||
           encounter == MonsterEncounter::BOOK_OF_STABBING;
}

inline bool tracksPublicEnemyHistory(MonsterEncounter encounter) {
    // Legacy direct native callers can still sample a currently revealed double
    // Louse root without a ledger. SimulatorSession always records its history.
    return needsPublicEnemyHistory(encounter) || encounter == MonsterEncounter::TWO_LOUSE;
}

inline bool isLouse(MonsterId id) {
    return id == MonsterId::GREEN_LOUSE || id == MonsterId::RED_LOUSE;
}

inline bool isLouseBite(MMID intent) {
    return intent == MMID::GREEN_LOUSE_BITE || intent == MMID::RED_LOUSE_BITE;
}

inline bool isThief(MonsterId id) {
    return id == MonsterId::LOOTER || id == MonsterId::MUGGER;
}

inline bool isTheftIntent(MMID intent) {
    return intent == MMID::LOOTER_MUG || intent == MMID::LOOTER_LUNGE ||
           intent == MMID::MUGGER_MUG || intent == MMID::MUGGER_LUNGE;
}

inline PublicEnemyFrame publicEnemyFrame(const BattleContext &battle) {
    PublicEnemyFrame frame;
    frame.encounter = battle.encounter;
    frame.ascension = battle.ascension;
    frame.turn = battle.turn;
    frame.count = battle.monsters.monsterCount;
    frame.playerGold = battle.player.gold;
    if (frame.count < 1 || frame.count > 5) {
        throw std::runtime_error("public enemy slot count is invalid");
    }
    for (int i = 0; i < frame.count; ++i) {
        const auto &m = battle.monsters.arr[i];
        frame.slots[i] = {m.id, m.moveHistory[0],
                         m.getMoveBaseDamage(battle).damage, m.isDeadOrEscaped(),
                         m.getStatus<MS::THIEVERY>(), m.getMoveBaseDamage(battle).attackCount,
                         m.halfDead, m.publicGeneration};
    }
    return frame;
}

inline bool isSlimeSplit(MMID intent) {
    return intent == MMID::SLIME_BOSS_SPLIT || intent == MMID::ACID_SLIME_L_SPLIT ||
           intent == MMID::SPIKE_SLIME_L_SPLIT;
}

inline bool fixedMoveHistory(MonsterId id) {
    // These native implementations do not call setMove for an unchanged intent.
    return id == MonsterId::SPIKE_SLIME_S || id == MonsterId::CULTIST ||
           id == MonsterId::MAD_GREMLIN || id == MonsterId::FAT_GREMLIN ||
           id == MonsterId::SNEAKY_GREMLIN || id == MonsterId::SHIELD_GREMLIN ||
           id == MonsterId::GREMLIN_WIZARD || id == MonsterId::TASKMASTER ||
           id == MonsterId::POINTY || id == MonsterId::TORCH_HEAD ||
           id == MonsterId::TRANSIENT;
}

class PublicEnemyHistory {
    PublicEnemyFrame last {};
    std::array<MMID, 5> previous {};
    std::array<int, 5> revealedAttackDamage {};
    std::array<int, 5> phaseCounter {};
    std::array<int, 5> stolenGold {};
    std::string failureReason;
    unsigned theftCorrections = 0, theftEvents = 0;
    int guardianThreshold = 30;
    bool initialized = false, complete = false;

public:
    unsigned publicTheftCorrections() const { return theftCorrections; }
    unsigned publicTheftEvents() const { return theftEvents; }
    void observe(const PublicEnemyFrame &frame, const std::vector<PublicTheftEvent>* events = nullptr) {
        std::optional<PublicEnemyHistory> legacy;
        if(events && initialized && (frame.turn != last.turn || !events->empty())) {
            legacy = *this; legacy->observe(frame);
        }
        if(events && !initialized && !events->empty()) throw std::runtime_error("theft_before_public_anchor");
        if (!tracksPublicEnemyHistory(frame.encounter)) {
            *this = PublicEnemyHistory();
            return;
        }
        if (frame.count < 1 || frame.count > 5) {
            complete = false;
            return;
        }
        if (!initialized) {
            initialized = true;
            // Encounter admission enforces its own ascension range. Double
            // Louse keeps A0-20; the remaining compositions are admitted through A10.
            complete = frame.turn == 0 && frame.ascension >= 0 && frame.ascension <= 20;
            guardianThreshold = frame.ascension >= 19 ? 40 : frame.ascension >= 9 ? 35 : 30;
            previous.fill(MMID::INVALID);
            revealedAttackDamage.fill(-1);
            for (int i = 0; i < frame.count; ++i) {
                // Wizard visibly begins its first charge cycle at one.
                phaseCounter[i] = frame.slots[i].id == MonsterId::GREMLIN_WIZARD ? 1 : 0;
                if (frame.slots[i].id == MonsterId::BOOK_OF_STABBING) {
                    // A0: starts at one, increments when Multi Stab is announced
                    // (including the opening intent), not on Single Stab turns.
                    phaseCounter[i] = 1 + (frame.slots[i].intent == MMID::BOOK_OF_STABBING_MULTI_STAB);
                }
                if (frame.slots[i].id == MonsterId::THE_CHAMP) {
                    phaseCounter[i] = frame.slots[i].intent == MMID::THE_CHAMP_DEFENSIVE_STANCE ? 1 :
                        frame.slots[i].intent == MMID::THE_CHAMP_ANGER ? 4 : 0;
                }
            }
        } else {
            if (frame.encounter != last.encounter || frame.ascension != last.ascension ||
                frame.turn < last.turn || frame.turn > last.turn + 1) complete = false;
            const bool nextTurn = frame.turn == last.turn + 1;
            // Enemy actions execute in target-slot order. Gold available to the
            // second thief excludes what the first one has already stolen.
            int goldBeforeNextThief = last.playerGold;
            for (int i = 0; i < frame.count; ++i) {
                const auto &old = last.slots[i];
                const auto &now = frame.slots[i];
                if (now.generation < 0 || now.generation < old.generation) complete = false;
                if (i >= last.count || old.id != now.id || old.generation != now.generation) {
                    // A replacement is a new individual, even in a reused slot.
                    previous[i] = MMID::INVALID;
                    revealedAttackDamage[i] = -1;
                    phaseCounter[i] = now.id == MonsterId::GREMLIN_WIZARD ? 1 : 0;
                    stolenGold[i] = 0;
                } else if (nextTurn && (!old.gone || old.halfDead)) {
                    // A permanently dead/escaped slot can retain its last intent:
                    // a new player turn alone does not announce another move.
                    // Preserve changed announcements and half-dead/revival paths.
                    // Do not skip the remaining ledger: thief death can still
                    // leave future-relevant, ambiguous stolen-gold evidence.
                    const bool announced = !now.gone || now.halfDead || old.intent != now.intent;
                    if (announced && (!fixedMoveHistory(old.id) || old.intent != now.intent))
                        previous[i] = old.intent;
                    if (old.id == MonsterId::SHELLED_PARASITE &&
                        old.intent == MMID::SHELLED_PARASITE_STUNNED && !now.gone) {
                        // Original stun recovery sets a transient Fell intent
                        // before rolling. That deterministic intermediate is
                        // derivable from the observed stun, not a private cache.
                        previous[i] = MMID::SHELLED_PARASITE_FELL;
                    }
                    if (old.id == MonsterId::THE_GUARDIAN &&
                        old.intent == MMID::THE_GUARDIAN_TWIN_SLAM) guardianThreshold += 10;
                    if (old.id == MonsterId::RED_SLAVER && old.intent == MMID::RED_SLAVER_ENTANGLE &&
                        !now.gone) phaseCounter[i] = 1;
                    if (old.id == MonsterId::GREMLIN_WIZARD && !now.gone) {
                        if (old.intent == MMID::GREMLIN_WIZARD_CHARGING) ++phaseCounter[i];
                        else if (old.intent == MMID::GREMLIN_WIZARD_ULTIMATE_BLAST) phaseCounter[i] = 0;
                    }
                    if (old.id == MonsterId::BOOK_OF_STABBING && !now.gone &&
                        now.intent == MMID::BOOK_OF_STABBING_MULTI_STAB) ++phaseCounter[i];
                    // Counters below are consequences of observed announcements
                    // or executed intents, never copied from private miscInfo.
                    if (old.id == MonsterId::BRONZE_AUTOMATON &&
                        old.intent == MMID::BRONZE_AUTOMATON_BOOST) phaseCounter[i] ^= 1;
                    if ((old.id == MonsterId::BRONZE_ORB && old.intent == MMID::BRONZE_ORB_STASIS) ||
                        (old.id == MonsterId::WRITHING_MASS && old.intent == MMID::WRITHING_MASS_IMPLANT) ||
                        (old.id == MonsterId::TIME_EATER && old.intent == MMID::TIME_EATER_HASTE) ||
                        (old.id == MonsterId::AWAKENED_ONE && old.intent == MMID::AWAKENED_ONE_REBIRTH))
                        phaseCounter[i] = 1;
                    if (old.id == MonsterId::SPIKER && old.intent == MMID::SPIKER_SPIKE) ++phaseCounter[i];
                    if (old.id == MonsterId::THE_CHAMP && !now.gone) {
                        if (now.intent == MMID::THE_CHAMP_DEFENSIVE_STANCE) ++phaseCounter[i];
                        if (now.intent == MMID::THE_CHAMP_ANGER) phaseCounter[i] |= 4;
                    }
                    if (!events && isThief(old.id) && isTheftIntent(old.intent)) {
                        // No scoped end-turn effect spends/gains gold before theft;
                        // start-turn Hand of Greed gains happen AFTER it. Do not
                        // infer theft from the net gold delta (which can hide gains).
                        const int amount = std::min(goldBeforeNextThief, old.thievery);
                        if (!now.gone || now.intent != old.intent) {
                            stolenGold[i] += amount;
                            goldBeforeNextThief -= amount;
                        } else if (last.turn == 0 && amount > 0) {
                            // First Mug repeats its intent. Death somewhere in a
                            // batched turn does not reveal whether it acted. Refuse,
                            // rather than copying the native private theft counter.
                            complete = false;
                            failureReason = "ambiguous thief death during first Mug at slot " + std::to_string(i);
                        }
                    }
                } else if (old.intent != now.intent && !isSlimeSplit(now.intent)) {
                    previous[i] = old.intent;
                }
            }
        }
        for (int i = 0; i < frame.count; ++i) {
            const auto &slot = frame.slots[i];
            if (isLouse(slot.id) && isLouseBite(slot.intent)) {
                const int minimum = frame.ascension >= 2 ? 6 : 5;
                if (slot.attackDamage < minimum || slot.attackDamage > minimum + 2 ||
                    (revealedAttackDamage[i] >= 0 && revealedAttackDamage[i] != slot.attackDamage)) {
                    complete = false;
                }
                revealedAttackDamage[i] = slot.attackDamage;
            }
            if (slot.id == MonsterId::DARKLING && slot.intent == MMID::DARKLING_NIP) {
                const int minimum = frame.ascension >= 2 ? 9 : 7;
                if (slot.attackDamage < minimum || slot.attackDamage > minimum + 4 ||
                    (revealedAttackDamage[i] >= 0 && revealedAttackDamage[i] != slot.attackDamage)) {
                    complete = false;
                    failureReason = "Darkling Nip disagrees with publicly revealed birth damage";
                }
                revealedAttackDamage[i] = slot.attackDamage;
            }
            if (slot.id == MonsterId::BOOK_OF_STABBING && !slot.gone) {
                const int hits = slot.intent == MMID::BOOK_OF_STABBING_MULTI_STAB ? phaseCounter[i] : 1;
                if (slot.attackHits != hits) {
                    complete = false;
                    failureReason = "Book hit count disagrees with observed Multi Stab announcements";
                }
            }
        }
        if(events) for(const auto& e : *events) {
            if(e.slot < 0 || e.slot >= last.count || e.slot >= frame.count ||
               !isThief(e.id) || !isTheftIntent(e.intent) || e.goldBefore < 0 || e.goldAfter < 0 ||
               e.thievery < 0 || e.amount() != std::min(e.goldBefore, e.thievery) ||
               e.turn < last.turn || e.turn > frame.turn ||
               last.slots[e.slot].id != e.id || frame.slots[e.slot].id != e.id ||
               last.slots[e.slot].generation != e.generation || frame.slots[e.slot].generation != e.generation)
                throw std::runtime_error("invalid_public_theft_event");
            stolenGold[e.slot] += e.amount(); ++theftEvents;
        }
        if(legacy && (legacy->stolenGold != stolenGold || legacy->complete != complete)) ++theftCorrections;
        last = frame;
    }

    bool isCurrent(const BattleContext &battle) const {
        return initialized && complete && last == publicEnemyFrame(battle);
    }

    // This mutates only a sampled clone. The source's private caches are not
    // evidence: replace them with facts derived from observed public history.
    void reconstruct(BattleContext &battle, std::uint64_t latentSeed = 0) const {
        if (!isCurrent(battle)) {
            throw std::runtime_error("complete, current public enemy history is required (observed turn=" +
                std::to_string(last.turn) + ", current turn=" + std::to_string(battle.turn) +
                ", complete=" + std::to_string(complete) + "): " + failureReason);
        }
        if (battle.monsters.skipTurn.any() || battle.monsters.extraRollMoveOnTurn.any()) {
            throw std::runtime_error("unresolved enemy turn bookkeeping is unsupported");
        }
        if (battle.player.hasStatus<PS::SURROUNDED>() &&
            (battle.player.lastTargetedMonster < 0 ||
             battle.player.lastTargetedMonster >= battle.monsters.monsterCount)) {
            throw std::runtime_error("Surrounded public facing slot is invalid");
        }
        for (int i = 0; i < last.count; ++i) {
            auto &m = battle.monsters.arr[i];
            if (m.id == MonsterId::BRONZE_ORB && !m.isDeadOrEscaped() &&
                m.hasStatus<MS::STASIS>() != (battle.cards.stasisCards[std::min(1,i)].id != CardId::INVALID)) {
                throw std::runtime_error("Stasis public power disagrees with visibly held card");
            }
            m.moveHistory[1] = m.id == MonsterId::SPIKE_SLIME_S ? MMID::INVALID : previous[i];
            if (m.id == MonsterId::WRITHING_MASS ||
                (m.id == MonsterId::GIANT_HEAD && m.moveHistory[0] == MMID::GIANT_HEAD_IT_IS_TIME) ||
                ((m.id == MonsterId::DARKLING || m.id == MonsterId::AWAKENED_ONE) && m.halfDead)) {
                // Reactive intent changes can occur inside a batched action.
                // These phases never consult the older intent before replacing it.
                m.moveHistory[1] = MMID::INVALID;
            }
            if (m.id == MonsterId::SHELLED_PARASITE &&
                m.moveHistory[0] == MMID::SHELLED_PARASITE_STUNNED) {
                // Start-turn autoplay may interrupt a newly rolled intent before
                // it is observed. Stun recovery overwrites that history with Fell,
                // so the unobserved previous intent has no continuation effect.
                m.moveHistory[1] = MMID::INVALID;
            }
            if (m.id == MonsterId::BYRD) {
                const auto intent = m.moveHistory[0];
                const bool grounded = intent == MMID::BYRD_STUNNED ||
                    intent == MMID::BYRD_HEADBUTT || intent == MMID::BYRD_FLY;
                if (!m.isDeadOrEscaped() &&
                    (grounded == m.hasStatus<MS::FLIGHT>() ||
                     (!grounded && (m.getStatus<MS::FLIGHT>() < 1 || m.getStatus<MS::FLIGHT>() > 3)))) {
                    throw std::runtime_error("Byrd intent disagrees with public Flight state");
                }
                // Autoplay can ground it before the freshly rolled intent is
                // observed. The deterministic Stun -> Headbutt -> Fly sequence
                // overwrites that irrelevant private history before random AI.
                if (intent == MMID::BYRD_STUNNED) m.moveHistory[1] = MMID::INVALID;
            }
            if (m.id == MonsterId::CHOSEN && !m.isDeadOrEscaped()) {
                const auto intent = m.moveHistory[0];
                const bool previousDebuff = previous[i] == MMID::CHOSEN_DEBILITATE ||
                                            previous[i] == MMID::CHOSEN_DRAIN;
                const bool valid = last.turn == 0 ? intent == MMID::CHOSEN_POKE :
                    last.turn == 1 ? intent == MMID::CHOSEN_HEX :
                    previousDebuff ? intent == MMID::CHOSEN_POKE || intent == MMID::CHOSEN_ZAP :
                                     intent == MMID::CHOSEN_DEBILITATE || intent == MMID::CHOSEN_DRAIN;
                if (!valid) throw std::runtime_error("Chosen intent disagrees with public opening/cycle");
            }
            if (m.id == MonsterId::SNECKO && !m.isDeadOrEscaped()) {
                const bool glare = m.moveHistory[0] == MMID::SNECKO_PERPLEXING_GLARE;
                if (glare != (last.turn == 0)) {
                    throw std::runtime_error("Snecko intent disagrees with its once-only opening");
                }
            }
            if (m.id == MonsterId::THE_GUARDIAN) {
                m.miscInfo = guardianThreshold;
                // Previous intent is irrelevant to this deterministic cycle;
                // damage interrupts may have revealed intermediate intents.
                m.moveHistory[1] = MMID::INVALID;
            } else if (m.id == MonsterId::HEXAGHOST) {
                // Native/public turn is zero-based: Activate, Divider, then a
                // seven-turn repeating cycle. No private orb count is read.
                static constexpr MMID cycle[] {MMID::HEXAGHOST_SEAR,
                    MMID::HEXAGHOST_TACKLE, MMID::HEXAGHOST_SEAR,
                    MMID::HEXAGHOST_INFLAME, MMID::HEXAGHOST_TACKLE,
                    MMID::HEXAGHOST_SEAR, MMID::HEXAGHOST_INFERNO};
                const int phase = last.turn < 2 ? 0 : (last.turn - 2) % 7;
                const MMID expected = last.turn == 0 ? MMID::HEXAGHOST_ACTIVATE :
                    last.turn == 1 ? MMID::HEXAGHOST_DIVIDER : cycle[phase];
                if (!m.isDeadOrEscaped() && m.moveHistory[0] != expected) {
                    throw std::runtime_error("Hexaghost intent disagrees with its public cycle");
                }
                m.uniquePower0 = phase;
                m.miscInfo = m.moveHistory[0] == MMID::HEXAGHOST_DIVIDER ?
                    last.slots[i].attackDamage : 0;
                m.moveHistory[1] = MMID::INVALID;
            } else if (m.id == MonsterId::SPHERIC_GUARDIAN) {
                const auto expected = last.turn == 0 ? MMID::SPHERIC_GUARDIAN_ACTIVATE :
                    last.turn == 1 ? MMID::SPHERIC_GUARDIAN_ATTACK_DEBUFF :
                    last.turn % 2 == 0 ? MMID::SPHERIC_GUARDIAN_SLAM : MMID::SPHERIC_GUARDIAN_HARDEN;
                if (!m.isDeadOrEscaped() && m.moveHistory[0] != expected) {
                    throw std::runtime_error("Spheric Guardian intent disagrees with its public cycle");
                }
                m.miscInfo = 0;
            } else if (m.id == MonsterId::BOOK_OF_STABBING) {
                // On Single Stab the count is not visible, but all earlier
                // Multi Stab announcements were observed. Never use miscInfo.
                m.miscInfo = phaseCounter[i];
            } else if (isLouse(m.id) || m.id == MonsterId::DARKLING) {
                const bool darkling = m.id == MonsterId::DARKLING;
                const int minimum = darkling ? (last.ascension >= 2 ? 9 : 7) : last.ascension >= 2 ? 6 : 5;
                // Independent from the source monsterHpRng and its cached birth
                // damage. Domain- and slot-separated from future HP sampling.
                Random latent(Random::murmurHash3(latentSeed ^
                                                  (darkling ? 0x4441524b444d4745ULL : 0x4c4f555345444d47ULL) ^
                                                  static_cast<std::uint64_t>(i)));
                m.miscInfo = revealedAttackDamage[i] >= 0 ? revealedAttackDamage[i] :
                            latent.random(minimum, minimum + (darkling ? 4 : 2));
            } else if (m.id == MonsterId::BRONZE_AUTOMATON || m.id == MonsterId::BRONZE_ORB ||
                       m.id == MonsterId::THE_CHAMP || m.id == MonsterId::SPIKER ||
                       m.id == MonsterId::WRITHING_MASS || m.id == MonsterId::TIME_EATER ||
                       m.id == MonsterId::AWAKENED_ONE) {
                m.miscInfo = phaseCounter[i];
            } else if (m.id == MonsterId::RED_SLAVER || m.id == MonsterId::GREMLIN_WIZARD) {
                m.miscInfo = m.isDeadOrEscaped() ? 0 : phaseCounter[i];
                if (m.id == MonsterId::GREMLIN_WIZARD && !m.isDeadOrEscaped()) {
                    const auto expected = phaseCounter[i] == 3 ?
                        MMID::GREMLIN_WIZARD_ULTIMATE_BLAST : MMID::GREMLIN_WIZARD_CHARGING;
                    if (phaseCounter[i] < 0 || phaseCounter[i] > 3 || m.moveHistory[0] != expected) {
                        throw std::runtime_error("Wizard intent disagrees with observed charge cycle");
                    }
                }
            } else if (isThief(m.id)) {
                m.miscInfo = stolenGold[i];
                m.isEscapingB = last.slots[i].gone && m.curHp > 0;
            } else {
                // Other admitted monsters have no behaviorally relevant miscInfo.
                m.miscInfo = 0;
            }
            // No admitted encounter has a leader-death flee instruction. Shield
            // targets are selected by the future action, not a cached target.
            m.escapeNext = false;
        }
        if (battle.encounter == MonsterEncounter::LOOTER ||
            battle.encounter == MonsterEncounter::TWO_THIEVES ||
            battle.encounter == MonsterEncounter::EXORDIUM_THUGS) {
            bool anyTheft = false;
            for (int gold : stolenGold) anyTheft = anyTheft || gold > 0;
            battle.setRequiresStolenGoldCheck(anyTheft);
        }
    }
};

}
#endif
