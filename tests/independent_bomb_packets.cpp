// Standalone The Bomb regression; uses upstream interfaces only.
// Original-game reference: TheBomb.use and independent TheBombPower instances.
#include <algorithm>
#include <iostream>
#include <sstream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks; if (!ok) { ++failures; if (failures <= 24) std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    GameContext game; BattleContext b;
    explicit Fixture(int seed = 123, MonsterEncounter encounter = MonsterEncounter::CULTIST)
        : game(CharacterClass::IRONCLAD, seed, 0) {
        game.floorNum = 1; game.relics = {}; game.enterBattle(encounter);
        b.init(game); b.executeActions(); b.cards = CardManager{};
        b.cards.nextUniqueCardId = 100; b.player.energy = 20; b.player.curHp = 70;
        for (int i = 0; i < b.monsters.monsterCount; ++i)
            b.monsters.arr[i].curHp = b.monsters.arr[i].maxHp = 1000;
        for (int i = 0; i < 8; ++i) {
            CardInstance card(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED);
            card.setUniqueId(b.cards.nextUniqueCardId++); b.cards.drawPile.push_back(card);
        }
    }
    void run() { b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(bool upgraded = false) {
        b.cards.createTempCardInHand(CardInstance(CardId::THE_BOMB, upgraded));
        b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand - 1], 0, b.player.energy)); run();
    }
    void tick() { b.player.applyEndOfTurnPowers(b); run(); }
};
void basic() {
    for (bool upgraded : {false, true}) {
        Fixture f; f.play(upgraded);
        check(f.b.player.energy == 18, "both variants cost two energy");
        check(f.b.cards.discardPile.size() == 1 && f.b.cards.exhaustPile.empty(), "Bomb is a normally discarded Skill");
        check(f.b.monsters.arr[0].curHp == 1000, "no immediate damage");
        f.tick(); check(f.b.monsters.arr[0].curHp == 1000, "played-turn end counts down three to two");
        f.tick(); check(f.b.monsters.arr[0].curHp == 1000, "second end counts down two to one");
        f.tick(); check(f.b.monsters.arr[0].curHp == (upgraded ? 950 : 960), "third end detonates once for 40 or 50");
        f.tick(); check(f.b.monsters.arr[0].curHp == (upgraded ? 950 : 960), "expired Bomb does not repeat");
    }
    Fixture f; f.play(); f.tick(); f.play(true); f.tick(); f.tick();
    check(f.b.monsters.arr[0].curHp == 960, "staggered Bombs retain separate countdowns");
    f.tick(); check(f.b.monsters.arr[0].curHp == 910, "later upgraded Bomb detonates one end later");
    Fixture modifier; modifier.b.player.buff<PS::STRENGTH>(20); modifier.b.player.buff<PS::WEAK>(3);
    modifier.b.monsters.arr[0].buff<MS::VULNERABLE>(3); modifier.b.monsters.arr[0].buff<MS::THORNS>(7);
    modifier.play(); modifier.tick(); modifier.tick(); modifier.tick();
    check(modifier.b.monsters.arr[0].curHp == 960 && modifier.b.player.curHp == 70,
          "Bomb is non-attack damage, without Strength/Weak/Vulnerable or attack Thorns callback");
    Fixture corruption; corruption.b.player.buff<PS::CORRUPTION>(); corruption.b.player.buff<PS::FEEL_NO_PAIN>(3);
    corruption.play(); check(corruption.b.player.block == 3 && corruption.b.cards.exhaustPile.size() == 1,
                             "Corruption exhaust callback occurs on Skill use");
    corruption.tick(); corruption.tick(); corruption.tick();
    check(corruption.b.monsters.arr[0].curHp == 960, "exhausting the source does not cancel countdown");
}
void packets() {
    for (int count = 1; count <= 8; ++count) for (bool intangible : {false, true}) {
        Fixture f; if (intangible) f.b.monsters.arr[0].buff<MS::INTANGIBLE>(10);
        int expected = 0;
        for (int i = 0; i < count; ++i) { f.play(i % 2); expected += intangible ? 1 : (i % 2 ? 50 : 40); }
        f.tick(); f.tick(); f.b.player.applyEndOfTurnPowers(f.b);
        check(f.b.actionQueue.size == count, "same-turn Bombs queue independent damage packets");
        f.run(); check(f.b.monsters.arr[0].curHp == 1000 - expected,
                      "separate Bomb hits preserve packet modifiers and totals above signed-byte range");
    }
    Fixture overflow; for (int i = 0; i < 3; ++i) overflow.play(true);
    overflow.tick(); overflow.tick(); overflow.tick();
    check(overflow.b.monsters.arr[0].curHp == 850, "three upgraded Bombs deal 150, not zero after signed-byte overflow");
    Fixture block; block.play(); block.play(true); block.b.monsters.arr[0].buff<MS::INTANGIBLE>(10);
    block.b.monsters.arr[0].block = 1; block.tick(); block.tick(); block.tick();
    check(block.b.monsters.arr[0].curHp == 999 && block.b.monsters.arr[0].block == 0,
          "two intangible packets consume one block then lose one HP");
    Fixture multi(123, MonsterEncounter::SMALL_SLIMES); multi.play(); multi.play(true);
    multi.b.monsters.arr[0].buff<MS::INTANGIBLE>(10); multi.b.monsters.arr[1].block = 45;
    multi.tick(); multi.tick(); multi.tick();
    check(multi.b.monsters.arr[0].curHp == 998 && multi.b.monsters.arr[1].curHp == 955,
          "each living enemy processes each packet through its own modifiers/block");
    Fixture lethal; lethal.play(); lethal.play(); lethal.b.monsters.arr[0].curHp = 30;
    lethal.tick(); lethal.tick(); lethal.tick();
    check(lethal.b.outcome == Outcome::PLAYER_VICTORY, "first lethal explosion safely terminates remaining damage");
    std::ostringstream text; text << block.b;
    check(!text.str().empty(), "diagnostic state remains printable");
}
void copyingAndEmptyDeck() {
    Fixture original; original.play(); original.tick(); original.play(true);
    auto twin = original.b;
    twin.player.buff<PS::THE_BOMB>(50);
    for (int i = 0; i < 3; ++i) {
        original.tick(); twin.player.applyEndOfTurnPowers(twin);
        twin.inputState = InputState::EXECUTING_ACTIONS; twin.executeActions();
    }
    check(original.b.monsters.arr[0].curHp == 910, "copy mutation does not affect original Bombs");
    check(twin.monsters.arr[0].curHp == 860, "copy retains independent Bomb countdown and new packet");

    Fixture empty; empty.play(); empty.b.cards = CardManager{};
    empty.b.monsters.arr[0].curHp = 30;
    empty.run(); check(empty.b.outcome == Outcome::UNDECIDED, "empty deck with three-turn Bomb can continue");
    empty.tick(); check(empty.b.outcome == Outcome::UNDECIDED, "empty deck with two-turn Bomb can continue");
    empty.tick(); check(empty.b.outcome == Outcome::UNDECIDED, "empty deck with one-turn Bomb can continue");
    empty.tick(); check(empty.b.outcome == Outcome::PLAYER_VICTORY, "pending Bomb can win with an empty deck");
}
}
int main() {
    basic(); packets(); copyingAndEmptyDeck();
    std::cout << "INDEPENDENT_BOMB_PACKETS " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
