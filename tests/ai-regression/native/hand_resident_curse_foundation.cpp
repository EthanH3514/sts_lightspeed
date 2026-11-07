#include <iostream>
#include <stdexcept>
#include <string>
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
        bc.player.energy = 20;
        for (int i = 0; i < 12; ++i) bc.cards.drawPile.push_back(card(CardId::DEFEND_RED));
    }
    CardInstance card(CardId id) { CardInstance c(id); c.setUniqueId(bc.cards.nextUniqueCardId++); return c; }
    void hand(CardId id) { bc.cards.moveToHand(card(id)); }
    void resolve() { bc.inputState = InputState::EXECUTING_ACTIONS; bc.executeActions(); }
    void play(int index = 0) {
        auto c = bc.cards.hand[index];
        check(c.canUse(bc, 0, false), "fixture manual legality");
        bc.addToBotCard(CardQueueItem(c, 0, bc.player.energy)); resolve();
    }
};
void painPackets() {
    for (int count : {1, 2, 3}) for (auto relic : {RelicId::INVALID, RelicId::TUNGSTEN_ROD}) {
        Fixture f(relic); f.hand(CardId::STRIKE_RED);
        for (int i = 0; i < count; ++i) f.hand(CardId::PAIN);
        f.bc.player.block = 20; f.bc.player.buff<PS::RUPTURE>(2);
        const int hp = f.bc.player.curHp, enemy = f.bc.monsters.arr[0].curHp;
        f.play();
        const int loss = relic == RelicId::TUNGSTEN_ROD ? 0 : count;
        check(hp - f.bc.player.curHp == loss && f.bc.player.block == 20, "Pain separate HP-loss packets bypass block / Rod per packet");
        check(f.bc.player.strength == loss * 2, "Pain self source triggers Rupture per actual loss");
        check(enemy - f.bc.monsters.arr[0].curHp == 6, "Pain queued Rupture does not recalculate already queued Strike damage");
    }
    Fixture buffer; buffer.hand(CardId::DEFEND_RED); buffer.hand(CardId::PAIN); buffer.hand(CardId::PAIN);
    buffer.bc.player.buff<PS::BUFFER>(1); buffer.bc.player.buff<PS::RUPTURE>(2);
    const int hp = buffer.bc.player.curHp; buffer.play();
    check(hp - buffer.bc.player.curHp == 1 && !buffer.bc.player.hasStatus<PS::BUFFER>(), "Buffer blocks one Pain packet only");
    check(buffer.bc.player.strength == 2, "blocked Pain does not trigger Rupture");
    Fixture lethal; lethal.hand(CardId::STRIKE_RED); lethal.hand(CardId::PAIN); lethal.bc.player.curHp = 1;
    const int enemy = lethal.bc.monsters.arr[0].curHp; lethal.play();
    check(lethal.bc.player.curHp == 0 && lethal.bc.monsters.arr[0].curHp == enemy,
          "Pain loss precedes queued attack and can kill player before damage");
}
void painIdentity() {
    for (int resident : {0, 1, 2}) {
        Fixture manual(RelicId::BLUE_CANDLE); manual.hand(CardId::PAIN);
        for (int i = 0; i < resident; ++i) manual.hand(CardId::PAIN);
        const int hp = manual.bc.player.curHp; manual.play();
        check(hp - manual.bc.player.curHp == resident + 1, "manual Pain excludes itself, includes other Pain identities and Candle cost");
        check(manual.bc.cards.handPainCount == resident && manual.bc.cards.exhaustPile.size() == 1, "manual Pain hand counter cleanup");
        Fixture automatic(RelicId::BLUE_CANDLE);
        for (int i = 0; i < resident; ++i) automatic.hand(CardId::PAIN);
        automatic.bc.cards.drawPile.push_back(automatic.card(CardId::PAIN));
        const int before = automatic.bc.player.curHp;
        automatic.bc.addToBot(Actions::PlayTopCard(0, false)); automatic.resolve();
        check(before - automatic.bc.player.curHp == resident + 1,
              "autoplay Pain not in hand must not subtract a different resident Pain");
        check(automatic.bc.cards.handPainCount == resident, "autoplay Pain preserves resident count");
    }
    Fixture rejected; rejected.hand(CardId::PAIN); rejected.bc.cards.drawPile.push_back(rejected.card(CardId::WOUND));
    const int hp = rejected.bc.player.curHp;
    rejected.bc.addToBot(Actions::PlayTopCard(0, true)); rejected.resolve();
    check(rejected.bc.player.curHp == hp && rejected.bc.player.cardsPlayedThisTurn == 0,
          "rejected autoplay does not trigger Pain or increment plays");
    Fixture end; end.hand(CardId::PAIN); end.hand(CardId::DOUBT);
    const int before = end.bc.player.curHp;
    search::Action(search::ActionType::END_TURN).execute(end.bc);
    check(end.bc.player.curHp == before && end.bc.cards.handPainCount == 0,
          "no-use end-hand callback does not trigger Pain and discard clears resident count");
}
void normalityGates() {
    for (int played : {0, 2, 3, 5}) for (int count : {1, 2}) {
        Fixture f(RelicId::BLUE_CANDLE); f.hand(CardId::STRIKE_RED);
        for (int i = 0; i < count; ++i) f.hand(CardId::NORMALITY);
        f.bc.player.cardsPlayedThisTurn = played;
        check(f.bc.isCardPlayAllowed() == (played < 3), "Normality global manual gate");
        check(f.bc.cards.hand[0].canUse(f.bc, 0, false) == (played < 3), "Normality individual manual legality");
        check(f.bc.cards.hand[0].canUse(f.bc, 0, true) == (played < 3), "Normality autoplay legality");
        check(f.bc.cards.hand[1].canUse(f.bc, 0, false) == (played < 3), "Candle cannot bypass Normality limit");
    }
    for (bool exhaust : {false, true}) {
        Fixture f; f.hand(CardId::NORMALITY); f.bc.player.cardsPlayedThisTurn = 3;
        f.bc.cards.drawPile.push_back(f.card(CardId::STRIKE_RED));
        const int enemy = f.bc.monsters.arr[0].curHp;
        f.bc.addToBot(Actions::PlayTopCard(0, exhaust)); f.resolve();
        check(f.bc.monsters.arr[0].curHp == enemy && f.bc.player.cardsPlayedThisTurn == 3,
              "Normality rejects queued autoplay effect and play count");
        check((exhaust ? f.bc.cards.exhaustPile : f.bc.cards.discardPile).size() == 1,
              "Normality rejected autoplay settles lifecycle");
    }
    Fixture removal(RelicId::BLUE_CANDLE); removal.hand(CardId::NORMALITY); removal.hand(CardId::STRIKE_RED);
    removal.bc.player.cardsPlayedThisTurn = 2; removal.play();
    check(removal.bc.cards.handNormalityCount == 0 && removal.bc.player.cardsPlayedThisTurn == 3 &&
          removal.bc.cards.hand[0].canUse(removal.bc, 0, false), "removing last Normality lifts restriction immediately");
    Fixture late; late.hand(CardId::STRIKE_RED); late.bc.player.cardsPlayedThisTurn = 3;
    check(late.bc.cards.hand[0].canUse(late.bc, 0, false), "no resident Normality no limit");
    late.bc.cards.drawPile.push_back(late.card(CardId::NORMALITY));
    late.bc.addToBot(Actions::DrawCards(1)); late.resolve();
    check(!late.bc.cards.hand[0].canUse(late.bc, 0, false), "late draw uses all prior turn plays");
}
void repeats() {
    Fixture pain; pain.hand(CardId::DOUBLE_TAP); pain.hand(CardId::STRIKE_RED); pain.hand(CardId::PAIN);
    const int hp = pain.bc.player.curHp; pain.play(); pain.play();
    check(hp - pain.bc.player.curHp == 3 && pain.bc.player.cardsPlayedThisTurn == 3,
          "Pain triggers on Double Tap and both attacks, not once per manual action");
    Fixture normal; normal.hand(CardId::DOUBLE_TAP); normal.hand(CardId::STRIKE_RED); normal.hand(CardId::NORMALITY);
    normal.bc.player.cardsPlayedThisTurn = 1; const int enemy = normal.bc.monsters.arr[0].curHp;
    normal.play(); normal.play();
    check(normal.bc.player.cardsPlayedThisTurn == 3 && enemy - normal.bc.monsters.arr[0].curHp == 6,
          "Normality also rejects purged Double Tap replay at the limit");
    check(normal.bc.cards.discardPile.size() == 2 && normal.bc.cards.exhaustPile.empty(),
          "rejected repeat copy vanishes without duplicating original or firing exhaust callbacks");
}
void movementAndTurns() {
    Fixture f; f.hand(CardId::SECOND_WIND); f.hand(CardId::PAIN); f.hand(CardId::NORMALITY); f.hand(CardId::STRIKE_RED);
    f.bc.player.cardsPlayedThisTurn = 2;
    const int hp = f.bc.player.curHp; f.play();
    check(hp - f.bc.player.curHp == 1 && f.bc.player.block == 10,
          "Pain already queued still loses HP before Second Wind exhausts it");
    check(f.bc.cards.handPainCount == 0 && f.bc.cards.handNormalityCount == 0 && f.bc.cards.exhaustPile.size() == 2,
          "bulk exhaust clears both resident counters");
    check(f.bc.cards.hand[0].canUse(f.bc, 0, false), "third-card Second Wind removes Normality and permits fourth card");
    Fixture multiple(RelicId::BLUE_CANDLE); multiple.hand(CardId::NORMALITY); multiple.hand(CardId::NORMALITY);
    multiple.hand(CardId::STRIKE_RED); multiple.bc.player.cardsPlayedThisTurn = 2; multiple.play();
    check(multiple.bc.cards.handNormalityCount == 1 && !multiple.bc.cards.hand[1].canUse(multiple.bc, 0, false),
          "removing only one of two Normalities does not lift limit");
    Fixture next; next.hand(CardId::NORMALITY); next.hand(CardId::STRIKE_RED); next.bc.player.cardsPlayedThisTurn = 3;
    search::Action(search::ActionType::END_TURN).execute(next.bc);
    check(next.bc.player.cardsPlayedThisTurn == 0 && next.bc.cards.handNormalityCount == 0,
          "new turn resets play count and discarded Normality leaves no restriction");
    Fixture automatic; automatic.hand(CardId::PAIN); automatic.hand(CardId::BLOOD_FOR_BLOOD);
    automatic.bc.cards.drawPile.push_back(automatic.card(CardId::STRIKE_RED));
    const int before = automatic.bc.player.curHp;
    automatic.bc.addToBot(Actions::PlayTopCard(0, false)); automatic.resolve();
    check(before - automatic.bc.player.curHp == 1 && automatic.bc.player.cardsPlayedThisTurn == 1,
          "successful autoplay triggers resident Pain");
    check(automatic.bc.cards.hand[1].cost == 3 && automatic.bc.cards.hand[1].costForTurn == 3,
          "Pain HP loss lowers Blood for Blood in active hand");
}
void publicCounterSampling() {
    const std::array<std::uint64_t, 7> seeds {11,22,33,44,55,66,77};
    for (int count : {0, 2, 3, 7}) {
        Fixture f; f.hand(CardId::NORMALITY); f.hand(CardId::STRIKE_RED); f.hand(CardId::PAIN);
        f.bc.player.cardsPlayedThisTurn = count;
        auto a = f.bc, b = f.bc;
        std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, a, seeds);
        public_sampling::resampleCombatContinuation(f.game, b, seeds);
        check(a.player.cardsPlayedThisTurn == count && b.player.cardsPlayedThisTurn == count,
              "public actual play count survives hidden-world resampling");
        check(a.cards.handNormalityCount == 1 && a.cards.handPainCount == 1,
              "resident public identities survive resampling");
        check(a.cards.hand[1].canUse(a, 0, false) == (count < 3), "sample legality uses public play count");
        for (std::size_t i = 0; i < a.cards.drawPile.size(); ++i)
            check(a.cards.drawPile[i].getUniqueId() == b.cards.drawPile[i].getUniqueId(), "paired hidden-order invariance");
    }
}
}
int main() {
    painPackets(); painIdentity(); normalityGates(); repeats(); movementAndTurns(); publicCounterSampling();
    std::cout << "HAND_RESIDENT_CURSE_FOUNDATION " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
