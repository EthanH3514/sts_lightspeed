#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

namespace {
int recordCount(const Player &p, PS status) {
    int count = 0;
    for (const auto &row : public_state::playerPowers(p)) if (row.type == status) ++count;
    return count;
}
template<PS status> void expiredDebuff() {
    Fixture f;
    f.play();
    f.b.player.buff<status>(1);
    auto untouched = f.b;
    check(recordCount(f.b.player, status) == 1, "active debuff exports once");
    check(public_state::powersComplete(f.b.player), "active represented debuff is complete");
    f.b.endTurn(); f.run();
    check(!f.b.player.hasStatus<status>() && f.b.player.statusMap.at(status) == 0,
          "normal end turn leaves an inactive zero-valued legacy slot");
    check(public_state::powersComplete(f.b.player), "expired debuff does not invalidate public powers");
    check(recordCount(f.b.player, status) == 0, "expired debuff is absent from public records");
    check(recordCount(untouched.player, status) == 1, "branch expiry does not mutate original snapshot");
    check(public_state::effectsResolved(f.b), "expiry reaches a resolved live boundary");
    check(public_state::pendingBombs(f.b.player).size() == 1,
          "debuff expiry does not lose the pending Bomb");
}
void removedPower() {
    Fixture f;
    f.b.player.buff<PS::DOUBLE_TAP>(2);
    f.b.player.removeStatus<PS::DOUBLE_TAP>();
    check(f.b.player.statusMap.at(PS::DOUBLE_TAP) == 2, "removal may retain a positive legacy slot");
    check(public_state::powersComplete(f.b.player), "inactive positive slot is not a missing public power");
    check(recordCount(f.b.player, PS::DOUBLE_TAP) == 0, "removed positive power is not exported");
}
void representations() {
    Fixture f;
    f.play(); f.b.player.buff<PS::COMBUST>(5); f.play(true);
    auto rows = public_state::playerPowers(f.b.player);
    check(rows.size() == 3 && rows[0].type == PS::THE_BOMB && rows[1].type == PS::COMBUST &&
          rows[2].type == PS::THE_BOMB, "registry order and duplicate Bombs are preserved");
    check(rows[0].amount == 3 && rows[0].damage == 40 && rows[2].damage == 50 && rows[1].hpLoss == 1,
          "registry countdowns and per-instance payloads are preserved");
    f.b.player.buff<PS::PANACHE>(10);
    f.b.player.panacheCounter = 2;
    rows = public_state::playerPowers(f.b.player);
    check(rows.back().type == PS::PANACHE && rows.back().amount == 2 && rows.back().damage == 10,
          "active Panache retains remaining plays separately from damage");
    f.b.player.removeStatus<PS::PANACHE>();
    check(recordCount(f.b.player, PS::PANACHE) == 0, "inactive Panache payload is not a public power");
    f.b.player.strength = -3; f.b.player.dexterity = 2;
    f.b.player.statusMap[PS::STRENGTH] = 17; f.b.player.statusMap[PS::ARTIFACT] = 9;
    rows = public_state::playerPowers(f.b.player);
    check(recordCount(f.b.player, PS::STRENGTH) == 1 && rows.back().amount == -3,
          "signed direct field overrides a stale numeric slot without duplication");
    check(recordCount(f.b.player, PS::ARTIFACT) == 0, "zero direct field does not resurrect stale map value");
    f.b.player.buff<PS::CORRUPTION>();
    check(public_state::powersComplete(f.b.player), "Corruption flag now has a complete public representation");
    f.b.player.statusMap.erase(PS::CORRUPTION);
    check(!public_state::powersComplete(f.b.player), "missing active flag payload still fails closed");
    f.b.player.removeStatus<PS::CORRUPTION>();
    f.b.player.statusMap.erase(PS::COMBUST);
    check(!public_state::powersComplete(f.b.player), "active registry missing its payload still fails closed");
    f.b.player.removeStatus<PS::COMBUST>();
    f.b.player.powerInstances.activate(PS::COMBUST);
    check(!public_state::powersComplete(f.b.player), "orphaned reviewed registry still fails closed");
}
}
int main() {
    expiredDebuff<PS::WEAK>(); expiredDebuff<PS::FRAIL>(); expiredDebuff<PS::VULNERABLE>();
    removedPower(); representations();
    std::cout << "PLAYER_PUBLIC_POWERS " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
