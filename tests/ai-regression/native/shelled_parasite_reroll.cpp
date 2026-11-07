#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
std::string rngState(const Random &rng) {
    std::ostringstream s; s << rng.counter << ':' << rng.seed0 << ':' << rng.seed1; return s.str();
}
void rerolls() {
    GameContext g(CharacterClass::IRONCLAD, 7, 0);
    g.enterBattle(MonsterEncounter::SHELL_PARASITE);
    BattleContext b; b.init(g); b.executeActions();
    auto &m = b.monsters.arr[0];
    const MMID moves[] {MMID::SHELLED_PARASITE_FELL, MMID::SHELLED_PARASITE_DOUBLE_STRIKE,
                        MMID::SHELLED_PARASITE_SUCK};
    std::set<MMID> afterFellReroll;
    for (int seed = 0; seed < 32; ++seed) {
        for (auto current : moves) for (auto previous : moves) for (int roll = 0; roll < 100; ++roll) {
            m.moveHistory[0] = current; m.moveHistory[1] = previous;
            b.aiRng = Random(seed);
            auto expectedRng = b.aiRng;
            MMID expected;
            if (roll < 20 && current != MMID::SHELLED_PARASITE_FELL) {
                expected = MMID::SHELLED_PARASITE_FELL;
            } else {
                // Original getMove retries only the forbidden Fell bucket.
                const int effectiveRoll = roll < 20 ? expectedRng.random(20,99) : roll;
                if (effectiveRoll < 60) expected = current == moves[1] && previous == moves[1] ? moves[2] : moves[1];
                else expected = current == moves[2] && previous == moves[2] ? moves[1] : moves[2];
                if (roll < 20) afterFellReroll.insert(expected);
            }
            int data = m.miscInfo;
            const auto actual = m.getMoveForRoll(b, data, roll);
            check(actual == expected, "Parasite forbidden-Fell reroll ignored its new roll");
            check(rngState(b.aiRng) == rngState(expectedRng), "Parasite reroll RNG consumption");
        }
    }
    check(afterFellReroll.size() == 2, "retry never exercises both follow-up moves");
    // Stunned deliberately writes transient Fell before rolling, as the original
    // setMove appends to history too. Do not remove this as an apparent extra move.
    m.moveHistory[0] = MMID::SHELLED_PARASITE_STUNNED;
    m.moveHistory[1] = MMID::SHELLED_PARASITE_SUCK;
    b.player.curHp = b.player.maxHp = 1000;
    search::Action(search::ActionType::END_TURN).execute(b);
    check(m.moveHistory[1] == MMID::SHELLED_PARASITE_FELL, "post-stun intermediate history");
    check(m.moveHistory[0] != MMID::SHELLED_PARASITE_FELL, "post-stun forbidden Fell");
    check(b.player.curHp == 1000, "stunned parasite attacked");
}
}
int main() {
    try { rerolls(); std::cout << "SHELLED_PARASITE_REROLL " << checks << " checks passed\n"; return 0; }
    catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
