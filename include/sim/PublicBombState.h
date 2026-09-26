#ifndef STS_PUBLIC_BOMB_STATE_H
#define STS_PUBLIC_BOMB_STATE_H
#include <vector>
#include "combat/Player.h"

namespace sts::public_state {
struct PendingBomb {
    int remainingTurns;
    int damage;
};

// Preserve each packet and its order within a countdown bucket. This is not
// a reconstruction of acquisition order relative to other player powers.
inline std::vector<PendingBomb> pendingBombs(const Player &player) {
    std::vector<PendingBomb> result;
    int remaining = 1;
    for (const auto *bucket : {&player.bomb1, &player.bomb2, &player.bomb3}) {
        for (const int damage : *bucket) result.push_back({remaining, damage});
        ++remaining;
    }
    return result;
}
}
#endif
