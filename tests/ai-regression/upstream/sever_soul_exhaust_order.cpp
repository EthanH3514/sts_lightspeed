// Standalone regression; no bindings or other pending patches required.
// g++ -std=c++17 -O2 -Iinclude -Ijson/single_include \
//     tests/sever_soul_exhaust_order.cpp $(find src -name '*.cpp') -o sever_soul_exhaust_order
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
struct Fixture {
    GameContext game;
    BattleContext battle;
    explicit Fixture(RelicId relic = RelicId::INVALID)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        if (relic != RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
        // Retain a real deck to avoid the unrelated empty-deck loss guard.
        for (int i = 0; i < 10; ++i) {
            CardInstance card(CardId::DEFEND_RED);
            card.setUniqueId(battle.cards.nextUniqueCardId++);
            battle.cards.drawPile.push_back(card);
        }
        battle.player.energy = 10;
        battle.player.maxHp = 80;
        battle.player.curHp = 70;
        battle.monsters.arr[0].curHp = battle.monsters.arr[0].maxHp = 200;
    }
    void hand(CardId id, bool upgraded = false) {
        CardInstance card(id, upgraded);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
    }
    void play(bool upgraded) {
        hand(CardId::SEVER_SOUL, upgraded);
        auto card = battle.cards.hand[battle.cards.cardsInHand - 1];
        battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }
};
void run() {
    for (bool upgraded : {false, true}) {
        Fixture basic;
        basic.hand(CardId::STRIKE_RED);
        basic.hand(CardId::SENTINEL);
        basic.hand(CardId::WOUND);
        basic.hand(CardId::INFLAME);
        basic.play(upgraded);
        check(basic.battle.player.energy == 10, "cost and Sentinel refund");
        check(basic.battle.monsters.arr[0].curHp == 200 - (upgraded ? 22 : 16), "base/upgraded attack damage");
        const auto &pile = basic.battle.cards.exhaustPile;
        check(pile.size() == 3 && pile[0].uniqueId == 113 && pile[1].uniqueId == 112 && pile[2].uniqueId == 111,
              "reverse original non-attack identity order");
        check(basic.battle.cards.cardsInHand == 1 && basic.battle.cards.hand[0].uniqueId == 110,
              "attack card preserved");
        check(basic.battle.cards.discardPile.size() == 1 && basic.battle.cards.discardPile[0].id == CardId::SEVER_SOUL,
              "played Sever Soul discards normally");

        Fixture ashes(RelicId::CHARONS_ASHES);
        ashes.hand(CardId::WOUND);
        ashes.battle.monsters.arr[0].curHp = 3;
        ashes.battle.monsters.arr[0].buff<MS::THORNS>(5);
        ashes.play(upgraded);
        check(ashes.battle.player.curHp == 70, "Ashes kills before attack can trigger Thorns");
        check(ashes.battle.cards.exhaustPile.size() == 1 && ashes.battle.cards.exhaustPile[0].id == CardId::WOUND,
              "Ashes victory must follow the required exhaustion");
        check(ashes.battle.outcome == Outcome::PLAYER_VICTORY, "Ashes combat still ends in victory");

        Fixture fatal;
        fatal.hand(CardId::WOUND);
        fatal.battle.monsters.arr[0].curHp = 1;
        fatal.play(upgraded);
        check(fatal.battle.cards.exhaustPile.size() == 1,
              "ordinary lethal attack must not skip exhaustion");

        Fixture empty;
        empty.play(upgraded);
        check(empty.battle.monsters.arr[0].curHp == 200 - (upgraded ? 22 : 16) && empty.battle.cards.exhaustPile.empty(),
              "empty remaining hand still attacks");
        Fixture attacks;
        attacks.hand(CardId::STRIKE_RED);
        attacks.hand(CardId::BASH);
        attacks.play(upgraded);
        check(attacks.battle.cards.cardsInHand == 2 && attacks.battle.cards.exhaustPile.empty(),
              "attack-only hand is not exhausted");

        Fixture callbacks;
        callbacks.hand(CardId::SENTINEL, upgraded);
        callbacks.hand(CardId::WOUND);
        callbacks.hand(CardId::STRIKE_RED);
        callbacks.battle.player.buff<PS::FEEL_NO_PAIN>(3);
        callbacks.battle.player.buff<PS::DARK_EMBRACE>(1);
        callbacks.play(upgraded);
        check(callbacks.battle.cards.cardsInHand == 3 && callbacks.battle.cards.exhaustPile.size() == 2,
              "queued Dark Embrace draws are outside the selected exhaust identities");
        check(callbacks.battle.player.block == 6, "Feel No Pain triggers for both exhausted cards");
        check(callbacks.battle.player.energy == 8 + (upgraded ? 3 : 2), "Sentinel on-exhaust energy preserved");
    }
}
}
int main() {
    run();
    std::cout << "SEVER_SOUL_EXHAUST_ORDER_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
