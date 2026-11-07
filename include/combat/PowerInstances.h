#ifndef STS_POWER_INSTANCES_H
#define STS_POWER_INSTANCES_H
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include "constants/PlayerStatusEffects.h"

namespace sts {
enum class PowerPhase : unsigned {
    All = 0, StatusDraw = 1, Exhaust = 2, End = 4, Start = 8,
    PostDraw = 16, PreEndCards = 32, UseCard = 64, Attacked = 128
};
constexpr PowerPhase operator|(PowerPhase a, PowerPhase b) {
    return static_cast<PowerPhase>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool hasPhase(PowerPhase phases, PowerPhase phase) {
    return phase == PowerPhase::All ||
        (static_cast<unsigned>(phases) & static_cast<unsigned>(phase)) != 0;
}
struct PowerSpec {
    PlayerStatus type;
    PowerPhase phase;
    int priority;
    bool independent;
};
inline const PowerSpec *reviewedPowerSpec(PlayerStatus type) {
    // Callback phases and ordering priorities, not card/combo whitelists.
    // AbstractPower defaults to priority 5; ties retain acquisition order.
    static const PowerSpec specs[] = {
        {PlayerStatus::EVOLVE, PowerPhase::StatusDraw, 5, false},
        {PlayerStatus::FIRE_BREATHING, PowerPhase::StatusDraw, 5, false},
        {PlayerStatus::DARK_EMBRACE, PowerPhase::Exhaust, 5, false},
        {PlayerStatus::FEEL_NO_PAIN, PowerPhase::Exhaust, 5, false},
        {PlayerStatus::COMBUST, PowerPhase::End, 5, false},
        {PlayerStatus::THE_BOMB, PowerPhase::End, 5, true},
        {PlayerStatus::BRUTALITY, PowerPhase::PostDraw, 5, false},
        {PlayerStatus::DEMON_FORM, PowerPhase::PostDraw, 5, false},
        {PlayerStatus::DRAW_CARD_NEXT_TURN, PowerPhase::PostDraw, 20, false},
        {PlayerStatus::MAGNETISM, PowerPhase::Start, 5, false},
        {PlayerStatus::MAYHEM, PowerPhase::Start, 5, false},
        {PlayerStatus::FLAME_BARRIER, PowerPhase::Start | PowerPhase::Attacked, 5, false},
        {PlayerStatus::NEXT_TURN_BLOCK, PowerPhase::Start, 5, false},
        {PlayerStatus::PANACHE, PowerPhase::Start | PowerPhase::UseCard, 5, false},
        {PlayerStatus::METALLICIZE, PowerPhase::PreEndCards, 5, false},
        {PlayerStatus::PLATED_ARMOR, PowerPhase::PreEndCards, 5, false},
        {PlayerStatus::CONSTRICTED, PowerPhase::End, 105, false},
        {PlayerStatus::LOSE_DEXTERITY, PowerPhase::End, 5, false},
        {PlayerStatus::LOSE_STRENGTH, PowerPhase::End, 5, false},
        {PlayerStatus::NO_DRAW, PowerPhase::End, 5, false},
        {PlayerStatus::ENTANGLED, PowerPhase::End, 5, false},
        {PlayerStatus::REGEN, PowerPhase::End, 5, false},
        {PlayerStatus::RITUAL, PowerPhase::End, 5, false},
        {PlayerStatus::RAGE, PowerPhase::End | PowerPhase::UseCard, 5, false},
        {PlayerStatus::DOUBLE_TAP, PowerPhase::End | PowerPhase::UseCard, 5, false},
        {PlayerStatus::DUPLICATION, PowerPhase::UseCard, 5, false},
        {PlayerStatus::HEX, PowerPhase::UseCard, 5, false},
        {PlayerStatus::THORNS, PowerPhase::Attacked, 5, false},
    };
    for (const auto &spec : specs) if (spec.type == type) return &spec;
    return nullptr;
}
struct PowerInstance {
    PlayerStatus type;
    PowerPhase phase;
    int priority;
    std::uint64_t acquired;
    // Zero is persistent; positive values describe independently timed effects.
    int remainingTurns = 0;
    int payload = 0;
};
class PowerInstances {
    std::vector<PowerInstance> entries;
    std::uint64_t nextAcquired = 0;

    std::uint64_t insert(const PowerSpec &spec, int turns, int payload) {
        if (nextAcquired == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("power acquisition identity exhausted");
        const auto id = nextAcquired++;
        entries.push_back({spec.type, spec.phase, spec.priority, id, turns, payload});
        return id;
    }
public:
    bool has(PlayerStatus type) const {
        return std::any_of(entries.begin(), entries.end(), [=](const auto &p) { return p.type == type; });
    }
    void activate(PlayerStatus type) {
        const auto *spec = reviewedPowerSpec(type);
        if (!spec || spec->independent || has(type)) return;
        insert(*spec, 0, 0); // Stacking never changes an existing instance's position.
    }
    std::uint64_t add(PlayerStatus type, int turns, int payload) {
        const auto *spec = reviewedPowerSpec(type);
        if (!spec || !spec->independent || turns <= 0 || payload <= 0)
            throw std::invalid_argument("invalid independent power instance");
        return insert(*spec, turns, payload);
    }
    void remove(PlayerStatus type) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                      [=](const auto &p) { return p.type == type; }), entries.end());
    }
    void removeInstance(std::uint64_t id) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                      [=](const auto &p) { return p.acquired == id; }), entries.end());
    }
    static void sort(std::vector<PowerInstance> &powers) {
        std::stable_sort(powers.begin(), powers.end(), [](const auto &a, const auto &b) {
            return a.priority != b.priority ? a.priority < b.priority : a.acquired < b.acquired;
        });
    }
    std::vector<PowerInstance> ordered(PowerPhase phase = PowerPhase::All) const {
        std::vector<PowerInstance> result;
        for (const auto &p : entries)
            if (hasPhase(p.phase, phase)) result.push_back(p);
        sort(result);
        return result; // Snapshot: callbacks may queue mutations without invalidating traversal.
    }
    bool before(PlayerStatus a, PlayerStatus b) const {
        const auto powers = ordered();
        auto ia = std::find_if(powers.begin(), powers.end(), [=](const auto &p) { return p.type == a; });
        auto ib = std::find_if(powers.begin(), powers.end(), [=](const auto &p) { return p.type == b; });
        return ia != powers.end() && ib != powers.end() && ia < ib;
    }
    void tick(PowerPhase phase) {
        for (auto it = entries.begin(); it != entries.end();) {
            if (hasPhase(it->phase, phase) && it->remainingTurns > 0 && --it->remainingTurns == 0)
                it = entries.erase(it);
            else ++it;
        }
    }
};
}
#endif
