#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
using namespace sts::public_sampling;
namespace {
int roots = 0, checks = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
template<class F> void rejects(F f, const char *why) {
    try { f(); } catch (const std::runtime_error &) { return; }
    throw std::runtime_error(why);
}
std::string dump(const BattleContext &b) { std::ostringstream s; s << b; return s.str(); }
const std::array<std::uint64_t, 7> seeds {11,22,33,44,55,66,77};
const std::array<MonsterEncounter,6> batch {ME::SPHERIC_GUARDIAN, ME::SHELL_PARASITE,
    ME::SNAKE_PLANT, ME::SENTRY_AND_SPHERE, ME::SHELLED_PARASITE_AND_FUNGI, ME::THREE_CULTIST};
struct Fixture {
    GameContext g;
    BattleContext b;
    PublicEnemyHistory history;
    Fixture(MonsterEncounter e, int seed = 7) : g(CharacterClass::IRONCLAD,seed,0) {
        g.act = 2; g.floorNum = 20; g.enterBattle(e);
        b.init(g); b.executeActions();
        // Synthetic durability for lifecycle coverage, not survival measurement.
        b.player.curHp = b.player.maxHp = 10000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(b)); }
    void resetHistory() { history = PublicEnemyHistory(); observe(); }
    void resolve() { b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions(); observe(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); observe(); }
    void attack(int idx, int damage) { b.monsters.arr[idx].attacked(b,damage); resolve(); }
    void damage(int idx, int damage) { b.monsters.arr[idx].damage(b,damage); resolve(); }
};
void sample(Fixture &f) {
    const auto source = dump(f.b);
    const auto visible = publicEnemyFrame(f.b);
    auto a = f.b, b = f.b;
    b.aiRng = Random(901); b.cardRandomRng = Random(902); b.miscRng = Random(903);
    b.monsterHpRng = Random(904); b.potionRng = Random(905); b.shuffleRng = Random(906);
    std::reverse(b.cards.drawPile.begin(),b.cards.drawPile.end());
    for (int i=0;i<b.monsters.monsterCount;++i) {
        b.monsters.arr[i].miscInfo = 999;
        b.monsters.arr[i].moveHistory[1] = MMID::INVALID;
        b.monsters.arr[i].escapeNext = true;
    }
    check(publicEnemyFrame(b) == visible, "test changed public frame");
    resampleCombatContinuation(f.g,a,seeds,-1,&f.history);
    resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
    check(publicEnemyFrame(a) == visible, "sampling changed public frame");
    check(dump(a) == dump(b), "sample depends on hidden source state");
    check(dump(f.b) == source, "sampling mutated source");
    for (int i=0;i<a.monsters.monsterCount;++i) {
        const auto &m=a.monsters.arr[i], &original=f.b.monsters.arr[i];
        if (!m.isDeadOrEscaped()) {
            const bool stunned = m.id==MonsterId::SHELLED_PARASITE && m.moveHistory[0]==MMID::SHELLED_PARASITE_STUNNED;
            check(m.moveHistory[1] == (stunned ? MMID::INVALID : original.moveHistory[1]),
                  "observed previous intent mismatch");
            check(m.block == original.block && m.statusBits == original.statusBits &&
                  m.uniquePower0 == original.uniquePower0 && m.platedArmor == original.platedArmor,
                  "sampling changed public reactive power");
        }
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    check(dump(a) == dump(b), "continuation twins differ");
    if (a.outcome == Outcome::UNDECIDED) {
        auto h=f.history; h.observe(publicEnemyFrame(a));
        resampleCombatContinuation(f.g,a,seeds,-1,&h);
    }
    ++roots;
}
void lifecycles() {
    for (auto e : batch) for (int seed=0;seed<16;++seed) {
        Fixture f(e,seed);
        for (int t=0;t<8;++t) { sample(f); f.end(); }
        if (f.b.monsters.monsterCount > 1) {
            // One surviving group per victim slot, from identical observed roots.
            for (int victim=0;victim<f.b.monsters.monsterCount;++victim) {
                auto group=f;
                const int count=group.b.monsters.monsterCount;
                group.damage(victim,99999);
                check(group.b.monsters.monsterCount == count, "dead slots compacted");
                check(group.b.monsters.arr[victim].isDeadOrEscaped(), "victim survived");
                for (int t=0;t<3;++t) { sample(group); group.end(); }
            }
        }
    }
}
void sphere() {
    Fixture f(ME::SPHERIC_GUARDIAN); auto &m=f.b.monsters.arr[0];
    check(m.curHp==20 && m.block==40 && m.getStatus<MS::ARTIFACT>()==3 &&
          m.hasStatus<MS::BARRICADE>(), "Sphere opening powers");
    const MMID moves[] {MMID::SPHERIC_GUARDIAN_ACTIVATE,MMID::SPHERIC_GUARDIAN_ATTACK_DEBUFF,
        MMID::SPHERIC_GUARDIAN_SLAM,MMID::SPHERIC_GUARDIAN_HARDEN,MMID::SPHERIC_GUARDIAN_SLAM,
        MMID::SPHERIC_GUARDIAN_HARDEN};
    const int damage[] {0,10,20,10,20,10}, blocks[] {65,65,65,80,80,95};
    for (int turn=0;turn<6;++turn) {
        check(m.moveHistory[0]==moves[turn], "Sphere fixed cycle");
        const int hp=f.b.player.curHp; sample(f); f.end();
        check(hp-f.b.player.curHp==damage[turn], "Sphere damage");
        check(m.block==blocks[turn], "Sphere block retention");
        if(turn==1) check(f.b.player.getStatus<PS::FRAIL>()==5, "Sphere Frail");
    }
    for(int n=0;n<3;++n) {
        Actions::DebuffEnemy<MS::WEAK>(0,2,false).actFunc(f.b); f.observe();
        check(!m.hasStatus<MS::WEAK>() && m.getStatus<MS::ARTIFACT>()==2-n,
              "Sphere Artifact should block each debuff");
        sample(f);
    }
    Actions::DebuffEnemy<MS::WEAK>(0,2,false).actFunc(f.b); f.observe();
    check(m.getStatus<MS::WEAK>()==2, "Sphere post-Artifact debuff");
    sample(f);
}
void snake() {
    Fixture f(ME::SNAKE_PLANT); auto &m=f.b.monsters.arr[0];
    check(m.getStatus<MS::MALLEABLE>()==3, "Snake opening Malleable");
    f.attack(0,1);
    check(m.block==3 && m.getStatus<MS::MALLEABLE>()==4, "first unblocked hit Malleable");
    f.attack(0,3);
    check(m.block==0 && m.getStatus<MS::MALLEABLE>()==4, "fully blocked hit triggered Malleable");
    f.attack(0,1);
    check(m.block==4 && m.getStatus<MS::MALLEABLE>()==5, "second unblocked hit Malleable");
    f.damage(0,5);
    check(m.block==0 && m.getStatus<MS::MALLEABLE>()==5, "non-attack triggered Malleable");
    sample(f); f.end();
    check(m.getStatus<MS::MALLEABLE>()==3, "Malleable did not reset"); sample(f);
    for (auto move : {MMID::SNAKE_PLANT_CHOMP,MMID::SNAKE_PLANT_ENFEEBLING_SPORES}) {
        Fixture effect(ME::SNAKE_PLANT);
        effect.b.monsters.arr[0].moveHistory[0]=move; effect.resetHistory();
        effect.end();
        check(10000-effect.b.player.curHp == (move==MMID::SNAKE_PLANT_CHOMP ? 21 : 0),
              "Snake move damage");
        if(move==MMID::SNAKE_PLANT_ENFEEBLING_SPORES)
            check(effect.b.player.getStatus<PS::WEAK>()==2 && effect.b.player.getStatus<PS::FRAIL>()==2,
                  "Snake spores debuffs");
        sample(effect);
    }
    for(int current=0;current<2;++current) for(int previous=0;previous<2;++previous)
        for(int roll=0;roll<100;++roll) {
            auto b=f.b; auto &n=b.monsters.arr[0];
            const MMID moves[] {MMID::SNAKE_PLANT_CHOMP,MMID::SNAKE_PLANT_ENFEEBLING_SPORES};
            n.moveHistory[0]=moves[current]; n.moveHistory[1]=moves[previous];
            int data=0;
            const auto expected=roll<65 ? (current==0 && previous==0 ? moves[1] : moves[0]) :
                                        (current==1 ? moves[0] : moves[1]);
            check(n.getMoveForRoll(b,data,roll)==expected,"Snake A0 history restriction");
        }
}
void parasite() {
    Fixture f(ME::SHELL_PARASITE); auto &m=f.b.monsters.arr[0];
    check(m.block==14 && m.getStatus<MS::PLATED_ARMOR>()==14, "Parasite opening armor");
    f.attack(0,14);
    check(m.getStatus<MS::PLATED_ARMOR>()==14, "blocked attack removed armor");
    f.damage(0,1);
    check(m.getStatus<MS::PLATED_ARMOR>()==14, "non-attack removed armor");
    for(int remaining=13;remaining>=0;--remaining) {
        f.attack(0,1);
        check(m.getStatus<MS::PLATED_ARMOR>()==remaining, "unblocked attack armor loss");
        sample(f);
    }
    check(m.moveHistory[0]==MMID::SHELLED_PARASITE_STUNNED, "armor break did not stun");
    const int hp=f.b.player.curHp; f.end();
    check(f.b.player.curHp==hp && m.moveHistory[1]==MMID::SHELLED_PARASITE_FELL,
          "post-stun transient history");
    sample(f); f.end(); sample(f);
    for(int block : {0,4,10,20}) {
        Fixture heal(ME::SHELL_PARASITE); auto &p=heal.b.monsters.arr[0];
        p.curHp=20; p.block=0; heal.b.player.block=block;
        p.moveHistory[0]=MMID::SHELLED_PARASITE_SUCK;
        p.takeTurn(heal.b); heal.resolve();
        const int lost=std::max(0,10-block);
        check(heal.b.player.curHp==10000-lost && p.curHp==20+lost, "Suck actual HP loss/healing");
    }
    Fixture barrier(ME::SHELL_PARASITE); auto &p=barrier.b.monsters.arr[0];
    p.curHp=1; p.block=0; p.moveHistory[0]=MMID::SHELLED_PARASITE_SUCK;
    barrier.b.player.buff<PS::THORNS>(3);
    p.takeTurn(barrier.b); barrier.resolve();
    check(p.curHp==8, "Suck healing must resolve before queued Thorns");
    Fixture debuff(ME::SHELL_PARASITE);
    debuff.b.monsters.arr[0].moveHistory[0]=MMID::SHELLED_PARASITE_FELL;
    debuff.end();
    check(debuff.b.player.curHp==9982 && debuff.b.player.getStatus<PS::FRAIL>()==2, "Fell effect");
}
void composedEffects() {
    Fixture autoplay(ME::SHELL_PARASITE);
    autoplay.b.cards=CardManager{}; autoplay.b.cards.nextUniqueCardId=100;
    for(int i=0;i<6;++i) {
        CardInstance c(CardId::STRIKE_RED); c.setUniqueId(autoplay.b.cards.nextUniqueCardId++);
        autoplay.b.cards.drawPile.push_back(c);
    }
    auto &interrupted=autoplay.b.monsters.arr[0];
    interrupted.curHp=interrupted.maxHp=1000; interrupted.block=0;
    interrupted.setStatus<MS::PLATED_ARMOR>(1);
    autoplay.b.player.buff<PS::MAYHEM>(1); autoplay.resetHistory();
    autoplay.end();
    check(interrupted.moveHistory[0]==MMID::SHELLED_PARASITE_STUNNED &&
          !interrupted.hasStatus<MS::PLATED_ARMOR>(), "Mayhem start-turn armor break");
    // No observation between enemy roll and the automatic player attack. The
    // interrupted private intent is irrelevant and canonicalized, not copied.
    sample(autoplay);
    Fixture sentry(ME::SENTRY_AND_SPHERE);
    check(sentry.b.monsters.arr[0].moveHistory[0]==MMID::SENTRY_BOLT &&
          sentry.b.monsters.arr[1].moveHistory[0]==MMID::SPHERIC_GUARDIAN_ACTIVATE, "Sentry/Sphere opening");
    sentry.end();
    check(sentry.b.monsters.arr[1].block==65 && sentry.b.player.curHp==10000,
          "Sentry/Sphere opening effects");
    int dazed=0;
    for (const auto &c : sentry.b.cards.discardPile) dazed+=c.id==CardId::DAZED;
    check(dazed==2, "Sentry Dazed insertion"); sample(sentry);
    sentry.end();
    check(sentry.b.player.curHp==9981, "Sentry/Sphere combined attacks"); sample(sentry);
    Fixture fungi(ME::SHELLED_PARASITE_AND_FUNGI);
    fungi.damage(1,9999);
    check(fungi.b.player.getStatus<PS::VULNERABLE>()==2, "Fungi death debuff in parasite group");
    sample(fungi);
    auto &p=fungi.b.monsters.arr[0]; p.curHp=20; p.block=0;
    p.moveHistory[0]=MMID::SHELLED_PARASITE_SUCK;
    p.takeTurn(fungi.b); fungi.resolve();
    check(fungi.b.player.curHp==9985 && p.curHp==35, "Vulnerable amplifies Suck and healing");
    Fixture cultists(ME::THREE_CULTIST); cultists.end();
    for (int i=0;i<3;++i) check(cultists.b.monsters.arr[i].getStatus<MS::RITUAL>()==3 &&
                              cultists.b.monsters.arr[i].strength==0,"Cultist ritual opening");
    cultists.end();
    check(cultists.b.player.curHp==9982, "three Cultist first attacks");
    for(int i=0;i<3;++i) check(cultists.b.monsters.arr[i].strength==3,"independent Cultist growth");
    sample(cultists);
}
void refusals() {
    for (auto e : batch) {
        Fixture f(e);
        rejects([&]{ auto b=f.b; resampleCombatContinuation(f.g,b,seeds); },"missing enemy history admitted");
        auto high=f.b; high.ascension=11;
        rejects([&]{validateEncounterRecipe(high);},"unreviewed A11 admitted");
        auto stale=f.history; f.end();
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&stale);},"stale history admitted");
        PublicEnemyHistory late; late.observe(publicEnemyFrame(f.b));
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&late);},"late history admitted");
    }
    Fixture wrong(ME::SENTRY_AND_SPHERE);
    std::swap(wrong.b.monsters.arr[0],wrong.b.monsters.arr[1]);
    rejects([&]{validateEncounterRecipe(wrong.b);},"reversed target slots admitted");
    Fixture cycle(ME::SPHERIC_GUARDIAN);
    cycle.b.monsters.arr[0].moveHistory[0]=MMID::SPHERIC_GUARDIAN_SLAM; cycle.resetHistory();
    rejects([&]{auto b=cycle.b;resampleCombatContinuation(cycle.g,b,seeds,-1,&cycle.history);},
            "Sphere contradictory phase admitted");
    Fixture future(ME::GREMLIN_LEADER);
    future.b.ascension=11; // A0-A10 is admitted by its reviewed lifecycle recipe.
    rejects([&]{validateEncounterRecipe(future.b);},"unreviewed summon ascension admitted");
}
}
int main() {
    try { lifecycles(); sphere(); snake(); parasite(); composedEffects(); refusals();
        std::cout << "act2-reactive-history-v1: 6 compositions, " << roots
                  << " lifecycle roots, " << checks << " checks passed\n"; return 0;
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
