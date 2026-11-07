#ifndef STS_PUBLIC_BOMB_STATE_H
#define STS_PUBLIC_BOMB_STATE_H
#include <vector>
#include "combat/Player.h"
#include "combat/BattleContext.h"

namespace sts::public_state {
constexpr int bombStateSchema = 1;
inline bool directPower(PS status) {
    return status == PS::ARTIFACT || status == PS::DEXTERITY ||
            status == PS::STRENGTH || status == PS::FOCUS;
}
// Active bits, not statusMap membership, govern legacy power lifetime. Expiry
// and removeStatus intentionally retain numeric slots (including positive ones).
// A genuinely active bit-only power still cannot attest a complete public list.
inline bool powersComplete(const Player &player) {
    for (int i = 1; i < static_cast<int>(PS::THE_BOMB); ++i) {
        const auto status = static_cast<PS>(i);
        if (directPower(status)) continue;
        const bool active = player.hasStatusRuntime(status);
        if (reviewedPowerSpec(status) && player.powerInstances.has(status) != active) return false;
        if (active && player.statusMap.count(status) == 0) return false;
    }
    return true;
}
struct PlayerPowerRecord {
    PS type;
    int amount;
    int damage = 0;
    int hpLoss = 0;
};
// Shared by the Python binding and the native search projection. Preserve the
// reviewed registry's acquisition order and independent Bomb instances; direct
// fields are authoritative even if a legacy numeric slot exists for that type.
inline std::vector<PlayerPowerRecord> playerPowers(const Player &player) {
    std::vector<PlayerPowerRecord> result;
    for (const auto &power : player.powerInstances.ordered()) {
        result.push_back({power.type,
            power.type == PS::THE_BOMB ? power.remainingTurns :
                power.type == PS::PANACHE ? static_cast<int>(player.panacheCounter) : player.getStatusRuntime(power.type),
            power.type == PS::THE_BOMB ? power.payload :
                power.type == PS::PANACHE ? player.getStatusRuntime(power.type) : 0,
            power.type == PS::COMBUST ? static_cast<int>(player.combustHpLoss) : 0});
    }
    for (const auto &entry : player.statusMap) {
        const auto status = entry.first;
        if (reviewedPowerSpec(status) || directPower(status) || !player.hasStatusRuntime(status)) continue;
        result.push_back({status, status == PS::PANACHE ? static_cast<int>(player.panacheCounter) : entry.second,
            status == PS::PANACHE ? entry.second : 0});
    }
    const PlayerPowerRecord direct[] = {{PS::ARTIFACT, player.artifact}, {PS::DEXTERITY, player.dexterity},
        {PS::FOCUS, player.focus}, {PS::STRENGTH, player.strength}};
    for (const auto &power : direct) if (power.amount != 0) result.push_back(power);
    return result;
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
