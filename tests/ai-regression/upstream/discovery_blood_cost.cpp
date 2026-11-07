// Standalone regression: upstream master plus only the Discovery cost fix.
#include <algorithm>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/Game.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"

using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game);
        b.executeActions();
        b.cards = CardManager{};
        b.cards.nextUniqueCardId = 100;
        b.player.energy = 3;
        b.player.curHp = 70;
        b.player.maxHp = 80;
        b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
    }
    void play(CardId id, bool upgraded = false) {
        b.cards.createTempCardInHand(CardInstance(id, upgraded));
        b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy));
        b.inputState = InputState::EXECUTING_ACTIONS;
        b.executeActions();
    }
};
int seedFor(CardId wanted) {
    // Fixture-only seed selection, not a policy peeking at a live game's RNG.
    for (int seed = 1; seed < 10000; ++seed) {
        Random rng(seed);
        const auto ids = generateDiscoveryCards(rng, CharacterClass::IRONCLAD, CardType::INVALID);
        if (std::find(ids.begin(), ids.end(), wanted) != ids.end()) return seed;
    }
    check(false, "fixed seed corpus reaches requested candidate");
    return 1;
}
void select(Fixture &f, CardId wanted, bool upgraded) {
    f.b.cardRandomRng = Random(seedFor(wanted));
    f.play(CardId::DISCOVERY, upgraded);
    check(f.b.inputState == InputState::CARD_SELECT, "Discovery opens candidate selection");
    const auto &ids = f.b.cardSelectInfo.cards;
    const auto found = std::find(ids.begin(), ids.end(), wanted);
    check(found != ids.end(), "wanted card is an actual offered candidate");
    if (found == ids.end()) return;
    auto action = search::Action(search::ActionType::SINGLE_CARD_SELECT,
                                static_cast<int>(found - ids.begin()));
    check(action.isValidAction(f.b), "offered choice is legal");
    action.execute(f.b);
    check(f.b.inputState == InputState::PLAYER_NORMAL, "choice resumes the action queue");
}
void damageHistory() {
    for (bool upgraded : {false, true}) for (int hits : {0, 1, 2, 4, 6}) {
        Fixture f;
        for (int i = 0; i < hits; ++i) f.play(CardId::BLOODLETTING);
        check(f.b.player.timesDamagedThisCombat == hits, "actual HP-loss plays update damage history");
        select(f, CardId::BLOOD_FOR_BLOOD, upgraded);
        check(f.b.cards.cardsInHand == 1, "both Discovery variants generate one card");
        if (f.b.cards.cardsInHand != 1) continue;
        const int expected = std::max(0, 4 - hits);
        auto &card = f.b.cards.hand[0];
        check(card.id == CardId::BLOOD_FOR_BLOOD && !card.upgraded, "chosen card is base Blood for Blood");
        check(card.cost == expected, "generated combat cost inherits prior HP-loss events");
        check(card.costForTurn == 0, "Discovery still grants temporary zero cost");
        check(f.b.cards.exhaustPile.size() == (upgraded ? 0u : 1u), "generator exhaust behavior is unchanged");
        f.b.cards.resetAttributesAtEndOfTurn();
        check(card.costForTurn == expected, "turn reset retains prior damage reduction");
        f.b.player.energy = expected;
        check(card.canUse(f.b, 0, false), "reset card is playable at its discounted energy cost");
        if (expected > 0) {
            f.b.player.energy = expected - 1;
            check(!card.canUse(f.b, 0, false), "discount does not make reset card free");
        }
        if (hits == 2) {
            std::cout << "After two HP-loss events: combat_cost=" << int(card.cost)
                      << ", reset_cost=" << int(card.costForTurn) << '\n';
        }
    }
}
void controls() {
    for (CardId wanted : {CardId::BLUDGEON, CardId::ANGER, CardId::WHIRLWIND}) {
        Fixture f;
        f.play(CardId::BLOODLETTING);
        f.play(CardId::BLOODLETTING);
        select(f, wanted, false);
        check(f.b.cards.cardsInHand == 1, "control generated one card");
        if (f.b.cards.cardsInHand != 1) continue;
        const int expected = CardInstance(wanted).cost;
        const auto &card = f.b.cards.hand[0];
        check(card.cost == expected, "other generated combat costs are unchanged");
        check(card.costForTurn == (expected >= 0 ? 0 : expected), "temporary zero preserves X costs");
        f.b.cards.resetAttributesAtEndOfTurn();
        check(card.costForTurn == expected, "other generated costs reset normally");
    }
    // Exercise the same helper's overflow branch without requiring autoplay fixes.
    Fixture f;
    f.play(CardId::BLOODLETTING);
    f.play(CardId::BLOODLETTING);
    for (int i = 0; i < 10; ++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
    f.b.cardSelectInfo.data0 = 1;
    const auto previous = f.b.cards.discardPile.size();
    f.b.chooseDiscoveryCard(CardId::BLOOD_FOR_BLOOD);
    check(f.b.cards.cardsInHand == 10 && f.b.cards.discardPile.size() == previous + 1,
          "full hand sends generated card to discard");
    const auto &card = f.b.cards.discardPile.back();
    check(card.id == CardId::BLOOD_FOR_BLOOD && card.cost == 2 && card.costForTurn == 0,
          "overflow preserves inherited combat discount and temporary zero");
    f.b.cards.resetAttributesAtEndOfTurn();
    check(card.costForTurn == 2, "overflow card also resets to discounted cost");
}
}
int main() {
    damageHistory();
    controls();
    std::cout << "DISCOVERY_BLOOD_COST_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
