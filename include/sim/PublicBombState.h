#ifndef STS_PUBLIC_BOMB_STATE_H
#define STS_PUBLIC_BOMB_STATE_H
#include <vector>
#include "combat/Player.h"
#include "combat/BattleContext.h"

namespace sts::public_state {
constexpr int bombStateSchema = 1;
// Mirror the current binding's representation sources. Missing bit-only powers
// and stale map entries must not be mistaken for a complete public power list.
inline bool powersComplete(const Player &player) {
    for (int i = 1; i < static_cast<int>(PS::THE_BOMB); ++i) {
        const auto status = static_cast<PS>(i);
        if (status == PS::ARTIFACT || status == PS::DEXTERITY ||
                status == PS::STRENGTH || status == PS::FOCUS) continue;
        const bool active = player.hasStatusRuntime(status);
        if (reviewedPowerSpec(status) && player.powerInstances.has(status) != active) return false;
        if (active != (player.statusMap.count(status) != 0)) return false;
    }
    return true;
}
// Public decision readiness only, never queue contents, identities or RNG state.
inline bool effectsResolved(const BattleContext &battle) {
    return battle.outcome == Outcome::UNDECIDED &&
            battle.inputState == InputState::PLAYER_NORMAL &&
            battle.actionQueue.size == 0 && battle.cardQueue.size == 0 &&
            battle.cards.pendingDiscardCostResets.empty();
}
struct PendingBomb {
    int remainingTurns;
    int damage;
    bool beforeCombust;
};
// Compatibility projection only: all authoritative state/order lives in the registry.
inline std::vector<PendingBomb> pendingBombs(const Player &player) {
    std::vector<PendingBomb> result;
    bool seenCombust = false;
    for (const auto &power : player.powerInstances.ordered()) {
        if (power.type == PlayerStatus::COMBUST) seenCombust = true;
        if (power.type == PlayerStatus::THE_BOMB)
            result.push_back({power.remainingTurns, power.payload, !seenCombust});
    }
    return result;
}
}
#endif
