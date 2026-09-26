#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <vector>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/Game.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 16) std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(bool chemical = false) : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1; game.relics = {};
        if (chemical) game.relics.add({RelicId::CHEMICAL_X});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 20; b.player.curHp = 70; b.player.maxHp = 80;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
    }
    void run() { b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(CardId id, bool up = false, bool free = false) {
        b.cards.createTempCardInHand(CardInstance(id, up));
        auto item = CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy);
        item.freeToPlay = free; b.addToBotCard(item); run();
    }
};
std::vector<CardId> expected(Random &rng, int count) {
    std::vector<CardId> ids;
    for (int i = 0; i < count; ++i) ids.push_back(getTrulyRandomColorlessCardInCombat(rng));
    return ids;
}
void transmutation() {
    std::set<CardId> seen;
    for (bool up : {false, true}) for (bool chemical : {false, true})
    for (bool free : {false, true}) for (int energy : {0, 1, 3, 8})
    for (int occupied : {0, 9}) for (int seed = 1; seed <= 24; ++seed) {
        Fixture f(chemical); f.b.player.energy = energy; f.b.player.buff<PS::NO_DRAW>();
        f.b.player.buff<PS::FEEL_NO_PAIN>(3);
        for (int i = 0; i < occupied; ++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        f.b.cardRandomRng = Random(seed); auto rng = f.b.cardRandomRng;
        const int count = energy + (chemical ? 2 : 0);
        const auto ids = expected(rng, count);
        f.play(CardId::TRANSMUTATION, up, free);
        const int handCount = std::min(count, 10 - occupied);
        check(f.b.player.energy == (free ? energy : 0), "X generation spends energy only when not free");
        check(f.b.cards.cardsInHand == occupied + handCount, "X/Chemical X count respects hand capacity and ignores No Draw");
        check(f.b.cards.discardPile.size() == static_cast<unsigned>(count - handCount), "overflow enters discard");
        check(f.b.cards.exhaustPile.size() == 1 && f.b.player.block == 3, "generator exhausts exactly once and triggers Feel No Pain");
        check(f.b.cardRandomRng.counter == rng.counter, "one random identity per generated card");
        for (int i = 0; i < count; ++i) {
            const auto &c = i < handCount ? f.b.cards.hand[occupied + i] : f.b.cards.discardPile[i - handCount];
            const CardInstance reference(ids[i], up); seen.insert(c.id);
            check(c.id == ids[i] && c.upgraded == up, "ordered random copies use generator upgrade");
            check(c.uniqueId == 101 + occupied + i, "generated instances get fresh identities");
            // MakeTempCardInHand overflow -> ShowCardAndAddToDiscardEffect -> Soul:
            // the hand keeps temporary zero, settled discard restores combat cost.
            const int expectedTurn = i < handCount && reference.cost >= 0 ? 0 : reference.cost;
            check(c.cost == reference.cost && c.costForTurn == expectedTurn,
                  "hand temporary zero and X survive; settled overflow restores combat cost");
        }
        f.b.cards.resetAttributesAtEndOfTurn();
        for (int i = 0; i < count; ++i) {
            const auto &c = i < handCount ? f.b.cards.hand[occupied + i] : f.b.cards.discardPile[i - handCount];
            check(c.costForTurn == CardInstance(ids[i], up).cost, "generated temporary costs reset in both piles");
        }
    }
    check(seen.size() == 34 && !seen.count(CardId::BANDAGE_UP), "corpus reaches all 34 non-healing colorless cards");
}
void magnetism() {
    Fixture empty; empty.play(CardId::MAGNETISM);
    check(empty.b.outcome == Outcome::UNDECIDED && empty.b.inputState == InputState::PLAYER_NORMAL,
          "active Magnetism prevents premature no-cards loss");
    if (empty.b.outcome == Outcome::UNDECIDED) {
        search::Action(search::ActionType::END_TURN).execute(empty.b);
        check(empty.b.cards.cardsInHand == 1, "empty deck can continue by generating next turn");
    }
    for (bool up : {false, true}) for (int stacks : {1, 2, 4}) for (int seed = 1; seed <= 32; ++seed) {
        Fixture f;
        for (int i = 0; i < 10; ++i) f.b.cards.createTempCardInDrawPile(i, CardInstance(CardId::DEFEND_RED));
        for (int i = 0; i < stacks; ++i) f.play(CardId::MAGNETISM, up);
        check(f.b.player.energy == 20 - stacks * (up ? 1 : 2), "Magnetism upgrade reduces only play cost");
        check(f.b.player.getStatus<PS::MAGNETISM>() == stacks && f.b.cards.cardsInHand == 0, "registration stacks without immediate generation");
        check(f.b.cards.discardPile.empty() && f.b.cards.exhaustPile.empty(), "played power leaves active piles");
        f.b.cardRandomRng = Random(seed); auto rng = f.b.cardRandomRng;
        const auto ids = expected(rng, stacks);
        search::Action(search::ActionType::END_TURN).execute(f.b);
        check(f.b.cards.cardsInHand == stacks + 5, "next turn generates one card per stack plus normal draws");
        check(f.b.cardRandomRng.counter == rng.counter, "Magnetism consumes only its generation RNG");
        if (f.b.cards.cardsInHand != stacks + 5) continue;
        for (int i = 0; i < stacks; ++i) {
            const auto &c = f.b.cards.hand[i];
            check(c.id == ids[i] && !c.upgraded, "base copies inserted before normal turn draws in sampled order");
            check(c.cost == CardInstance(c.id).cost && c.costForTurn == c.cost, "Magnetism does not discount generated cards");
        }
        check(f.b.cards.hand[stacks].id == CardId::DEFEND_RED, "normal draw follows generation");
    }
    for (int occupied : {0, 9, 10}) {
        Fixture f; f.b.player.buff<PS::MAGNETISM>(3); f.b.player.buff<PS::NO_DRAW>();
        f.b.player.buff<PS::CONFUSED>();
        for (int i = 0; i < occupied; ++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        auto rng = f.b.cardRandomRng; const auto ids = expected(rng, 3);
        f.b.player.applyStartOfTurnPowers(f.b);
        check(f.b.cards.cardsInHand == occupied && f.b.cardRandomRng.counter == rng.counter, "identity sampled at trigger, insertion queued");
        f.run();
        const int inHand = std::min(3, 10 - occupied);
        check(f.b.cards.cardsInHand == occupied + inHand && f.b.cards.discardPile.size() == static_cast<unsigned>(3 - inHand), "No Draw permits generation and overflow");
        check(f.b.cardRandomRng.counter == rng.counter, "generation does not trigger Confusion draw rolls");
        if (f.b.cards.cardsInHand != occupied + inHand || f.b.cards.discardPile.size() != static_cast<unsigned>(3 - inHand)) continue;
        for (int i = 0; i < 3; ++i) {
            const auto &c = i < inHand ? f.b.cards.hand[occupied + i] : f.b.cards.discardPile[i - inHand];
            check(c.id == ids[i] && c.costForTurn == CardInstance(ids[i]).cost, "normal cost and order across overflow");
        }
    }
    Fixture dead; dead.b.player.buff<PS::MAGNETISM>(); dead.b.monsters.arr[0].curHp = 0;
    dead.b.monsters.monstersAlive = 0;
    const auto counter = dead.b.cardRandomRng.counter;
    dead.b.player.applyStartOfTurnPowers(dead.b);
    check(dead.b.cardRandomRng.counter == counter && dead.b.actionQueue.isEmpty(), "no generation after monsters are basically dead");
}
void roots() {
    for (bool frozen : {false, true}) for (int seed = 1; seed <= 24; ++seed) {
        Fixture f;
        if (frozen) f.b.player.setHasRelic<RelicId::FROZEN_EYE>(true);
        for (int i = 0; i < 8; ++i) f.b.cards.createTempCardInDrawPile(i, CardInstance(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED));
        f.play(CardId::MAGNETISM);
        auto twin = f.b;
        if (!frozen) std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        twin.cardRandomRng = Random(9999);
        const std::array<std::uint64_t, 7> seeds{11, static_cast<std::uint64_t>(seed), 33, 44, 55, 66, 77};
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        search::Action(search::ActionType::END_TURN).execute(f.b);
        search::Action(search::ActionType::END_TURN).execute(twin);
        check(f.b.player.getStatus<PS::MAGNETISM>() == 1 && f.b.cards.cardsInHand == 6, "resolved sample preserves active generator and executes turn");
        for (int i = 0; i < f.b.cards.cardsInHand; ++i) check(f.b.cards.hand[i].id == twin.cards.hand[i].id, "paired public worlds ignore original hidden order/RNG");
        const auto card = f.b.cards.hand[0];
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        check(f.b.cards.hand[0].uniqueId == card.uniqueId && f.b.cards.hand[0].costForTurn == card.costForTurn, "resolved generated card identity/cost survives next sampling");
    }
    for (int seed = 1; seed <= 24; ++seed) {
        Fixture f; f.b.player.energy = 3;
        for (int i = 0; i < 8; ++i) f.b.cards.createTempCardInDrawPile(i, CardInstance(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED));
        auto twin = f.b; std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        twin.cardRandomRng = Random(9999);
        const std::array<std::uint64_t, 7> seeds{11, static_cast<std::uint64_t>(seed), 33, 44, 55, 66, 77};
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        f.play(CardId::TRANSMUTATION, true);
        twin.cards.createTempCardInHand(CardInstance(CardId::TRANSMUTATION, true));
        twin.addToBotCard(CardQueueItem(twin.cards.hand[0], 0, 3));
        twin.inputState = InputState::EXECUTING_ACTIONS; twin.executeActions();
        check(f.b.cards.cardsInHand == 3 && twin.cards.cardsInHand == 3, "paired X roots generate three upgraded copies");
        for (int i = 0; i < 3; ++i) check(f.b.cards.hand[i].id == twin.cards.hand[i].id && f.b.cards.hand[i].costForTurn == twin.cards.hand[i].costForTurn, "paired X worlds ignore original hidden RNG/order");
        const auto card = f.b.cards.hand[0];
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        check(f.b.cards.hand[0].uniqueId == card.uniqueId && f.b.cards.hand[0].costForTurn == card.costForTurn, "resolved X root retains generated costs/identities");
    }
}
void xInteractions() {
    for (bool up : {false, true}) {
        Fixture autoPlay; autoPlay.b.player.energy = 3;
        autoPlay.b.cards.createTempCardInDrawPile(0, CardInstance(CardId::TRANSMUTATION, up));
        autoPlay.b.playTopCardInDrawPile(0, true); autoPlay.run();
        check(autoPlay.b.cards.cardsInHand == 3 && autoPlay.b.player.energy == 3, "autoplay X generates from energy without spending it");
        Fixture corruption; corruption.b.player.energy = 3; corruption.b.player.buff<PS::CORRUPTION>();
        corruption.play(CardId::TRANSMUTATION, up);
        check(corruption.b.cards.cardsInHand == 3 && corruption.b.player.energy == 0, "Corruption does not make X-cost Transmutation free");
        Fixture draw; draw.b.player.energy = 3; draw.b.player.buff<PS::DARK_EMBRACE>();
        draw.b.cards.createTempCardInDrawPile(0, CardInstance(CardId::DEFEND_RED));
        draw.play(CardId::TRANSMUTATION, up);
        check(draw.b.cards.cardsInHand == 4 && draw.b.cards.hand[3].id == CardId::DEFEND_RED, "generator exhaustion draws after queued generation");
    }
}
}
int main() {
    std::cerr << "Transmutation matrix\n";
    transmutation();
    std::cerr << "Magnetism matrix\n";
    magnetism();
    std::cerr << "Resolved roots\n";
    roots();
    xInteractions();
    std::cout << "COLORLESS_X_TURN_GENERATION_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
