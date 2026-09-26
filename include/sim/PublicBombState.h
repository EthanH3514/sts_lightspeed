#ifndef STS_PUBLIC_BOMB_STATE_H
#define STS_PUBLIC_BOMB_STATE_H
#include <vector>
#include "combat/Player.h"

namespace sts::public_state {
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
