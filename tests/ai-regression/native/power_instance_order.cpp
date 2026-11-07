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
    check(reviewedPowerSpec(PS::CONSTRICTED)->priority == 105, "Constricted uses its original priority 105");

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
    check(legacy.b.player.curHp == 69, "priority 5 Combust runs before acquired-earlier priority 105 Constricted");

    for (bool reverse : {false, true}) {
        Fixture order;
        if(reverse) {order.b.player.buff<PS::RAGE>(3);order.b.player.buff<PS::PANACHE>(10);}
        else {order.b.player.buff<PS::PANACHE>(10);order.b.player.buff<PS::RAGE>(3);}
        order.b.player.panacheCounter=1;
        order.b.curCardQueueItem.card=CardInstance(CardId::STRIKE_RED);
        order.b.onUseOrderedPowers();
        order.b.actionQueue.popFront()(order.b);
        check(reverse ? order.b.player.block==3 : order.b.monsters.arr[0].curHp==990,
              "on-use callback order follows acquisition, not fixed Panache-before-Rage");
        Fixture damage;
        if(reverse) {damage.b.player.buff<PS::FLAME_BARRIER>(7);damage.b.player.buff<PS::THORNS>(3);}
        else {damage.b.player.buff<PS::THORNS>(3);damage.b.player.buff<PS::FLAME_BARRIER>(7);}
        damage.b.player.attacked(damage.b,0,0);
        damage.b.actionQueue.popFront()(damage.b);
        check(damage.b.monsters.arr[0].curHp==(reverse?997:993),
              "on-attacked queue-top insertion reverses callback execution as in the game");
    }
    Fixture post;
    post.b.player.buff<PS::DRAW_CARD_NEXT_TURN>(1);
    post.b.player.buff<PS::DEMON_FORM>(3);
    post.b.player.applyStartOfTurnPostDrawPowers(post.b);
    post.b.actionQueue.popFront()(post.b);
    check(post.b.player.strength==3,"post-draw priority 20 draw follows priority 5 Demon Form");
    Fixture wide;
    wide.b.player.debuff<PS::DRAW_REDUCTION>(2);
    wide.b.player.debuff<PS::DRAW_REDUCTION>(1);
    check(wide.b.player.cardDrawPerTurn==4&&wide.b.player.getStatus<PS::DRAW_REDUCTION>()==3,
          "Draw Reduction stacks duration, not reduction magnitude");
    wide.b.player.applyAtEndOfRoundPowers();
    check(wide.b.player.getStatus<PS::DRAW_REDUCTION>()==3,"new reduction skips the application round");
    for(int i=0;i<3;++i)wide.b.player.applyAtEndOfRoundPowers();
    check(!wide.b.player.hasStatus<PS::DRAW_REDUCTION>()&&wide.b.player.cardDrawPerTurn==5,
          "expiration restores draw before the next start, not after another reduced draw");
    for(int i=0;i<160;++i)wide.b.player.buff<PS::COMBUST>(5);
    check(wide.b.player.combustHpLoss==160,"Combust source count does not wrap at signed-byte boundary");
    wide.b.player.buff<PS::MAYHEM>(40000);
    check(wide.b.player.getStatus<PS::MAYHEM>()==40000,"stack storage does not wrap at signed-short boundary");
    std::cout << "POWER_INSTANCE_ORDER " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
