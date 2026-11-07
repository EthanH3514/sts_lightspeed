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
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F f, const char *why) {
    try { f(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error(why);
}
std::string dump(const BattleContext &b) { std::ostringstream s; s << b; return s.str(); }
const std::array<std::uint64_t, 7> seeds {11,22,33,44,55,66,77};
struct Fixture {
    GameContext game;
    BattleContext battle;
    PublicEnemyHistory history;
    Fixture(MonsterEncounter e, int seed = 7) : game(CharacterClass::IRONCLAD, seed, 0) {
        game.floorNum = 8; game.enterBattle(e);
        // Direct encounter fixtures have no map/event reward-screen callback.
        game.regainControlAction = [](GameContext &) {};
        battle.init(game); battle.executeActions();
        battle.player.curHp = battle.player.maxHp = 10000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(battle)); }
    void resetHistory() { history = PublicEnemyHistory(); observe(); }
    void end(bool record = true) {
        search::Action(search::ActionType::END_TURN).execute(battle);
        if (record) observe();
    }
    void hit(int i, int amount = 99999) {
        auto &m = battle.monsters.arr[i]; m.block = 0; m.damage(battle, amount);
        battle.inputState = InputState::EXECUTING_ACTIONS; battle.executeActions(); observe();
    }
};
using Composition = std::vector<MonsterId>;
Composition ids(const BattleContext &b) {
    Composition result;
    for (int i = 0; i < b.monsters.monsterCount; ++i) result.push_back(b.monsters.arr[i].id);
    return result;
}
int seedFor(MonsterEncounter e, const Composition &wanted) {
    for (int seed = 0; seed < 8192; ++seed) {
        Fixture f(e, seed); if (ids(f.battle) == wanted) return seed;
    }
    throw std::runtime_error("bounded spawn enumeration missed composition");
}
void sample(Fixture &f) {
    check(f.battle.outcome == Outcome::UNDECIDED, "sample requested after terminal");
    const auto source = dump(f.battle);
    const auto frameSource = publicEnemyFrame(f.battle);
    auto a = f.battle, b = f.battle;
    b.aiRng = Random(9901); b.cardRandomRng = Random(9902);
    b.miscRng = Random(9903); b.monsterHpRng = Random(9904);
    b.potionRng = Random(9905); b.shuffleRng = Random(9906);
    std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
    for (int i = 0; i < b.monsters.monsterCount; ++i) {
        auto &m = b.monsters.arr[i];
        if (!isLouseBite(m.moveHistory[0])) m.miscInfo = 999;
        m.moveHistory[1] = MMID::INVALID; m.escapeNext = true;
    }
    if (b.encounter == MonsterEncounter::LOOTER || b.encounter == MonsterEncounter::EXORDIUM_THUGS)
        b.setRequiresStolenGoldCheck(!b.requiresStolenGoldCheck());
    check(publicEnemyFrame(b) == frameSource, "poisoned public fields");
    resampleCombatContinuation(f.game, a, seeds, -1, &f.history);
    resampleCombatContinuation(f.game, b, seeds, -1, &f.history);
    check(publicEnemyFrame(a) == frameSource, "resampled public fields changed");
    check(dump(a) == dump(b), "hidden counter/cache/RNG/pile affected sample");
    check(dump(f.battle) == source, "sampling mutated source");
    for (int i = 0; i < a.monsters.monsterCount; ++i) {
        const auto &m = a.monsters.arr[i], &original = f.battle.monsters.arr[i];
        if (!m.isDeadOrEscaped()) {
            check(m.moveHistory[1] == original.moveHistory[1], "public intent reconstruction mismatch");
            if (m.id == MonsterId::RED_SLAVER || m.id == MonsterId::GREMLIN_WIZARD ||
                m.id == MonsterId::LOOTER) check(m.miscInfo == original.miscInfo, "public counter mismatch");
        }
        if (m.id == MonsterId::LOOTER) check(m.miscInfo == original.miscInfo, "dead thief accounting mismatch");
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    check(dump(a) == dump(b), "sampled continuation mismatch");
    if (a.outcome == Outcome::UNDECIDED) {
        auto h = f.history; h.observe(publicEnemyFrame(a));
        resampleCombatContinuation(f.game, a, seeds, -1, &h);
    }
    ++roots;
}
void allCompositions() {
    const std::map<MonsterEncounter, int> expected {{MonsterEncounter::BLUE_SLAVER,1},
        {MonsterEncounter::RED_SLAVER,1}, {MonsterEncounter::LOOTER,1},
        {MonsterEncounter::GREMLIN_GANG,354}, {MonsterEncounter::EXORDIUM_THUGS,16}};
    for (auto [e, count] : expected) {
        std::set<Composition> declared, seen;
        for (const auto &r : encounterCompositionRecipes) if (r.encounter == e)
            declared.emplace(r.monsters.begin(), r.monsters.begin()+r.monsterCount);
        check(declared.size() == static_cast<std::size_t>(count), "composition declaration count");
        for (int seed = 0; seed < 8192 && seen != declared; ++seed) {
            Fixture f(e,seed); const auto c = ids(f.battle);
            check(declared.count(c), "actual spawn not admitted");
            if (!seen.insert(c).second) continue;
            for (int turn = 0; turn < 3 && f.battle.outcome == Outcome::UNDECIDED; ++turn) {
                sample(f); f.end();
            }
            if (f.battle.monsters.monsterCount > 1 && !f.battle.monsters.arr[0].isDeadOrEscaped()) {
                f.hit(0);
                check(ids(f.battle) == c, "dead target slot compacted");
                if (f.battle.outcome == Outcome::UNDECIDED) {
                    sample(f); f.end();
                    if (f.battle.outcome == Outcome::UNDECIDED) sample(f);
                }
            }
        }
        check(seen == declared, "not all declared compositions exercised");
        compositions += static_cast<int>(seen.size());
    }
}
void redSlaverLifecycle() {
    Fixture f(MonsterEncounter::RED_SLAVER, 7);
    int entangles = 0;
    for (int turn = 0; turn < 16; ++turn) {
        entangles += f.battle.monsters.arr[0].moveHistory[0] == MMID::RED_SLAVER_ENTANGLE;
        sample(f); f.end();
    }
    check(entangles == 1, "Red Slaver observed limited-use lifecycle");
    check(f.battle.monsters.arr[0].miscInfo == 1, "observed Entangle flag was lost");
}
void wizardAndShield() {
    const Composition group {MonsterId::MAD_GREMLIN, MonsterId::FAT_GREMLIN,
        MonsterId::SHIELD_GREMLIN, MonsterId::GREMLIN_WIZARD};
    const int seed = seedFor(MonsterEncounter::GREMLIN_GANG, group);
    Fixture f(MonsterEncounter::GREMLIN_GANG, seed);
    for (int turn = 0; turn < 11; ++turn) {
        const bool blast = turn == 2 || turn == 6 || turn == 10;
        check(f.battle.monsters.arr[3].moveHistory[0] == (blast ?
              MMID::GREMLIN_WIZARD_ULTIMATE_BLAST : MMID::GREMLIN_WIZARD_CHARGING),
              "Wizard first/subsequent charge cycle");
        sample(f); f.end();
    }
    Fixture shield(MonsterEncounter::GREMLIN_GANG, seed);
    shield.hit(0); shield.hit(1); shield.hit(3);
    sample(shield); shield.end();
    check(shield.battle.monsters.arr[2].block == 7, "lone Shield should defend itself once");
    check(shield.battle.monsters.arr[2].moveHistory[0] == MMID::SHIELD_GREMLIN_SHIELD_BASH,
          "lone Shield did not switch to attack");
    sample(shield);
    const int hp = shield.battle.player.curHp; shield.end();
    check(hp-shield.battle.player.curHp == 6, "Shield Bash A0 damage");
    sample(shield);

    Fixture targets(MonsterEncounter::GREMLIN_GANG, seed);
    targets.hit(0);
    std::set<int> selected;
    for (int n = 0; n < 120; ++n) {
        auto a = targets.battle, b = targets.battle; auto s = seeds; s[0] = n;
        b.miscRng = Random(n+1); b.aiRng = Random(n+2); b.monsters.arr[2].miscInfo = 999;
        resampleCombatContinuation(targets.game, a, s, -1, &targets.history);
        resampleCombatContinuation(targets.game, b, s, -1, &targets.history);
        Actions::GainBlockRandomEnemy(2,7).actFunc(a);
        Actions::GainBlockRandomEnemy(2,7).actFunc(b);
        check(dump(a) == dump(b), "Shield used source hidden target/RNG");
        int blocks = 0;
        for (int i = 0; i < 4; ++i) if (a.monsters.arr[i].block) {
            check(i == 1 || i == 3, "Shield targeted self/dead ally despite living candidates");
            check(a.monsters.arr[i].block == 7, "Shield block amount");
            selected.insert(i); ++blocks;
        }
        check(blocks == 1, "Shield did not select exactly one ally");
    }
    check(selected.size() == 2, "future Shield sampling never branched");
}
void looterLifecycle() {
    for (int gold : {0, 5, 20, 100}) {
        Fixture f(MonsterEncounter::LOOTER);
        f.battle.player.gold = gold; f.resetHistory();
        sample(f); f.end(); sample(f);
        check(f.battle.player.gold == std::max(0,gold-15), "first theft amount");
        check(f.battle.monsters.arr[0].miscInfo == std::min(gold,15), "first theft counter");
        f.end(); sample(f);
        check(f.battle.monsters.arr[0].miscInfo == std::min(gold,30), "capped second theft");
        for (int t = 0; t < 4 && f.battle.outcome == Outcome::UNDECIDED; ++t) {
            sample(f); f.end();
        }
        check(f.battle.outcome == Outcome::PLAYER_VICTORY && f.battle.monsters.arr[0].isEscaping(),
              "Looter escape did not end single-enemy combat");
        auto escapedGame = f.game; f.battle.exitBattle(escapedGame);
        check(escapedGame.info.stolenGold == 0, "escaped thief refunded gold");
    }
    Fixture killed(MonsterEncounter::LOOTER);
    killed.end(); sample(killed);
    const int stolen = killed.battle.monsters.arr[0].miscInfo;
    auto branch = killed.battle;
    branch.monsters.arr[0].miscInfo = 999;
    branch.setRequiresStolenGoldCheck(false);
    resampleCombatContinuation(killed.game, branch, seeds, -1, &killed.history);
    branch.monsters.arr[0].damage(branch,9999);
    branch.inputState = InputState::EXECUTING_ACTIONS; branch.executeActions();
    auto rewardedGame = killed.game; branch.exitBattle(rewardedGame);
    check(stolen == 15 && rewardedGame.info.stolenGold == stolen, "killed thief refund reconstruction");

    const Composition pair {MonsterId::GREEN_LOUSE, MonsterId::LOOTER};
    const int seed = seedFor(MonsterEncounter::EXORDIUM_THUGS,pair);
    Fixture gain(MonsterEncounter::EXORDIUM_THUGS,seed);
    gain.end(false);
    // Synthetic aggregate observation: a start-turn gold gain masks the theft.
    // Do not interpret net gold change as the amount stolen.
    gain.battle.player.gold += 20; gain.observe(); sample(gain);
    check(gain.battle.monsters.arr[1].miscInfo == 15, "net-gold increase erased theft history");
    gain.hit(1); sample(gain);

    Fixture ambiguous(MonsterEncounter::EXORDIUM_THUGS,seed);
    auto &looter = ambiguous.battle.monsters.arr[1];
    looter.curHp = 1; ambiguous.battle.player.buff<PS::THORNS>(1); ambiguous.observe();
    ambiguous.end();
    check(ambiguous.battle.outcome == Outcome::UNDECIDED && looter.isDeadOrEscaped(),
          "first-Mug thorns-death ambiguity fixture");
    rejects([&]{ auto b=ambiguous.battle;
        resampleCombatContinuation(ambiguous.game,b,seeds,-1,&ambiguous.history); },
        "ambiguous first-Mug death silently copied native counter");
}
void basicEffects() {
    Fixture blue(MonsterEncounter::BLUE_SLAVER);
    blue.battle.monsters.arr[0].moveHistory[0] = MMID::BLUE_SLAVER_RAKE;
    blue.resetHistory(); blue.end();
    check(10000-blue.battle.player.curHp == 7 && blue.battle.player.hasStatus<PS::WEAK>(),
          "Blue Slaver Rake effect");
    sample(blue);
    const Composition group {MonsterId::MAD_GREMLIN, MonsterId::FAT_GREMLIN,
        MonsterId::SNEAKY_GREMLIN, MonsterId::GREMLIN_WIZARD};
    Fixture f(MonsterEncounter::GREMLIN_GANG,seedFor(MonsterEncounter::GREMLIN_GANG,group));
    f.battle.monsters.arr[0].attacked(f.battle,1); f.observe();
    check(f.battle.monsters.arr[0].strength == 1, "Mad Gremlin Angry reaction");
    f.end();
    check(10000-f.battle.player.curHp == 5+4+9, "Gremlin attack effects");
    check(f.battle.player.hasStatus<PS::WEAK>(), "Fat Gremlin Weak");
    sample(f);
}
void refusals() {
    Fixture f(MonsterEncounter::GREMLIN_GANG);
    rejects([&]{ auto b=f.battle; resampleCombatContinuation(f.game,b,seeds); }, "missing history admitted");
    auto high = f.battle; high.ascension=11;
    rejects([&]{ validateEncounterRecipe(high); }, "unreviewed A11 Gang admitted");
    auto invalid = f.battle;
    for (int i=0;i<4;++i) invalid.monsters.arr[i].id=MonsterId::GREMLIN_WIZARD;
    rejects([&]{ validateEncounterRecipe(invalid); }, "overdrawn Gremlin pool admitted");
    f.end(); PublicEnemyHistory late; late.observe(publicEnemyFrame(f.battle));
    rejects([&]{ auto b=f.battle; resampleCombatContinuation(f.game,b,seeds,-1,&late); }, "late history admitted");
    auto stale=f.history; f.end();
    rejects([&]{ auto b=f.battle; resampleCombatContinuation(f.game,b,seeds,-1,&stale); }, "stale history admitted");
    const Composition group {MonsterId::MAD_GREMLIN, MonsterId::FAT_GREMLIN,
        MonsterId::SHIELD_GREMLIN, MonsterId::GREMLIN_WIZARD};
    Fixture wrong(MonsterEncounter::GREMLIN_GANG,seedFor(MonsterEncounter::GREMLIN_GANG,group));
    wrong.battle.monsters.arr[3].moveHistory[0]=MMID::GREMLIN_WIZARD_ULTIMATE_BLAST;
    wrong.observe();
    rejects([&]{ auto b=wrong.battle; resampleCombatContinuation(wrong.game,b,seeds,-1,&wrong.history); },
            "Wizard intent contradicting observed cycle admitted");
}
}
int main() {
    try { allCompositions(); redSlaverLifecycle(); wizardAndShield(); looterLifecycle(); basicEffects(); refusals();
        std::cout << "act1-humanoid-history-v1: " << compositions << " compositions, " << roots
                  << " lifecycle roots; counters/gold/targets/refusals passed\n"; return 0;
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
