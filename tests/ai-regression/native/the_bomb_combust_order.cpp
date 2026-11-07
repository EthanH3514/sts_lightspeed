#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

int main() {
    for (int before = 0; before <= 3; ++before) for (int after = 0; after <= 3; ++after) {
        if (!before && !after) continue;
        for (bool upgraded : {false, true}) for (int hp : {1, 70}) {
            Fixture f;
            for (int i = 0; i < before; ++i) f.play(upgraded);
            f.b.player.buff<PS::COMBUST>(5);
            for (int i = 0; i < after; ++i) f.play(upgraded);
            f.tick(); f.tick();
            f.b.player.curHp = hp; f.b.monsters.arr[0].curHp = 30;
            auto reference = f.b;
            for (int i = 0; i < before; ++i) reference.addToBot(Actions::DamageAllEnemy(upgraded ? 50 : 40));
            reference.addToBot(Actions::PlayerLoseHp(1, true));
            reference.addToBot(Actions::DamageAllEnemy(5));
            for (int i = 0; i < after; ++i) reference.addToBot(Actions::DamageAllEnemy(upgraded ? 50 : 40));
            reference.inputState = InputState::EXECUTING_ACTIONS; reference.executeActions();
            f.tick();
            check(f.b.player.curHp == reference.player.curHp, "player HP matches acquisition-ordered reference");
            check(f.b.monsters.arr[0].curHp == reference.monsters.arr[0].curHp, "enemy HP matches acquisition-ordered reference");
            check(f.b.outcome == reference.outcome, "outcome matches acquisition-ordered reference");
        }
    }
    // Copy and public resampling must preserve the same acquired-order state.
    for (int seed = 1; seed <= 16; ++seed) {
        Fixture f(seed); f.b.player.buff<PS::COMBUST>(5); f.play(); f.tick(); f.tick();
        f.b.player.curHp = 1; f.b.monsters.arr[0].curHp = 30;
        auto copy = f.b;
        const std::array<std::uint64_t, 7> seeds = {11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game, copy, seeds);
        copy.player.applyEndOfTurnPowers(copy); copy.inputState = InputState::EXECUTING_ACTIONS; copy.executeActions();
        f.tick();
        check(copy.monsters.arr[0].curHp == 30 && f.b.monsters.arr[0].curHp == 30,
              "Combust-first survives cloning and public resampling");
    }
    Fixture order; order.play(); order.b.player.buff<PS::COMBUST>(5); order.play(true);
    for (int turn = 0; turn < 3; ++turn) {
        const auto packets = public_state::pendingBombs(order.b.player);
        check(packets.size() == 2 && packets[0].beforeCombust && !packets[1].beforeCombust,
              "export retains packets on both acquired sides of Combust as countdown shifts");
        if (turn == 0) order.b.player.buff<PS::COMBUST>(5);
        order.tick();
    }
    order.play();
    check(!public_state::pendingBombs(order.b.player)[0].beforeCombust,
          "new Bomb does not inherit an expired before-Combust prefix");
    order.b.player.removeStatus<PS::COMBUST>();
    check(public_state::pendingBombs(order.b.player)[0].beforeCombust,
          "no active Combust means no after-Combust packet");
    order.b.player.buff<PS::COMBUST>(5); order.play();
    const auto reapplied = public_state::pendingBombs(order.b.player);
    check(reapplied[0].beforeCombust && !reapplied[1].beforeCombust,
          "reacquired Combust snapshots surviving Bombs instead of stale order");

    Fixture staggered; staggered.play(); staggered.tick(); staggered.play(true);
    staggered.b.player.buff<PS::COMBUST>(5); staggered.play();
    const auto waves = public_state::pendingBombs(staggered.b.player);
    check(waves.size() == 3 && waves[0].remainingTurns == 2 && waves[0].beforeCombust &&
          waves[1].remainingTurns == 3 && waves[1].beforeCombust && !waves[2].beforeCombust,
          "acquisition prefix is preserved across distinct countdown buckets");

    Fixture full; full.b.player.buff<PS::COMBUST>(5); full.play(); full.tick(); full.tick();
    full.b.player.curHp = 1; full.b.monsters.arr[0].curHp = 30;
    full.b.skipMonsterTurn = true; full.b.endTurn(); full.run();
    check(full.b.outcome == Outcome::PLAYER_LOSS && full.b.monsters.arr[0].curHp == 30,
          "full native end-turn path preserves lethal Combust-before-Bomb order");
    std::cout << "THE_BOMB_COMBUST_ORDER " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
