// Scope: completed discard movement and the DrawCardAction movement barrier.
// Original Soul.update -> clearPowers -> resetAttributes; not a frame simulator.
#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main
#include "sim/PublicBombState.h"

namespace {
void movementMatrix() {
    for (auto id : {CardId::BLUDGEON, CardId::BLOOD_FOR_BLOOD, CardId::STRIKE_RED,
                    CardId::WHIRLWIND, CardId::WOUND}) {
        for (bool free : {false, true}) for (int temporary : {0, 1, 4}) {
            Fixture f;
            CardInstance c(id); c.setUniqueId(200); c.costForTurn = temporary;
            c.freeToPlayOnce = free;
            if (id == CardId::BLOOD_FOR_BLOOD) c.cost = 2;
            f.b.cards.moveToDiscardPile(c);
            check(f.b.cards.discardPile.back().costForTurn == temporary,
                  "discard itself must not eagerly reset cost before queued effects");
            check(!public_state::effectsResolved(f.b), "pending movement is not resolved");
            bool refused = false;
            try { public_sampling::resampleCombatContinuation(f.game, f.b, {1,2,3,4,5,6,7}); }
            catch (const std::runtime_error &) { refused = true; }
            check(refused, "sampling refuses pending movement even with empty action queues");
            auto clone = f.b;
            f.run();
            const auto &settled = f.b.cards.discardPile.back();
            check(settled.costForTurn == c.cost && settled.cost == c.cost,
                  "normal decision restores current combat cost including negative sentinels");
            check(settled.freeToPlayOnce == free && settled.uniqueId == c.uniqueId,
                  "movement does not consume once-free or change identity");
            check(f.b.cards.pendingDiscardCostResets.empty() && public_state::effectsResolved(f.b),
                  "movement is settled before publishing normal decision");
            check(clone.cards.discardPile.back().costForTurn == temporary,
                  "cloned rollout owns its pending state independently");
            clone.inputState = InputState::EXECUTING_ACTIONS; clone.executeActions();
            check(clone.cards.discardPile.back().costForTurn == c.cost,
                  "cloned rollout retains deferred reset");
        }
    }
}

void queuedBarriers() {
    Fixture f; f.b.cards = CardManager{}; f.b.cards.nextUniqueCardId = 100;
    CardInstance c(CardId::BLUDGEON); c.cost = 1; c.costForTurn = 0;
    f.b.cards.createTempCardInHand(c);
    f.b.addToBot(Action([](BattleContext &b) {
        const auto moved = b.cards.hand[0]; b.cards.removeFromHandAtIdx(0);
        b.cards.moveToDiscardPile(moved);
    }));
    f.b.addToBot(Action([](BattleContext &b) {
        check(b.cards.discardPile[0].costForTurn == 0, "same-queue pre-barrier retains temporary cost");
    }));
    f.b.addToBot(Actions::DrawCards(1));
    f.b.addToBot(Action([](BattleContext &b) {
        check(b.cards.cardsInHand == 1 && b.cards.hand[0].costForTurn == 1,
              "same-queue shuffle/redraw observes reset before following action");
        check(b.cards.pendingDiscardCostResets.empty(), "draw barrier consumes pending reset");
    }));
    f.run();

    Fixture retrieved;
    CardInstance moved(CardId::STRIKE_RED); moved.setUniqueId(210); moved.costForTurn = 0;
    moved.freeToPlayOnce = true;
    retrieved.b.cards.moveToDiscardPile(moved);
    retrieved.b.addToBot(Action([](BattleContext &b) {
        auto card = b.cards.discardPile[0]; b.cards.removeFromDiscard(0);
        b.cards.moveToHand(card);
        check(b.cards.hand[0].costForTurn == 0, "direct retrieval is not itself a draw barrier");
        b.cards.hand[0].cost = 2; // Later reset must use current, not snapshotted combat cost.
    }));
    retrieved.b.addToBot(Actions::DrawCards(1)); retrieved.run();
    check(retrieved.b.cards.hand[0].costForTurn == 2 && retrieved.b.cards.hand[0].freeToPlayOnce,
          "settlement follows the moved identity into hand and preserves free-once");

    for (bool noDraw : {false, true}) {
        Fixture blocked;
        blocked.b.cards.moveToDiscardPile(moved);
        if (noDraw) blocked.b.player.buff<PS::NO_DRAW>();
        blocked.b.drawCards(noDraw ? 1 : 0);
        check(blocked.b.cards.discardPile[0].costForTurn == 0,
              "No Draw and zero-amount draws return before the Soul barrier");
        blocked.run();
        check(blocked.b.cards.discardPile[0].costForTurn == 1,
              "normal decision still settles movement after blocked draw");
    }
    Fixture full;
    for (int i = 0; i < 10; ++i) full.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
    full.b.cards.moveToDiscardPile(moved); full.b.drawCards(1);
    check(full.b.cards.discardPile[0].costForTurn == 1 && full.b.cards.cardsInHand == 10,
          "full-hand draw checks movement before refusing to draw");
}

void untouchedAndPlay() {
    Fixture f;
    CardInstance c(CardId::BLUDGEON); c.costForTurn = 0;
    f.b.cards.createTempCardInHand(c);
    f.b.cards.createTempCardInDrawPile(0, c);
    f.b.cards.createTempCardInDiscard(c); // Raw creation is not reviewed Soul movement.
    c.setUniqueId(300); f.b.cards.moveToExhaustPile(c);
    CardInstance moved(CardId::STRIKE_RED); moved.setUniqueId(301); moved.costForTurn = 0;
    f.b.cards.moveToDiscardPile(moved); f.run();
    check(f.b.cards.hand[0].costForTurn == 0 && f.b.cards.drawPile[0].costForTurn == 0 &&
          f.b.cards.discardPile[0].costForTurn == 0 && f.b.cards.exhaustPile[0].costForTurn == 0,
          "no blanket reset of untouched cards, generated piles or unreviewed exhaust timing");
    check(f.b.cards.discardPile[1].costForTurn == 1, "only recorded movement settles");

    Fixture live; live.b.cards = CardManager{}; live.b.cards.nextUniqueCardId = 100;
    live.b.player.energy = 0; c.cost = 1; c.costForTurn = 0;
    live.b.cards.createTempCardInHand(c);
    check(live.b.cards.hand[0].canUse(live.b, 0, false), "temporary-zero Bludgeon can initially play");
    live.b.addToBotCard(CardQueueItem(live.b.cards.hand[0], 0, 0)); live.run();
    check(live.b.monsters.arr[0].curHp == 968 && live.b.player.energy == 0,
          "reset happens after the first free attack, not before it");
    check(live.b.cards.discardPile[0].cost == 1 && live.b.cards.discardPile[0].costForTurn == 1,
          "retained live reference: settled Bludgeon discard is 1/1");
    live.b.addToBot(Actions::DrawCards(1)); live.run();
    check(!live.b.cards.hand[0].canUse(live.b, 0, false),
          "retained live reference: redraw cannot attack again at zero energy");
}
}
int main() {
    movementMatrix(); queuedBarriers(); untouchedAndPlay();
    std::cout << "DISCARD_COST_LIFECYCLE " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
