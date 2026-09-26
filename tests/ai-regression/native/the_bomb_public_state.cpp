#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

int main() {
    // Exercise actual end-turn/card-queue/next-turn flow, not just the isolated
    // power callback. Enemy actions are intentionally skipped in this control.
    for (bool upgraded : {false, true}) {
        Fixture fullTurn; fullTurn.play(upgraded);
        for (int turn = 1; turn <= 3; ++turn) {
            fullTurn.b.skipMonsterTurn = true; fullTurn.b.endTurn(); fullTurn.run();
            const auto packets = public_state::pendingBombs(fullTurn.b.player);
            check(turn == 3 ? packets.empty() : (packets.size() == 1 && packets[0].remainingTurns == 3 - turn),
                  "resolved full-turn flow exports the remaining countdown");
            check(fullTurn.b.monsters.arr[0].curHp == (turn == 3 ? (upgraded ? 950 : 960) : 1000),
                  "resolved full-turn flow detonates only on the third end");
        }
    }
    for (bool upgraded : {false, true}) for (int count = 1; count <= 8; ++count) {
        Fixture f;
        for (int i = 0; i < count; ++i) f.play(upgraded);
        for (int remaining = 3; remaining >= 1; --remaining) {
            const auto packets = public_state::pendingBombs(f.b.player);
            check(packets.size() == count, "export retains duplicate Bomb packets");
            for (const auto &packet : packets) {
                check(packet.remainingTurns == remaining, "export countdown is not damage");
                check(packet.damage == (upgraded ? 50 : 40), "export damage is not countdown");
            }
            f.tick();
        }
        check(public_state::pendingBombs(f.b.player).empty(), "exported Bombs disappear after detonation");
    }
    Fixture staggered; staggered.play(); staggered.tick(); staggered.play(true);
    auto packets = public_state::pendingBombs(staggered.b.player);
    check(packets.size() == 2 && packets[0].remainingTurns == 2 && packets[0].damage == 40 &&
          packets[1].remainingTurns == 3 && packets[1].damage == 50, "mixed public packets retain countdown and damage");
    auto twin = staggered.b;
    twin.player.buff<PS::THE_BOMB>(40);
    check(public_state::pendingBombs(twin.player).size() == 3 &&
          public_state::pendingBombs(staggered.b.player).size() == 2, "public records reflect independent copies");

    // Acquired-order regression, compared with bytecode-derived queue order.
    // Original equal-priority Combust acquired before The Bomb queues HP loss
    // before explosion. Native now preserves that position. Reference below
    // is bytecode-derived action order, not a live-game oracle.
    Fixture native; native.b.player.buff<PS::COMBUST>(5); native.b.player.combustHpLoss = 1;
    native.b.player.powerInstances.add(PS::THE_BOMB, 1, 40); native.b.monsters.arr[0].curHp = 30;
    auto reference = native.b;
    reference.addToBot(Actions::PlayerLoseHp(1, true));
    reference.addToBot(Actions::DamageAllEnemy(5));
    reference.addToBot(Actions::DamageAllEnemy(40));
    reference.inputState = InputState::EXECUTING_ACTIONS; reference.executeActions();
    native.tick();
    std::cout << "BOMB_COMBUST_ORDER_CONTROL native_hp=" << native.b.player.curHp
              << " reference_hp=" << reference.player.curHp << '\n';
    check(reference.player.curHp == 69, "bytecode-derived Combust-first reference loses one HP");
    // This nonlethal control does NOT demonstrate a final-HP difference.
    check(native.b.player.curHp == reference.player.curHp,
          "nonlethal Combust comparison does not demonstrate different final HP");
    Fixture lethal; lethal.b.player.curHp = 1;
    lethal.b.player.buff<PS::COMBUST>(5); lethal.b.player.combustHpLoss = 1;
    lethal.b.player.powerInstances.add(PS::THE_BOMB, 1, 40); lethal.b.monsters.arr[0].curHp = 30;
    auto lethalReference = lethal.b;
    lethalReference.addToBot(Actions::PlayerLoseHp(1, true));
    lethalReference.addToBot(Actions::DamageAllEnemy(5));
    lethalReference.addToBot(Actions::DamageAllEnemy(40));
    lethalReference.inputState = InputState::EXECUTING_ACTIONS; lethalReference.executeActions();
    lethal.tick();
    std::cout << "BOMB_COMBUST_LETHAL_CONTROL native_enemy_hp=" << lethal.b.monsters.arr[0].curHp
              << " reference_enemy_hp=" << lethalReference.monsters.arr[0].curHp
              << " native_outcome=" << static_cast<int>(lethal.b.outcome)
              << " reference_outcome=" << static_cast<int>(lethalReference.outcome) << '\n';
    check(lethal.b.outcome == Outcome::PLAYER_LOSS && lethalReference.outcome == Outcome::PLAYER_LOSS,
          "lethal comparison retains matching win/loss");
    check(lethal.b.monsters.arr[0].curHp == 30 && lethalReference.monsters.arr[0].curHp == 30,
          "lethal Combust-first order leaves the enemy alive on both paths");
    std::cout << "THE_BOMB_PUBLIC_STATE " << failures << " failures / " << checks
              << " checks\n";
    return failures ? 1 : 0;
}
