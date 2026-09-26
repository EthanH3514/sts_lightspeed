#include <algorithm>
#include <iostream>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
#include "sim/PublicCombatResampling.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
struct Fixture {
    GameContext game;
    BattleContext bc;
    Fixture(RelicId relic = RelicId::INVALID) : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (relic != RelicId::INVALID) game.obtainRelic(relic);
        game.floorNum = 1; game.enterBattle(MonsterEncounter::CULTIST);
        bc.init(game); bc.executeActions();
        bc.cards = CardManager{}; bc.cards.nextUniqueCardId = 100;
        bc.player.energy = 3;
        for (int i = 0; i < 12; ++i) bc.cards.drawPile.push_back(card(CardId::DEFEND_RED));
    }
    CardInstance card(CardId id) { CardInstance c(id); c.setUniqueId(bc.cards.nextUniqueCardId++); return c; }
    void hand(CardId id) { bc.cards.moveToHand(card(id)); }
    void resolve() { bc.inputState = InputState::EXECUTING_ACTIONS; bc.executeActions(); }
    void play() { bc.addToBotCard(CardQueueItem(bc.cards.hand[0], 0, bc.player.energy)); resolve(); }
};
void prideTraitsAndUse() {
    for (bool up : {false, true}) {
        CardInstance c(CardId::PRIDE, up);
        check(c.cost == 1 && c.costForTurn == 1, "Pride one energy both metadata variants");
        check(isCardInnate(CardId::PRIDE, up), "Pride innate");
        check(c.doesExhaust() && !c.canUpgrade(), "Pride exhausts and cannot upgrade");
    }
    for (int energy : {0, 1, 3}) for (auto relic : {RelicId::INVALID, RelicId::BLUE_CANDLE}) {
        Fixture f(relic); f.hand(CardId::PRIDE); f.bc.player.energy = energy;
        const int hp = f.bc.player.curHp;
        f.bc.player.buff<PS::FEEL_NO_PAIN>(3);
        const bool legal = f.bc.cards.hand[0].canUse(f.bc, 0, false);
        check(legal == (energy >= 1), "Pride playable without Candle, still pays energy with Candle");
        check(f.bc.cards.hand[0].canUse(f.bc, 0, true), "automatic Pride does not require energy or Candle");
        if (legal) {
            f.play();
            check(f.bc.player.energy == energy - 1, "manual Pride pays one energy");
            check(hp - f.bc.player.curHp == (relic == RelicId::BLUE_CANDLE ? 1 : 0), "Candle adds self HP loss even to playable curse");
            check(f.bc.player.block == 3 && f.bc.cards.exhaustPile.size() == 1 && f.bc.cards.cardsInHand == 0,
                  "played Pride exhausts once, exhaust callbacks, no replacement");
        }
    }
    for (bool forced : {false, true}) {
        Fixture f; f.bc.player.energy = 0; f.bc.player.buff<PS::FEEL_NO_PAIN>(3);
        f.bc.cards.drawPile.push_back(f.card(CardId::PRIDE));
        f.bc.addToBot(Actions::PlayTopCard(0, forced)); f.resolve();
        check(f.bc.player.cardsPlayedThisTurn == 1 && f.bc.player.energy == 0, "Pride autoplay counts as successful free use");
        check(f.bc.cards.exhaustPile.size() == 1 && f.bc.cards.discardPile.empty() && f.bc.player.block == 3,
              "both autoplay modes exhaust Pride intrinsically");
    }
    for (int seed = 0; seed < 16; ++seed) {
        GameContext gc(CharacterClass::IRONCLAD, seed, 0); gc.deck.obtain(gc, Card(CardId::PRIDE));
        gc.floorNum = 1; gc.enterBattle(MonsterEncounter::CULTIST); BattleContext bc; bc.init(gc); bc.executeActions();
        check(std::any_of(bc.cards.hand.begin(), bc.cards.hand.begin() + bc.cards.cardsInHand,
                         [](const auto &c) { return c.id == CardId::PRIDE; }), "Pride opening hand across seeds");
    }
}
void prideReplication() {
    for (int count : {1, 2, 3}) {
        Fixture f;
        for (int i = 0; i < count; ++i) {
            f.hand(CardId::PRIDE); f.bc.cards.hand[i].cost = i + 1; f.bc.cards.hand[i].costForTurn = 0;
            f.bc.cards.hand[i].freeToPlayOnce = true;
        }
        const auto before = f.bc.cards.nextUniqueCardId;
        const auto rng = f.bc.cardRandomRng.counter;
        const int hp = f.bc.player.curHp;
        f.bc.player.buff<PS::PANACHE>(10);
        f.bc.callEndOfTurnActions(); f.resolve();
        check(f.bc.cards.drawPile.size() == std::size_t(12 + count), "one top copy per resident Pride");
        check(f.bc.cards.cardsInHand == count && f.bc.cards.exhaustPile.empty(), "callback keeps original until ordinary end discard");
        check(f.bc.player.cardsPlayedThisTurn == 0 && f.bc.player.panacheCounter == 5 && f.bc.player.curHp == hp,
              "copy callback not a card play or Candle effect");
        check(f.bc.cardRandomRng.counter == rng, "top insertion consumes no RNG");
        for (int i = 0; i < count; ++i) {
            const auto &c = f.bc.cards.drawPile[12 + i < int(f.bc.cards.drawPile.size()) ? 12 + i : 0];
            check(c.id == CardId::PRIDE && c.getUniqueId() == before + i, "copy has fresh identity, last callback topmost");
            check(c.cost == i + 1 && c.costForTurn == 0 && c.freeToPlayOnce,
                  "Pride copies combat cost, temporary cost and once-free at callback time");
        }
    }
    Fixture turn; turn.hand(CardId::PRIDE); const auto id = turn.bc.cards.hand[0].getUniqueId();
    search::Action(search::ActionType::END_TURN).execute(turn.bc);
    check(turn.bc.cards.cardsInHand == 5 && turn.bc.cards.hand[0].id == CardId::PRIDE && turn.bc.cards.hand[0].getUniqueId() != id,
          "next normal draw starts with new Pride");
    check(turn.bc.cards.discardPile.size() == 1 && turn.bc.cards.discardPile[0].getUniqueId() == id,
          "original Pride discards, not duplicated identity");
    Fixture drawn; drawn.hand(CardId::PRIDE); drawn.hand(CardId::CLUMSY);
    drawn.bc.player.buff<PS::DARK_EMBRACE>(1);
    search::Action(search::ActionType::END_TURN).execute(drawn.bc);
    // Dark Embrace queues behind the ordinary discard; the drawn copy remains.
    check(drawn.bc.cards.cardsInHand == 6 && drawn.bc.cards.hand[0].id == CardId::PRIDE &&
          drawn.bc.cards.discardPile.size() == 1 && drawn.bc.cards.discardPile[0].id == CardId::PRIDE,
          "end-turn exhaust draw sees top Pride copy after original discard");
}
void sharedTopGeneration() {
    for (int existing : {0, 4}) for (int amount : {0, 1, 2, 3}) {
        Fixture f; f.bc.cards.drawPile.resize(existing);
        const auto rng = f.bc.cardRandomRng.counter;
        const auto next = f.bc.cards.nextUniqueCardId;
        CardInstance c(CardId::BURN);
        f.bc.addToBot(Actions::MakeTempCardInDrawPile(c, amount, false));
        c.id = CardId::WOUND;
        f.resolve();
        check(f.bc.cards.drawPile.size() == std::size_t(existing + amount), "top generation empty/nonempty pile and zero/multiple count");
        check(f.bc.cardRandomRng.counter == rng, "top generation does not consume RNG");
        for (int i = 0; i < amount; ++i)
            check(f.bc.cards.drawPile[existing + i].id == CardId::BURN &&
                  f.bc.cards.drawPile[existing + i].getUniqueId() == next + i,
                  "shared generation preserves queued template and assigns fresh identities");
    }
    for (int asc : {0, 17, 18, 20}) {
        GameContext gc(CharacterClass::IRONCLAD, 17, asc);
        gc.floorNum = 55; gc.act = 4; gc.enterBattle(MonsterEncounter::SHIELD_AND_SPEAR);
        BattleContext bc; bc.init(gc); bc.executeActions();
        bc.cards = CardManager{}; bc.cards.nextUniqueCardId = 100;
        bc.player.block = 999;
        auto &spear = bc.monsters.arr[1]; spear.setMove(MonsterMoveId::SPIRE_SPEAR_BURN_STRIKE);
        spear.takeTurn(bc); bc.inputState = InputState::EXECUTING_ACTIONS; bc.executeActions();
        const auto &pile = asc >= 18 ? bc.cards.drawPile : bc.cards.discardPile;
        check(pile.size() == 2 && pile[0].id == CardId::BURN && pile[1].id == CardId::BURN,
              "Spire Spear uses common top generator at A18+, discard below A18");
    }
}
void necroExhaust() {
    for (auto relic : {RelicId::INVALID, RelicId::CHARONS_ASHES, RelicId::BLUE_CANDLE}) {
        Fixture f(relic); f.hand(CardId::NECRONOMICURSE);
        auto original = f.bc.cards.hand[0]; f.bc.cards.hand[0].freeToPlayOnce = true;
        f.bc.player.buff<PS::FEEL_NO_PAIN>(3); f.bc.player.buff<PS::DARK_EMBRACE>(1);
        const int enemy = f.bc.monsters.arr[0].curHp;
        f.bc.addToBot(Actions::ExhaustSpecificCardInHand(0, original.getUniqueId())); f.resolve();
        check(f.bc.cards.exhaustPile.size() == 1 && f.bc.cards.exhaustPile[0].getUniqueId() == original.getUniqueId(), "original Necro stays exhausted");
        check(f.bc.cards.cardsInHand == 2 && f.bc.cards.hand[0].id == CardId::DEFEND_RED && f.bc.cards.hand[1].id == CardId::NECRONOMICURSE,
              "Dark Embrace draws before fresh Necro returns");
        check(f.bc.cards.hand[1].getUniqueId() != original.getUniqueId() && !f.bc.cards.hand[1].freeToPlayOnce,
              "Necro creates base copy not same identity or modified copy");
        check(f.bc.player.block == 3 && enemy - f.bc.monsters.arr[0].curHp == (relic == RelicId::CHARONS_ASHES ? 3 : 0), "exhaust powers and relic trigger once");
    }
    Fixture full; for (int i = 0; i < 10; ++i) full.hand(CardId::STRIKE_RED);
    auto necro = full.card(CardId::NECRONOMICURSE); full.bc.triggerAndMoveToExhaustPile(necro); full.resolve();
    check(full.bc.cards.cardsInHand == 10 && full.bc.cards.discardPile.size() == 1 && full.bc.cards.discardPile[0].id == CardId::NECRONOMICURSE,
          "full-hand replacement overflows to discard");
    Fixture candle(RelicId::BLUE_CANDLE); candle.hand(CardId::NECRONOMICURSE); candle.bc.player.buff<PS::RUPTURE>(2);
    const int hp = candle.bc.player.curHp;
    for (int i = 1; i <= 3; ++i) {
        candle.play();
        check(candle.bc.cards.cardsInHand == 1 && candle.bc.cards.exhaustPile.size() == std::size_t(i), "repeated Candle creates a new curse each time");
        check(hp - candle.bc.player.curHp == i && candle.bc.player.strength == 2 * i, "Candle repeated self loss and Rupture");
    }
    for (bool forced : {false, true}) {
        Fixture f; f.bc.cards.drawPile.push_back(f.card(CardId::NECRONOMICURSE));
        f.bc.addToBot(Actions::PlayTopCard(0, forced)); f.resolve();
        check(f.bc.player.cardsPlayedThisTurn == 0, "rejected Necro is not successful play");
        check(f.bc.cards.cardsInHand == (forced ? 1 : 0) && (forced ? f.bc.cards.exhaustPile : f.bc.cards.discardPile).size() == 1,
              "rejected forced-exhaust regenerates, ordinary discard does not");
    }
    GameContext gc(CharacterClass::IRONCLAD, 9, 0); gc.deck = Deck{};
    gc.deck.obtain(gc, Card(CardId::NECRONOMICURSE)); gc.deck.obtain(gc, Card(CardId::PRIDE));
    check(gc.deck.getTransformableCount() == 1, "Necro excluded, Pride eligible for master-deck selection");
}
void publicCopyOrder() {
    const std::array<std::uint64_t,7> seeds {11,22,33,44,55,66,77};
    for (int copies : {1, 2, 4}) {
        Fixture f;
        f.bc.cards.publicKnownBottomIds = {100,101};
        f.bc.addToBot(Actions::MakeTempCardInDrawPile(CardInstance(CardId::PRIDE), copies, false)); f.resolve();
        check(f.bc.cards.publicKnownBottomIds == std::vector<int>({100,101}) && !f.bc.cards.publicBottomOrderUncertain,
              "deterministic top generation preserves known bottom");
        check(f.bc.cards.publicKnownTopIds.size() == std::size_t(copies), "all generated top identities recorded");
        auto a = f.bc, b = f.bc;
        std::reverse(b.cards.drawPile.begin()+2, b.cards.drawPile.begin()+12);
        public_sampling::resampleCombatContinuation(f.game, a, seeds);
        public_sampling::resampleCombatContinuation(f.game, b, seeds);
        for (std::size_t i = 0; i < a.cards.drawPile.size(); ++i)
            check(a.cards.drawPile[i].uniqueId == b.cards.drawPile[i].uniqueId, "paired hidden permutation identical public sample");
        for (int i = 0; i < copies; ++i)
            check(a.cards.drawPile[12+i].uniqueId == f.bc.cards.publicKnownTopIds[i], "known top suffix not shuffled");
        f.bc.addToBot(Actions::DrawCards(1)); f.resolve();
        check(f.bc.cards.publicKnownTopIds.size() == std::size_t(copies-1), "drawing only consumes last known top ID");
        f.hand(CardId::STRIKE_RED); f.bc.chooseForethoughtCard(f.bc.cards.cardsInHand-1);
        check(f.bc.cards.publicKnownTopIds.size() == std::size_t(copies-1) && !f.bc.cards.publicTopOrderUncertain,
              "selected bottom placement preserves remaining top suffix");
        if (copies > 1) {
            const auto uid = f.bc.cards.publicKnownTopIds.front();
            auto it = std::find_if(f.bc.cards.drawPile.begin(), f.bc.cards.drawPile.end(), [=](const auto &c) { return c.uniqueId == uid; });
            const auto selected = *it;
            f.bc.cards.removeFromDrawPileAtIdx(int(it-f.bc.cards.drawPile.begin())); f.bc.moveToHandHelper(selected);
            check(f.bc.cards.publicKnownTopIds.size() == std::size_t(copies-2), "retrieval removes only selected known identity");
        }
    }
    Fixture random; random.bc.cards.createTempCardOnDrawTop(random.card(CardId::PRIDE));
    random.bc.cards.createTempCardInDrawPile(0, random.card(CardId::WOUND));
    check(random.bc.cards.publicTopOrderUncertain && random.bc.cards.publicKnownTopIds.empty(), "random insertion marks previous public constraints unsupported");
    bool refused = false;
    try { public_sampling::resampleCombatContinuation(random.game, random.bc, seeds); }
    catch (const std::runtime_error &) { refused = true; }
    check(refused, "uncertain top sampling refused");
    random.bc.addToBot(Actions::ShuffleDrawPile()); random.resolve();
    check(!random.bc.cards.publicTopOrderUncertain && random.bc.cards.publicKnownTopIds.empty(), "full shuffle clears knowledge and uncertainty");
    Fixture turn; for (int i = 0; i < 7; ++i) turn.hand(CardId::PRIDE);
    search::Action(search::ActionType::END_TURN).execute(turn.bc);
    check(turn.bc.cards.publicKnownTopIds.size() == 2, "seven end-hand copies leave two known after five normal draws");
    const auto ids = turn.bc.cards.publicKnownTopIds;
    public_sampling::resampleCombatContinuation(turn.game, turn.bc, seeds);
    check(turn.bc.cards.drawPile.back().uniqueId == ids.back(), "post-turn root preserves remaining generated top");
}
}
int main() {
    prideTraitsAndUse(); prideReplication(); necroExhaust(); sharedTopGeneration(); publicCopyOrder();
    std::cout << "REPLICATING_CURSE_FOUNDATION " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
