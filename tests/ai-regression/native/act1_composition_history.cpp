#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;
using namespace sts::public_sampling;

namespace {
int roots = 0, compositions = 0;
void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F test, const char *why) {
    try { test(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error(why);
}
std::string dump(const BattleContext &b) { std::ostringstream s; s << b; return s.str(); }
const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
const std::array<MonsterEncounter, 7> batch {MonsterEncounter::LARGE_SLIME,
    MonsterEncounter::LOTS_OF_SLIMES, MonsterEncounter::THREE_LOUSE,
    MonsterEncounter::TWO_FUNGI_BEASTS, MonsterEncounter::EXORDIUM_WILDLIFE,
    MonsterEncounter::LAGAVULIN_EVENT, MonsterEncounter::MUSHROOMS_EVENT};

struct Fixture {
    GameContext game;
    BattleContext battle;
    PublicEnemyHistory history;
    Fixture(MonsterEncounter encounter, int seed = 123456789, int ascension = 0)
        : game(CharacterClass::IRONCLAD, seed, ascension) {
        game.floorNum = 8;
        game.enterBattle(encounter);
        battle.init(game);
        battle.executeActions();
        // Synthetic durability: this fixture tests mechanics, not survival quality.
        battle.player.curHp = battle.player.maxHp = 1000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(battle)); }
    void resetHistory() { history = PublicEnemyHistory(); observe(); }
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
    b.aiRng = Random(9001); b.cardRandomRng = Random(9002);
    b.miscRng = Random(9003); b.monsterHpRng = Random(9004);
    b.potionRng = Random(9005); b.shuffleRng = Random(9006);
    std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
    for (int i = 0; i < b.monsters.monsterCount; ++i) {
        auto &m = b.monsters.arr[i];
        if (!isLouseBite(m.moveHistory[0])) m.miscInfo = 987;
        m.moveHistory[1] = MMID::INVALID;
    }
    require(publicEnemyFrame(b) == visible, "test poisoned public state");
    resampleCombatContinuation(f.game, a, seeds, -1, &f.history);
    resampleCombatContinuation(f.game, b, seeds, -1, &f.history);
    require(publicEnemyFrame(a) == visible, "sample changed public enemy fields");
    require(dump(a) == dump(b), "sample depends on source hidden data");
    require(dump(f.battle) == source, "sampling mutated source");
    for (int i = 0; i < a.monsters.monsterCount; ++i) {
        if (!a.monsters.arr[i].isDeadOrEscaped()) {
            require(a.monsters.arr[i].moveHistory[1] == f.battle.monsters.arr[i].moveHistory[1],
                    "observed intent reconstruction differs from native trajectory");
        }
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    require(dump(a) == dump(b), "hidden fields changed continuation");
    auto childHistory = f.history;
    childHistory.observe(publicEnemyFrame(a));
    if (a.outcome == Outcome::UNDECIDED) resampleCombatContinuation(f.game, a, seeds, -1, &childHistory);
    ++roots;
}

using Composition = std::vector<MonsterId>;
Composition composition(const BattleContext &b) {
    Composition result;
    for (int i = 0; i < b.monsters.monsterCount; ++i) result.push_back(b.monsters.arr[i].id);
    return result;
}
void allCompositions() {
    const std::map<MonsterEncounter, int> expected {{MonsterEncounter::LARGE_SLIME, 4},
        {MonsterEncounter::LOTS_OF_SLIMES, 10}, {MonsterEncounter::THREE_LOUSE, 8},
        {MonsterEncounter::TWO_FUNGI_BEASTS, 1}, {MonsterEncounter::EXORDIUM_WILDLIFE, 8},
        {MonsterEncounter::LAGAVULIN_EVENT, 1}, {MonsterEncounter::MUSHROOMS_EVENT, 1}};
    for (auto encounter : batch) {
        std::set<Composition> declared, seen;
        for (const auto &r : encounterCompositionRecipes) {
            if (r.encounter == encounter) {
                require(r.minimumAscension == 0 && r.maximumAscension == 10, "expected reviewed A0-A10 scope");
                declared.emplace(r.monsters.begin(), r.monsters.begin() + r.monsterCount);
            }
        }
        require(declared.size() == static_cast<std::size_t>(expected.at(encounter)), "declaration count");
        // Bounded seed enumeration obtains every genuine spawn composition; it is
        // not outcome-based policy/evaluation seed selection.
        for (int seed = 0; seed < 512 && seen != declared; ++seed) {
            Fixture f(encounter, seed);
            const auto opening = composition(f.battle);
            require(declared.count(opening), "normal spawn missing from declaration");
            if (!seen.insert(opening).second) continue;
            for (int turn = 0; turn < 5; ++turn) { sample(f); f.endTurn(); }
            if (encounter == MonsterEncounter::LARGE_SLIME) {
                auto &m = f.battle.monsters.arr[0];
                f.hit(0, (m.curHp + 1) / 2);
                require(isSlimeSplit(m.moveHistory[0]), "large slime did not interrupt to split");
                sample(f); f.endTurn();
                require(f.battle.monsters.monsterCount == 2, "large slime children slots");
                seen.insert(composition(f.battle));
                for (int turn = 0; turn < 5; ++turn) { sample(f); f.endTurn(); }
            }
            if (f.battle.monsters.monsterCount > 1) {
                const auto before = composition(f.battle);
                f.hit(0, 9999);
                require(composition(f.battle) == before, "death compacted target slots");
                require(f.battle.monsters.arr[0].isDeadOrEscaped(), "target not dead");
                sample(f); f.endTurn(); sample(f);
            }
        }
        require(seen == declared, "did not exercise all declared compositions including splits");
        compositions += static_cast<int>(seen.size());
    }
}

void fungiEffects() {
    for (bool artifact : {false, true}) {
        Fixture f(MonsterEncounter::TWO_FUNGI_BEASTS);
        auto &remaining = f.battle.monsters.arr[1];
        remaining.moveHistory[0] = MMID::FUNGI_BEAST_BITE;
        remaining.moveHistory[1] = MMID::INVALID;
        f.resetHistory();
        if (artifact) f.battle.player.buff<PS::ARTIFACT>(1);
        f.hit(0, 999);
        require(f.battle.player.getStatus<PS::VULNERABLE>() == (artifact ? 0 : 2),
                "Spore Cloud vulnerability / Artifact interaction");
        require(f.battle.player.getStatus<PS::ARTIFACT>() == 0, "Artifact not consumed");
        sample(f);
        const int hp = f.battle.player.curHp;
        f.endTurn();
        require(hp - f.battle.player.curHp == (artifact ? 6 : 9),
                "Spore Cloud did not affect surviving enemy attack");
    }
    Fixture f(MonsterEncounter::TWO_FUNGI_BEASTS);
    for (int i = 0; i < 2; ++i) {
        f.battle.monsters.arr[i].moveHistory[0] = MMID::FUNGI_BEAST_GROW;
        f.battle.monsters.arr[i].moveHistory[1] = MMID::INVALID;
    }
    f.resetHistory(); sample(f);
    f.endTurn();
    for (int i = 0; i < 2; ++i) {
        require(f.battle.monsters.arr[i].strength == 3, "Fungi Grow strength");
        require(f.battle.monsters.arr[i].moveHistory[0] == MMID::FUNGI_BEAST_BITE,
                "Fungi Grow cannot repeat");
    }
    sample(f);
}

void eventLagavulin() {
    Fixture f(MonsterEncounter::LAGAVULIN_EVENT);
    require(!f.battle.monsters.arr[0].hasStatus<MS::ASLEEP>(), "event Lagavulin starts asleep");
    require(f.battle.monsters.arr[0].moveHistory[0] == MMID::LAGAVULIN_SIPHON_SOUL,
            "event Lagavulin opening");
    sample(f); f.endTurn();
    require(f.battle.player.getStatus<PS::STRENGTH>() == -1 &&
            f.battle.player.getStatus<PS::DEXTERITY>() == -1, "event opening Siphon");
    sample(f);
}

void setLouseIntent(Monster &m, bool bite) {
    m.moveHistory[0] = m.id == MonsterId::GREEN_LOUSE ?
        (bite ? MMID::GREEN_LOUSE_BITE : MMID::GREEN_LOUSE_SPIT_WEB) :
        (bite ? MMID::RED_LOUSE_BITE : MMID::RED_LOUSE_GROW);
    m.moveHistory[1] = MMID::INVALID;
}
void louseDamage() {
    for (int ascension : {0, 1, 2, 17, 20}) {
        Fixture f(MonsterEncounter::TWO_LOUSE, 7, ascension);
        const int low = ascension >= 2 ? 6 : 5;
        for (int i = 0; i < 2; ++i) setLouseIntent(f.battle.monsters.arr[i], false);
        f.resetHistory();
        sample(f);
        rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds); },
                "legacy non-revealing Louse sampled without history");
        std::array<int, 3> frequencies {};
        int differentSlots = 0;
        for (int seed = 0; seed < 600; ++seed) {
            auto s = seeds; s[3] = seed;
            auto a = f.battle, b = f.battle;
            for (int i = 0; i < 2; ++i) b.monsters.arr[i].miscInfo = 900;
            resampleCombatContinuation(f.game, a, s, -1, &f.history);
            resampleCombatContinuation(f.game, b, s, -1, &f.history);
            require(dump(a) == dump(b), "unrevealed damage copied from source");
            for (int i = 0; i < 2; ++i) {
                require(a.monsters.arr[i].miscInfo >= low && a.monsters.arr[i].miscInfo <= low+2,
                        "unrevealed damage outside original constructor range");
            }
            ++frequencies[a.monsters.arr[0].miscInfo - low];
            differentSlots += a.monsters.arr[0].miscInfo != a.monsters.arr[1].miscInfo;
        }
        // Broad, predeclared deterministic smoke bounds; not a posterior proof.
        for (int n : frequencies) require(n >= 130 && n <= 270, "damage prior gross imbalance");
        require(differentSlots >= 300 && differentSlots <= 500, "slot damage RNG not separated");

        // Current reveal is raw base damage, not modified by public Strength/Weak.
        for (int i = 0; i < 2; ++i) {
            auto &m = f.battle.monsters.arr[i];
            setLouseIntent(m, true); m.miscInfo = low + i;
            m.buff<MS::STRENGTH>(3);
        }
        f.resetHistory();
        auto legacy = f.battle;
        resampleCombatContinuation(f.game, legacy, seeds);
        sample(f);
        f.endTurn();
        // Synthetic public intent change exercises retained base even at a
        // non-revealing decision; normal seeded trajectories cover real transitions.
        for (int i = 0; i < 2; ++i) setLouseIntent(f.battle.monsters.arr[i], false);
        f.observe();
        auto sampled = f.battle;
        sampled.monsters.arr[0].miscInfo = 999;
        sampled.monsters.arr[1].miscInfo = 998;
        resampleCombatContinuation(f.game, sampled, seeds, -1, &f.history);
        require(sampled.monsters.arr[0].miscInfo == low &&
                sampled.monsters.arr[1].miscInfo == low+1, "revealed base was forgotten");
    }
}

