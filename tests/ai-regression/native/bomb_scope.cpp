#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

namespace {
const std::array<std::uint64_t, 7> seeds = {11,22,33,44,55,66,77};
void expectPendingRefusal(Fixture &f, const char *message) {
    check(!public_state::effectsResolved(f.b), "pending native state is not publicly resolved");
    bool refused = false;
    try { public_sampling::resampleCombatContinuation(f.game, f.b, seeds); }
    catch (const std::runtime_error &) { refused = true; }
    check(refused, message);
}
void boundaries() {
    Fixture normal; normal.play();
    check(public_state::bombStateSchema == 1 && public_state::effectsResolved(normal.b),
          "resolved post-play boundary explicitly exports schema/readiness");
    check(public_state::powersComplete(normal.b.player), "ordinary Bomb has complete power representation");
    Fixture action; action.play(); action.b.addToBot(Actions::GainBlock(1));
    expectPendingRefusal(action, "nonempty action queue cannot resample");
    Fixture card; card.play();
    card.b.addToBotCard(CardQueueItem(CardInstance(CardId::DEFEND_RED), 0, 3));
    expectPendingRefusal(card, "nonempty card queue cannot resample");
    Fixture select; select.play(); select.b.inputState = InputState::CARD_SELECT;
    expectPendingRefusal(select, "suspended selection cannot resample");
    Fixture executing; executing.play(); executing.b.inputState = InputState::EXECUTING_ACTIONS;
    expectPendingRefusal(executing, "between countdown scheduling and resolution cannot resample");
    Fixture terminal; terminal.play(); terminal.b.outcome = Outcome::PLAYER_VICTORY;
    expectPendingRefusal(terminal, "terminal intermediate records cannot resample");
    Fixture flag; flag.play(); flag.b.player.buff<PS::CORRUPTION>();
    check(!public_state::powersComplete(flag.b.player), "bit-only Corruption omitted by legacy export is incomplete");
    flag.b.player.removeStatus<PS::CORRUPTION>();
    check(public_state::powersComplete(flag.b.player), "removal restores complete representation");
    flag.b.player.statusMap[PS::COMBUST] = 5;
    check(!public_state::powersComplete(flag.b.player), "stale map entry cannot attest complete state");
}
void fullTurns() {
    // Actual monster turns and normal draws, not isolated callbacks or skipMonsterTurn.
    for (bool upgraded : {false, true}) for (bool frozen : {false, true})
    for (int position = 0; position < 3; ++position) for (int seed = 1; seed <= 8; ++seed) {
        Fixture f(seed);
        if (frozen) { f.game.relics.add({RelicId::FROZEN_EYE}); f.b.player.setHasRelic<RelicId::FROZEN_EYE>(true); }
        if (position == 1) f.b.player.buff<PS::COMBUST>(5);
        f.play(upgraded);
        if (position == 2) f.b.player.buff<PS::COMBUST>(7);
        const int fire = position == 1 ? 5 : position == 2 ? 7 : 0;
        auto twin = f.b;
        if (!frozen) std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        twin.shuffleRng = Random(9999); twin.cardRandomRng = Random(7777);
        for (int round = 1; round <= 3; ++round) {
            check(public_state::effectsResolved(f.b) && public_state::powersComplete(f.b.player),
                  "each ongoing full-turn root has complete resolved records");
            public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
            public_sampling::resampleCombatContinuation(f.game, twin, seeds);
            const int oldTurn = f.b.turn;
            f.b.endTurn(); f.run();
            twin.endTurn(); twin.inputState = InputState::EXECUTING_ACTIONS; twin.executeActions();
            check(f.b.turn == oldTurn + 1 && f.b.inputState == InputState::PLAYER_NORMAL,
                  "monster action and next normal draw complete each round");
            check(f.b.monsters.arr[0].curHp == 1000 - fire * round - (round == 3 ? (upgraded ? 50 : 40) : 0),
                  "full turns detonate on third end while Combust ticks each end");
            check(f.b.player.curHp == twin.player.curHp && f.b.monsters.arr[0].curHp == twin.monsters.arr[0].curHp,
                  "public-equivalent sampled worlds have equal HP at resolved boundaries");
            const auto a = public_state::pendingBombs(f.b.player), b = public_state::pendingBombs(twin.player);
            check(a.size() == b.size() && a.size() == (round < 3 ? 1U : 0U),
                  "public packets expire exactly once in both worlds");
            if (!a.empty()) check(a[0].remainingTurns == 3-round && a[0].remainingTurns == b[0].remainingTurns &&
                                  a[0].damage == b[0].damage && a[0].beforeCombust == b[0].beforeCombust,
                                  "countdown payload and relative order survive full-turn sampling");
        }
        check(f.b.player.curHp < 70 && f.b.outcome == Outcome::UNDECIDED,
              "real enemy attacks happened and player survived three rounds");
    }
}
}
int main() {
    boundaries(); fullTurns();
    std::cout << "BOMB_SCOPE " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
