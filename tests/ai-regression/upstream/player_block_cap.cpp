// Standalone regression; no bindings or other pending simulator fixes required.
// From repository root, with the json submodule initialized:
// g++ -std=c++17 -O2 -Iinclude -Ijson/single_include tests/player_block_cap.cpp \
//     $(find src -name '*.cpp') -o player_block_cap
#include <algorithm>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"

using namespace sts;

namespace {
int cases = 0;
int failures = 0;

void check(bool condition, const char *name, int initialBlock, bool upgraded) {
    ++cases;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << ", initial block=" << initialBlock
                  << (upgraded ? ", upgraded" : ", base") << '\n';
    }
}

struct Fixture {
    GameContext game;
    BattleContext battle;

    Fixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
        // Keep a real remaining deck: consuming the only card as a power would
        // trigger the simulator's empty-deck loss guard, unrelated to block.
        for (auto id : {CardId::STRIKE_RED, CardId::DEFEND_RED, CardId::BASH}) {
            CardInstance card(id);
            card.setUniqueId(battle.cards.nextUniqueCardId++);
            battle.cards.drawPile.push_back(card);
        }
        battle.player.energy = 10;
        battle.player.maxHp = 80;
        battle.player.curHp = 70;
        battle.monsters.arr[0].curHp = battle.monsters.arr[0].maxHp = 200;
    }

    void drain() {
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }

    void play(CardId id, bool upgraded = false) {
        CardInstance card(id, upgraded);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
        battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
        drain();
    }
};

void run() {
    for (bool upgraded : {false, true}) {
        for (int initial : {0, 499, 500, 998, 999}) {
            Fixture f;
            f.battle.player.block = initial;
            f.battle.player.buff<PS::JUGGERNAUT>(5);
            f.play(CardId::ENTRENCH, upgraded);
            check(f.battle.player.block == std::min(999, 2 * initial),
                  "Entrench block cap", initial, upgraded);
            // Positive block-gain events still trigger at 999; zero gains do not.
            check(f.battle.monsters.arr[0].curHp == 200 - (initial > 0 ? 5 : 0),
                  "Juggernaut zero/positive gain callback", initial, upgraded);
        }

        Fixture card;
        card.battle.player.block = 995;
        card.play(CardId::DEFEND_RED, upgraded);
        check(card.battle.player.block == 999, "ordinary card block cap", 995, upgraded);

        Fixture endTurn;
        endTurn.battle.player.block = 998;
        endTurn.play(CardId::BARRICADE);
        endTurn.play(CardId::METALLICIZE, upgraded);
        search::Action(search::ActionType::END_TURN).execute(endTurn.battle);
        // Cultist's opening move is Incantation, so no attack consumes block.
        check(endTurn.battle.player.block == 999,
              "retained end-turn block cap", 998, upgraded);
    }
    for (int amount : {0, -1}) {
        Fixture f;
        f.battle.player.block = 999;
        f.battle.player.buff<PS::JUGGERNAUT>(5);
        f.battle.player.gainBlock(f.battle, amount);
        f.drain();
        check(f.battle.player.block == 999 && f.battle.monsters.arr[0].curHp == 200,
              "nonpositive gain remains a no-op", 999, false);
    }
}
}

int main() {
    run();
    if (failures) {
        std::cerr << "PLAYER_BLOCK_CAP_FAILED (" << failures << '/' << cases << ")\n";
        return 1;
    }
    std::cout << "PLAYER_BLOCK_CAP_OK (" << cases << " cases)\n";
}
