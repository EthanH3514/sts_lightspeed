#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

int main() {
    // Three-way order is a property of instances, not pairwise boolean flags.
    std::array<PS, 3> types = {PS::EVOLVE, PS::DARK_EMBRACE, PS::COMBUST};
    std::sort(types.begin(), types.end());
    do {
        PowerInstances registry;
        for (const auto type : types) registry.activate(type);
        const auto snapshot = registry.ordered();
        for (int i = 0; i < 3; ++i) check(snapshot[i].type == types[i], "equal priorities preserve all acquisition permutations");
        registry.activate(types[0]);
        check(registry.ordered()[0].acquired == snapshot[0].acquired, "stacking preserves identity and position");
        registry.remove(types[0]); registry.activate(types[0]);
        check(registry.ordered().back().type == types[0] && registry.ordered().back().acquired > snapshot.back().acquired,
              "remove/reacquire allocates a new position");
        check(snapshot[0].type == types[0], "previous callback snapshot is immutable");
    } while (std::next_permutation(types.begin(), types.end()));

    std::vector<PowerInstance> priorities = {
        {PS::CONSTRICTED, PowerPhase::End, 105, 0},
        {PS::COMBUST, PowerPhase::End, 5, 2},
        {PS::THE_BOMB, PowerPhase::End, 5, 1},
    };
    PowerInstances::sort(priorities);
    check(priorities[0].type == PS::THE_BOMB && priorities[1].type == PS::COMBUST &&
          priorities[2].type == PS::CONSTRICTED, "explicit priority precedes acquisition order");
    check(!reviewedPowerSpec(PS::CONSTRICTED), "sorting support does not automatically admit unreviewed callbacks");

    Fixture f;
    f.b.player.buff<PS::EVOLVE>(1); f.b.player.buff<PS::FIRE_BREATHING>(6);
    f.b.player.buff<PS::DARK_EMBRACE>(1); f.b.player.buff<PS::FEEL_NO_PAIN>(3);
    f.play(); f.b.player.buff<PS::COMBUST>(5); f.play(true);
    check(f.b.player.powerInstances.ordered(PowerPhase::StatusDraw).size() == 2, "draw phase contains only its callbacks");
    check(f.b.player.powerInstances.ordered(PowerPhase::Exhaust).size() == 2, "exhaust phase contains only its callbacks");
    auto end = f.b.player.powerInstances.ordered(PowerPhase::End);
    check(end.size() == 3 && end[0].type == PS::THE_BOMB && end[1].type == PS::COMBUST &&
          end[2].type == PS::THE_BOMB, "independent timed instances share the same order registry");
    check(end[0].acquired != end[2].acquired, "same-type independent effects have distinct identities");
    const auto acquired = f.b.player.powerInstances.ordered();
    auto copy = f.b;
    const std::array<std::uint64_t, 7> seeds = {11,22,33,44,55,66,77};
    public_sampling::resampleCombatContinuation(f.game, copy, seeds);
    auto sampled = copy.player.powerInstances.ordered();
    check(sampled.size() == acquired.size(), "resampling preserves registry membership");
    for (std::size_t i = 0; i < acquired.size(); ++i)
        check(sampled[i].type == acquired[i].type && sampled[i].acquired == acquired[i].acquired &&
              sampled[i].remainingTurns == acquired[i].remainingTurns && sampled[i].payload == acquired[i].payload,
              "resampling preserves public order/timers/payload without source RNG");
    copy.player.powerInstances.removeInstance(end[0].acquired);
    check(copy.player.powerInstances.ordered(PowerPhase::End).size() == 2 &&
          f.b.player.powerInstances.ordered(PowerPhase::End).size() == 3, "copy mutation and per-instance removal are isolated");
    for (int i = 0; i < 3; ++i) copy.player.powerInstances.tick(PowerPhase::End);
    check(!copy.player.powerInstances.has(PS::THE_BOMB) && copy.player.powerInstances.has(PS::COMBUST),
          "independent timers expire without removing persistent powers");

    f.b.player.removeStatus<PS::EVOLVE>();
    check(!f.b.player.powerInstances.has(PS::EVOLVE), "central removal removes its instance");
    f.b.player.debuff<PS::EVOLVE>(1);
    check(f.b.player.powerInstances.ordered(PowerPhase::StatusDraw).back().type == PS::EVOLVE,
          "alternate activation path uses the same acquisition lifecycle");
    f.b.player.decrementStatus<PS::EVOLVE>(1);
    check(!f.b.player.powerInstances.has(PS::EVOLVE), "decrement-to-zero removes its instance");
    f.b.player.setHasStatus<PS::EVOLVE>(true);
    check(f.b.player.powerInstances.ordered(PowerPhase::StatusDraw).back().type == PS::EVOLVE,
          "direct status activation records acquisition");
    f.b.player.setHasStatus<PS::EVOLVE>(false);
    check(!f.b.player.powerInstances.has(PS::EVOLVE), "direct status deactivation removes its instance");
    Fixture legacy;
    legacy.b.player.buff<PS::CONSTRICTED>(7); legacy.b.player.buff<PS::COMBUST>(5);
    legacy.b.player.applyEndOfTurnPowers(legacy.b);
    auto first = legacy.b.actionQueue.popFront(); first(legacy.b);
    check(legacy.b.player.curHp == 63, "unmigrated end-power callback retains its legacy anchor");
    std::cout << "POWER_INSTANCE_ORDER " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