void refusals() {
    Fixture f(MonsterEncounter::THREE_LOUSE);
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds); },
            "new encounter sampled without history");
    auto high = f.battle; high.ascension = 11;
    rejects([&] { validateEncounterRecipe(high); }, "new high ascension admitted");
    auto wrong = f.battle; wrong.monsters.arr[0].id = MonsterId::CULTIST;
    rejects([&] { validateEncounterRecipe(wrong); }, "wrong composition admitted");
    f.endTurn();
    PublicEnemyHistory late; late.observe(publicEnemyFrame(f.battle));
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds, -1, &late); },
            "late-attached history admitted");
    auto stale = f.history;
    f.endTurn();
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds, -1, &stale); },
            "stale history admitted");
    f.endTurn(); stale.observe(publicEnemyFrame(f.battle));
    rejects([&] { auto b = f.battle; resampleCombatContinuation(f.game, b, seeds, -1, &stale); },
            "missed-turn history admitted");

    Fixture invalid(MonsterEncounter::TWO_LOUSE);
    setLouseIntent(invalid.battle.monsters.arr[0], true);
    invalid.battle.monsters.arr[0].miscInfo = 99;
    invalid.resetHistory();
    rejects([&] { auto b = invalid.battle; resampleCombatContinuation(invalid.game, b, seeds, -1, &invalid.history); },
            "invalid revealed damage admitted");
    Fixture changed(MonsterEncounter::TWO_LOUSE);
    setLouseIntent(changed.battle.monsters.arr[0], true);
    changed.battle.monsters.arr[0].miscInfo = 5; changed.resetHistory();
    changed.battle.monsters.arr[0].miscInfo = 6; changed.observe();
    rejects([&] { auto b = changed.battle; resampleCombatContinuation(changed.game, b, seeds, -1, &changed.history); },
            "contradictory revealed base admitted");
}
}
int main() {
    try {
        allCompositions(); fungiEffects(); eventLagavulin(); louseDamage(); refusals();
        std::cout << "act1-composition-history-v1: " << compositions << " compositions, "
                  << roots << " lifecycle roots; damage prior/memory and refusals passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
