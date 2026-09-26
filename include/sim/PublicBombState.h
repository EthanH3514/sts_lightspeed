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

// Preserve packet order and the acquired position relative to Combust only.
// This is not a general ordering model for all end-of-turn powers.
inline std::vector<PendingBomb> pendingBombs(const Player &player) {
    std::vector<PendingBomb> result;
    int remaining = 1;
    const std::size_t prefixes[] = {player.bomb1BeforeCombust, player.bomb2BeforeCombust, player.bomb3BeforeCombust};
    for (const auto *bucket : {&player.bomb1, &player.bomb2, &player.bomb3}) {
        for (std::size_t i = 0; i < bucket->size(); ++i) {
            result.push_back({remaining, (*bucket)[i], !player.hasStatus<PS::COMBUST>() || i < prefixes[remaining - 1]});
        }
        ++remaining;
    }
    return result;
}
}
#endif
