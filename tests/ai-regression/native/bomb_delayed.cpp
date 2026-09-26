#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

int main() {
    const std::array<std::uint64_t, 7> seeds = {11,22,33,44,55,66,77};
    for (bool upgraded : {false, true}) for (bool frozen : {false, true}) for (int seed = 1; seed <= 8; ++seed) {
        Fixture f(seed);
        if (frozen) { f.game.relics.add({RelicId::FROZEN_EYE}); f.b.player.setHasRelic<RelicId::FROZEN_EYE>(true); }
        f.b.cards.createTempCardInHand(CardInstance(CardId::THE_BOMB, upgraded));
        auto twin = f.b;
        twin.shuffleRng = Random(98765); twin.cardRandomRng = Random(43210);
        if (!frozen) std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        for (auto *battle : {&f.b, &twin}) {
            check(public_state::effectsResolved(*battle) && public_state::powersComplete(battle->player),
                  "pre-play resolved complete-state roots can be sampled");
            const int energy = battle->player.energy;
            battle->addToBotCard(CardQueueItem(battle->cards.hand[0], 0, energy));
            battle->inputState = InputState::EXECUTING_ACTIONS; battle->executeActions();
            auto pending = public_state::pendingBombs(battle->player);
            check(pending.size() == 1 && pending[0].remainingTurns == 3 && pending[0].damage == (upgraded ? 50 : 40),
                  "real played variant installs independent typed countdown/damage");
            check(battle->player.energy == energy - 2 && battle->monsters.arr[0].curHp == 1000,
                  "normal play costs two without immediate damage");
            check(battle->cards.cardsInHand == 0 && battle->cards.discardPile.size() == 1 && battle->cards.exhaustPile.empty(),
                  "ordinary source lifecycle is discard, not mandatory exhaust");
            public_sampling::resampleCombatContinuation(f.game, *battle, seeds);
            check(public_state::pendingBombs(battle->player)[0].remainingTurns == 3,
                  "post-play resampling does not tick the countdown");
            for (int turn = 0; turn < 3; ++turn) {
                battle->endTurn(); battle->inputState = InputState::EXECUTING_ACTIONS; battle->executeActions();
            }
            check(public_state::pendingBombs(battle->player).empty() &&
                  battle->monsters.arr[0].curHp == (upgraded ? 950 : 960),
                  "sampled pre/post roots reach same third-end damage with actual enemy turns");
        }
        check(f.b.player.curHp == twin.player.curHp && f.b.outcome == twin.outcome,
              "public-equivalent worlds remain equal after actual play and full rounds");
    }
    std::cout << "BOMB_DELAYED " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
