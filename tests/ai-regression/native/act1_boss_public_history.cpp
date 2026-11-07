#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;
using namespace sts::public_sampling;

namespace {
int roots = 0;
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F test, const char *why) {
    try { test(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error(why);
}
std::string dump(const BattleContext &battle) {
    std::ostringstream out; out << battle; return out.str();
}
const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};

struct Fixture {
    GameContext game;
    BattleContext battle;
    PublicEnemyHistory history;
    explicit Fixture(MonsterEncounter encounter, int seed = 123456789)
        : game(CharacterClass::IRONCLAD, seed, 0) {
        game.floorNum = 16;
        game.enterBattle(encounter);
        battle.init(game);
        battle.executeActions();
        // Synthetic durability only: exercise long cycles without a survival policy.
        battle.player.curHp = battle.player.maxHp = 1000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(battle)); }
    void endTurn() {
        search::Action(search::ActionType::END_TURN).execute(battle);
        require(battle.inputState == InputState::PLAYER_NORMAL, "turn not resolved");
        observe();
    }
    void hit(int idx, int damage) {
        battle.monsters.arr[idx].block = 0;
        battle.monsters.arr[idx].damage(battle, damage);
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
        observe();
    }
};

void sample(Fixture &f) {
    const auto source = dump(f.battle);
    const auto visible = publicEnemyFrame(f.battle);
    BattleContext a(f.battle), b(f.battle);
    b.aiRng = Random(9001);
    b.miscRng = Random(9002);
    b.monsterHpRng = Random(9003);
    b.shuffleRng = Random(9004);
    std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
    for (int i = 0; i < b.monsters.monsterCount; ++i) {
        auto &m = b.monsters.arr[i];
        // Poison nonpublic caches, but NOT a currently revealed Divider attack.
        if (m.moveHistory[0] != MMID::HEXAGHOST_DIVIDER) m.miscInfo = 987;
        if (m.id == MonsterId::HEXAGHOST) m.uniquePower0 = 93;
        m.moveHistory[1] = MMID::INVALID;
    }
    require(publicEnemyFrame(b) == visible, "test poisoned a public field");
    resampleCombatContinuation(f.game, a, seeds, -1, &f.history);
    resampleCombatContinuation(f.game, b, seeds, -1, &f.history);
    require(publicEnemyFrame(a) == visible, "sample changed public enemy state");
    require(dump(a) == dump(b), "sample depends on hidden RNG/pile/cache/history");
    require(dump(f.battle) == source, "sampling mutated source");
    const auto &m = f.battle.monsters.arr[0];
    if (m.id == MonsterId::THE_GUARDIAN) {
        require(a.monsters.arr[0].miscInfo == m.miscInfo, "Guardian threshold reconstruction");
    } else if (m.id == MonsterId::HEXAGHOST) {
        require(a.monsters.arr[0].uniquePower0 == m.uniquePower0, "Hexaghost cycle reconstruction");
    } else {
        for (int i = 0; i < f.battle.monsters.monsterCount; ++i) {
            if (!f.battle.monsters.arr[i].isDeadOrEscaped()) {
                require(a.monsters.arr[i].moveHistory[1] == f.battle.monsters.arr[i].moveHistory[1],
                        "slime intent history reconstruction");
            }
        }
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    require(dump(a) == dump(b), "hidden cache changed continuation");
    auto continuationHistory = f.history;
    continuationHistory.observe(publicEnemyFrame(a));
    if (a.outcome == Outcome::UNDECIDED) {
        resampleCombatContinuation(f.game, a, seeds, -1, &continuationHistory);
    }
    ++roots;
}

void guardian() {
    Fixture f(MonsterEncounter::THE_GUARDIAN);
    for (int t = 0; t < 5; ++t) { sample(f); f.endTurn(); }
    // Two full phase switches: remaining shift is not the reset threshold.
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto &m = f.battle.monsters.arr[0];
        const int threshold = m.getStatus<MS::MODE_SHIFT>();
        f.hit(0, 1); sample(f);
        require(m.getStatus<MS::MODE_SHIFT>() == threshold - 1, "shift did not decrement");
        f.hit(0, threshold - 1);
        require(m.moveHistory[0] == MMID::THE_GUARDIAN_DEFENSIVE_MODE, "missing defensive interrupt");
        sample(f); f.endTurn(); sample(f); f.endTurn(); sample(f); f.endTurn(); sample(f);
        require(m.miscInfo == 40 + 10 * cycle, "phase threshold not increasing");
    }
}

void hexaghost() {
    Fixture f(MonsterEncounter::HEXAGHOST);
    std::set<MMID> seen;
    for (int t = 0; t < 18; ++t) {
        seen.insert(f.battle.monsters.arr[0].moveHistory[0]);
        sample(f); f.endTurn();
    }
    require(seen.size() == 6, "did not cover full Hexaghost cycle");
}

void slimeBoss(bool acidFirst) {
    Fixture f(MonsterEncounter::SLIME_BOSS, acidFirst ? 41 : 42);
    for (int t = 0; t < 4; ++t) { sample(f); f.endTurn(); }
    f.hit(0, f.battle.monsters.arr[0].curHp / 2);
    sample(f); f.endTurn(); sample(f);
    require(f.battle.monsters.monsterCount == 3 &&
            f.battle.monsters.arr[1].id == MonsterId::INVALID, "split slot layout");
    for (int i = 0; i < 5; ++i) { f.endTurn(); sample(f); }
    for (const int slot : {acidFirst ? 2 : 0, acidFirst ? 0 : 2}) {
        auto &m = f.battle.monsters.arr[slot];
        f.hit(slot, (m.curHp + 1) / 2);
        sample(f); f.endTurn(); sample(f);
        for (int i = 0; i < 4; ++i) { f.endTurn(); sample(f); }
    }
    require(f.battle.monsters.monsterCount == 4, "four medium slimes not reached");
    // Dead entities retain their target slot; killing one cannot erase its siblings.
    f.hit(0, 999); sample(f); f.endTurn(); sample(f);
}

void refusals() {
    Fixture f(MonsterEncounter::THE_GUARDIAN);
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds); },
            "boss sampled without observed history");
    f.endTurn();
    PublicEnemyHistory late;
    late.observe(publicEnemyFrame(f.battle));
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds, -1, &late); },
            "late-attached history accepted");
    auto stale = f.history;
    f.endTurn();
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds, -1, &stale); },
            "stale history accepted");
    auto high = f.battle; high.ascension = 11;
    rejects([&] { validateEncounterRecipe(high); }, "unaudited ascension accepted");
    auto wrong = f.battle; wrong.monsters.arr[0].id = MonsterId::HEXAGHOST;
    rejects([&] { validateEncounterRecipe(wrong); }, "wrong boss composition accepted");
    Fixture s(MonsterEncounter::SLIME_BOSS);
    s.hit(0, s.battle.monsters.arr[0].curHp / 2); s.endTurn();
    auto hole = s.battle; hole.monsters.arr[1].curHp = 10;
    rejects([&] { validateEncounterRecipe(hole); }, "living empty split slot accepted");
}
}

int main() {
    try {
        guardian(); hexaghost(); slimeBoss(false); slimeBoss(true); refusals();
        std::cout << "act1-boss-public-history-v1: " << roots << " multi-phase roots passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
