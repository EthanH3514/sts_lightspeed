// Standalone regression; no project-specific bindings or simulator patches needed.
// Build from the upstream repository root (json submodule initialized):
// g++ -std=c++17 -O2 -Iinclude -Ijson/single_include tests/hand_of_greed_attack.cpp \
//     $(find src -name '*.cpp') -o hand_of_greed_attack
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;

namespace {
int failures = 0;
int cases = 0;

void check(bool condition, const char *name, bool upgraded) {
    ++cases;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << (upgraded ? " (upgraded)" : " (base)") << '\n';
    }
}

struct Fixture {
    GameContext game;
    BattleContext battle;

    explicit Fixture(bool upgraded, RelicId relic = RelicId::INVALID)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        if (relic != RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::SMALL_SLIMES);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.player.maxHp = 80;
        battle.player.curHp = 50;
        battle.player.energy = 3;
        battle.player.gold = 100;
        for (int i = 0; i < 2; ++i) {
            battle.monsters.arr[i].curHp = battle.monsters.arr[i].maxHp = 100;
            battle.monsters.arr[i].block = 0;
        }
        CardInstance card(CardId::HAND_OF_GREED, upgraded);
        card.setUniqueId(100);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
        battle.cards.nextUniqueCardId = 101;
    }

    void play() {
        auto card = battle.cards.hand[0];
        battle.addToBotCard(CardQueueItem(card, 0, battle.player.energy));
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }
};

void run(bool upgraded) {
    const int damage = upgraded ? 25 : 20;
    const int reward = upgraded ? 25 : 20;

    Fixture ordinary(upgraded);
    ordinary.play();
    check(ordinary.battle.monsters.arr[0].curHp == 100 - damage
          && ordinary.battle.player.gold == 100 && ordinary.battle.player.energy == 1
          && ordinary.battle.cards.discardPile.size() == 1,
          "nonfatal damage, cost, discard and no gold", upgraded);

    Fixture thorns(upgraded);
    thorns.battle.monsters.arr[0].buff<MS::THORNS>(3);
    thorns.play();
    check(thorns.battle.player.curHp == 47, "Thorns retaliation", upgraded);

    Fixture angry(upgraded);
    angry.battle.monsters.arr[0].buff<MS::ANGRY>(2);
    angry.play();
    check(angry.battle.monsters.arr[0].getStatus<MS::STRENGTH>() == 2,
          "Angry Strength gain", upgraded);

    Fixture blocked(upgraded);
    blocked.battle.monsters.arr[0].curHp = 1;
    blocked.battle.monsters.arr[0].block = 100;
    blocked.play();
    check(blocked.battle.monsters.arr[0].curHp == 1
          && blocked.battle.monsters.arr[0].block == 100 - damage
          && blocked.battle.player.gold == 100, "blocked hit gives no gold", upgraded);

    Fixture fatal(upgraded);
    fatal.battle.monsters.arr[0].curHp = 1;
    fatal.play();
    check(fatal.battle.monsters.arr[0].curHp == 0
          && fatal.battle.player.gold == 100 + reward, "eligible fatal reward", upgraded);

    Fixture minion(upgraded);
    minion.battle.monsters.arr[0].curHp = 1;
    minion.battle.monsters.arr[0].buff<MS::MINION>();
    minion.play();
    check(minion.battle.monsters.arr[0].curHp == 0
          && minion.battle.player.gold == 100, "Minion gives no gold", upgraded);

    Fixture ectoplasm(upgraded, RelicId::ECTOPLASM);
    ectoplasm.battle.monsters.arr[0].curHp = 1;
    ectoplasm.play();
    check(ectoplasm.battle.player.gold == 100, "Ectoplasm blocks gold", upgraded);

    Fixture last(upgraded);
    last.battle.monsters.arr[0].curHp = 1;
    last.battle.monsters.arr[1].curHp = 0;
    last.battle.monsters.monstersAlive = 1;
    last.play();
    check(last.battle.outcome == Outcome::PLAYER_VICTORY
          && last.battle.player.gold == 100 + reward, "last enemy reward", upgraded);
}
}

int main() {
    run(false);
    run(true);
    if (failures) {
        std::cerr << "HAND_OF_GREED_ATTACK_FAILED (" << failures << '/' << cases << ")\n";
        return 1;
    }
    std::cout << "HAND_OF_GREED_ATTACK_OK (" << cases << " cases)\n";
}
