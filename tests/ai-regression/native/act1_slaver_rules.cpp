#include <iostream>
#include <stdexcept>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
struct Fixture {
    GameContext g;
    BattleContext b;
    Fixture() : g(CharacterClass::IRONCLAD, 7, 0) {
        g.enterBattle(MonsterEncounter::RED_SLAVER); b.init(g); b.executeActions();
        b.player.curHp = b.player.maxHp = 1000;
    }
};
void usedEntangle(bool artifact) {
    Fixture f;
    auto &m = f.b.monsters.arr[0];
    m.moveHistory[0] = MMID::RED_SLAVER_ENTANGLE;
    m.moveHistory[1] = MMID::RED_SLAVER_STAB;
    m.miscInfo = 0;
    if (artifact) f.b.player.buff<PS::ARTIFACT>(1);
    search::Action(search::ActionType::END_TURN).execute(f.b);
    check(m.miscInfo == 1, "Red Slaver did not remember used Entangle");
    check(f.b.player.hasStatus<PS::ENTANGLED>() != artifact, "Entangle/Artifact effect");
    check(!f.b.player.hasStatus<PS::ARTIFACT>(), "Entangle should consume Artifact");
    CardInstance strike(CardId::STRIKE_RED), defend(CardId::DEFEND_RED);
    check(strike.canUse(f.b, 0, false) == artifact, "Entangle attack legality");
    check(defend.canUse(f.b, 0, false), "Entangle incorrectly blocks skills");
    for (int roll = 0; roll < 100; ++roll) {
        int data = m.miscInfo;
        check(m.getMoveForRoll(f.b, data, roll) != MMID::RED_SLAVER_ENTANGLE,
              "Entangle selected twice");
    }
    search::Action(search::ActionType::END_TURN).execute(f.b);
    check(!f.b.player.hasStatus<PS::ENTANGLED>(), "Entangle did not expire");
}
void moveTable() {
    Fixture f; auto &m = f.b.monsters.arr[0];
    m.miscInfo = 1;
    m.moveHistory[0] = MMID::RED_SLAVER_ENTANGLE;
    m.moveHistory[1] = MMID::RED_SLAVER_STAB;
    int stab = 0;
    for (int roll = 0; roll < 100; ++roll) {
        int data = m.miscInfo;
        const auto actual = m.getMoveForRoll(f.b, data, roll);
        const auto expected = roll >= 55 ? MMID::RED_SLAVER_STAB : MMID::RED_SLAVER_SCRAPE;
        check(actual == expected, "post-Entangle attack threshold should be 55");
        stab += actual == MMID::RED_SLAVER_STAB;
    }
    check(stab == 45, "post-Entangle unconstrained Stab probability is not 45/100");
    m.moveHistory[0] = m.moveHistory[1] = MMID::RED_SLAVER_STAB;
    for (int roll = 0; roll < 100; ++roll) {
        int data = 1;
        check(m.getMoveForRoll(f.b, data, roll) == MMID::RED_SLAVER_SCRAPE, "three consecutive Stabs");
    }
}
}
int main() {
    try { usedEntangle(false); usedEntangle(true); moveTable();
        std::cout << "ACT1_SLAVER_RULES " << checks << " checks passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
