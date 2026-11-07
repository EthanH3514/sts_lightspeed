#include <initializer_list>
#include <iostream>
#include <stdexcept>

#include "combat/BattleContext.h"
#include "game/GameContext.h"

using namespace sts;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

BattleContext battle(MonsterEncounter encounter) {
    GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
    game.enterBattle(encounter);
    BattleContext result;
    result.init(game);
    result.executeActions();
    require(result.inputState == InputState::PLAYER_NORMAL, "fixture is not at a decision");
    return result;
}

void setHand(BattleContext &battle, std::initializer_list<CardInstance> cards) {
    battle.cards.cardsInHand = static_cast<int>(cards.size());
    int index = 0;
    for (auto card : cards) {
        card.setUniqueId(100 + index);
        battle.cards.hand[index++] = card;
    }
}

void play(BattleContext &battle, int handIndex, int target=0) {
    const auto card = battle.cards.hand[handIndex];
    require(card.canUse(battle, target, false), "fixture card is not playable");
    battle.addToBotCard(CardQueueItem(card, target, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
}

void ironWaveBlockIsModifiedOnce() {
    auto dexterity = battle(MonsterEncounter::CULTIST);
    setHand(dexterity, {CardInstance(CardId::IRON_WAVE)});
    dexterity.player.buff<PS::DEXTERITY>(2);
    const int hpBefore = dexterity.monsters.arr[0].curHp;
    play(dexterity, 0);
    require(dexterity.player.block == 7, "Iron Wave applied Dexterity more than once");
    require(hpBefore - dexterity.monsters.arr[0].curHp == 5,
            "Iron Wave base damage changed");

    auto frail = battle(MonsterEncounter::CULTIST);
    setHand(frail, {CardInstance(CardId::IRON_WAVE)});
    frail.player.debuff<PS::FRAIL>(1, false);
    play(frail, 0);
    require(frail.player.block == 3, "Iron Wave applied Frail more than once");

    auto upgraded = battle(MonsterEncounter::CULTIST);
    setHand(upgraded, {CardInstance(CardId::IRON_WAVE, true)});
    const int upgradedHp = upgraded.monsters.arr[0].curHp;
    play(upgraded, 0);
    require(upgraded.player.block == 7, "upgraded Iron Wave block changed");
    require(upgradedHp - upgraded.monsters.arr[0].curHp == 7,
            "upgraded Iron Wave damage changed");
}

void cleaveHitsEveryEnemy() {
    for (const bool upgraded : {false, true}) {
        auto context = battle(MonsterEncounter::TWO_LOUSE);
        setHand(context, {CardInstance(CardId::CLEAVE, upgraded)});
        const int firstHp = context.monsters.arr[0].curHp;
        const int secondHp = context.monsters.arr[1].curHp;
        play(context, 0);
        const int expected = upgraded ? 11 : 8;
        require(firstHp - context.monsters.arr[0].curHp == expected,
                "Cleave first-target damage changed");
        require(secondHp - context.monsters.arr[1].curHp == expected,
                "Cleave second-target damage changed");
    }
}

void bodySlamUsesCurrentBlockAndUpgradeCost() {
    for (const bool upgraded : {false, true}) {
        auto context = battle(MonsterEncounter::CULTIST);
        setHand(context, {CardInstance(CardId::BODY_SLAM, upgraded)});
        context.player.block = 13;
        context.player.buff<PS::STRENGTH>(2);
        const int hpBefore = context.monsters.arr[0].curHp;
        const int energyBefore = context.player.energy;
        play(context, 0);
        require(hpBefore - context.monsters.arr[0].curHp == 15,
                "Body Slam did not use current block plus Strength");
        require(context.player.energy == energyBefore - (upgraded ? 0 : 1),
                "Body Slam upgrade cost changed");
    }
}

void clashUsesPublicHandLegality() {
    auto blocked = battle(MonsterEncounter::CULTIST);
    setHand(blocked, {CardInstance(CardId::CLASH), CardInstance(CardId::DEFEND_RED)});
    require(!blocked.cards.hand[0].canUse(blocked, 0, false),
            "Clash ignored a non-attack card in hand");

    for (const bool upgraded : {false, true}) {
        auto context = battle(MonsterEncounter::CULTIST);
        setHand(context, {CardInstance(CardId::CLASH, upgraded),
                          CardInstance(CardId::STRIKE_RED)});
        require(context.cards.hand[0].canUse(context, 0, false),
                "Clash rejected an all-attack hand");
        const int hpBefore = context.monsters.arr[0].curHp;
        play(context, 0);
        require(hpBefore - context.monsters.arr[0].curHp == (upgraded ? 18 : 14),
                "Clash damage changed");
    }
}
}

int main() {
    try {
        ironWaveBlockIsModifiedOnce();
        cleaveHitsEveryEnemy();
        bodySlamUsesCurrentBlockAndUpgradeCost();
        clashUsesPublicHandLegality();
        std::cout << "DETERMINISTIC_CARD_EFFECTS_OK (4 cards)\n";
    } catch (const std::exception &error) {
        std::cerr << "DETERMINISTIC_CARD_EFFECTS_FAILED: " << error.what() << '\n';
        return 1;
    }
}
