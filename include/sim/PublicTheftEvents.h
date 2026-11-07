#pragma once
#include <algorithm>
#include <stdexcept>
#include <vector>
#include "combat/BattleContext.h"

namespace sts::public_sampling {
inline constexpr auto publicTheftVersion = "public-theft-events-v1";

// These are visible action events, not the thief's private cumulative counter.
// A live bridge may provide the same slot/intent and before/after gold amounts.
struct PublicTheftEvent {
    int slot, generation, turn;
    MonsterId id;
    MMID intent;
    int thievery, goldBefore, goldAfter;
    int amount() const { return goldBefore - goldAfter; }
};

// A scoped observer, keyed to one battle. Owned search worlds cannot write into
// the real battle's journal; nested scopes and threads have independent sinks.
struct PublicTheftScope {
    const BattleContext* battle;
    std::vector<PublicTheftEvent>& events;
    PublicTheftScope* previous;
    static inline thread_local PublicTheftScope* active = nullptr;
    PublicTheftScope(const BattleContext& b, std::vector<PublicTheftEvent>& e)
        : battle(&b), events(e), previous(active) { active = this; }
    ~PublicTheftScope() { active = previous; }
    PublicTheftScope(const PublicTheftScope&) = delete;
    PublicTheftScope& operator=(const PublicTheftScope&) = delete;
};

inline void observePublicTheft(const BattleContext& b, int slot, int before, int after) {
    auto* sink = PublicTheftScope::active;
    if (!sink || sink->battle != &b) return;
    if (slot < 0 || slot >= b.monsters.monsterCount) throw std::runtime_error("public_theft_slot");
    const auto& m = b.monsters.arr[slot];
    sink->events.push_back({slot, m.publicGeneration, b.turn, m.id, m.moveHistory[0],
        m.getStatus<MonsterStatus::THIEVERY>(), before, after});
}
} // namespace sts::public_sampling
