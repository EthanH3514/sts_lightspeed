#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
using namespace sts::public_sampling;
namespace {
int checks=0, roots=0, replacements=0, hiddenDamage=0;
std::string context;
void check(bool ok,const char *why) { ++checks; if(!ok) throw std::runtime_error(context+": "+why); }
template<class F> void rejects(F f,const char *why) {
    ++checks; try { f(); } catch(const std::runtime_error &) { return; }
    throw std::runtime_error(context+": "+why);
}
std::string dump(const BattleContext &b) { std::ostringstream s; s<<b; return s.str(); }
const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
const std::array<ME,26> batch{ME::GREMLIN_LEADER,ME::AUTOMATON,ME::COLLECTOR,ME::CHAMP,ME::THREE_DARKLINGS,ME::ORB_WALKER,ME::THREE_SHAPES,ME::SPIRE_GROWTH,ME::TRANSIENT,ME::FOUR_SHAPES,ME::MAW,ME::SPHERE_AND_TWO_SHAPES,ME::JAW_WORM_HORDE,ME::WRITHING_MASS,ME::GIANT_HEAD,ME::NEMESIS,ME::REPTOMANCER,ME::AWAKENED_ONE,ME::TIME_EATER,ME::DONU_AND_DECA,ME::SHIELD_AND_SPEAR,ME::THE_HEART,ME::COLOSSEUM_EVENT_SLAVERS,ME::COLOSSEUM_EVENT_NOBS,ME::MASKED_BANDITS_EVENT,ME::MYSTERIOUS_SPHERE_EVENT};
struct Fixture {
    GameContext g; BattleContext b; PublicEnemyHistory history;
    Fixture(ME e,int seed=7):g(CharacterClass::IRONCLAD,seed,0) {
        g.act=3; g.floorNum=40; g.enterBattle(e); b.init(g); b.executeActions();
        g.regainControlAction=[](GameContext &) {};
        b.player.curHp=b.player.maxHp=10000;
        for(int i=0;i<b.monsters.monsterCount;++i) if(b.monsters.arr[i].id!=MonsterId::INVALID)
            b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(b)); }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); observe(); }
    void kill(int idx) { b.monsters.arr[idx].damage(b,99999); drain(); observe(); }
    void play(CardId id,int target=0) {
        b.cards.createTempCardInHand(CardInstance(id)); b.player.energy=100;
        search::Action(search::ActionType::CARD,b.cards.cardsInHand-1,target).execute(b); observe();
    }
};
bool visibleMisc(const Monster &m) {
    return (m.id==MonsterId::DARKLING && m.moveHistory[0]==MMID::DARKLING_NIP);
}
void sample(Fixture &f) {
    if(f.b.outcome!=Outcome::UNDECIDED) return;
    context=std::string(monsterEncounterStrings[static_cast<int>(f.b.encounter)])+" turn="+std::to_string(f.b.turn);
    const auto source=dump(f.b);
    const auto frame=publicEnemyFrame(f.b);
    auto a=f.b,b=f.b;
    b.aiRng=Random(901); b.cardRandomRng=Random(902); b.miscRng=Random(903);
    b.monsterHpRng=Random(904); b.potionRng=Random(905); b.shuffleRng=Random(906);
    std::reverse(b.cards.drawPile.begin(),b.cards.drawPile.end());
    for(int i=0;i<b.monsters.monsterCount;++i) {
        auto &m=b.monsters.arr[i];
        if(!visibleMisc(m)) { m.miscInfo=999; if(m.id==MonsterId::DARKLING) ++hiddenDamage; }
        m.moveHistory[1]=MMID::INVALID; m.escapeNext=true;
    }
    check(publicEnemyFrame(b)==frame,"poisoning changed public frame");
    try {
        resampleCombatContinuation(f.g,a,seeds,-1,&f.history);
        resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
    } catch(const std::exception &e) { throw std::runtime_error(context+": "+e.what()); }
    check(publicEnemyFrame(a)==frame && dump(a)==dump(b),"same-public samples depend on hidden state");
    check(dump(f.b)==source && publicEnemyFrame(f.b)==frame,"sampling mutated source");
    check(a.player.lastTargetedMonster==f.b.player.lastTargetedMonster,"public facing changed");
    for(int i=0;i<a.monsters.monsterCount;++i) {
        const auto &m=a.monsters.arr[i], &o=f.b.monsters.arr[i];
        check(m.curHp==o.curHp && m.block==o.block && m.statusBits==o.statusBits &&
              m.halfDead==o.halfDead && m.publicGeneration==o.publicGeneration,"public monster changed");
        if(!m.isDeadOrEscaped() || m.halfDead) {
            // Damage prior is deliberately independent until Nip is observed.
            if(m.id!=MonsterId::DARKLING) check(m.miscInfo==o.miscInfo,"reconstructed phase counter mismatch");
        }
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    check(dump(a)==dump(b),"same-public continuations diverge");
    if(a.outcome==Outcome::UNDECIDED) {
        auto child=f.history; child.observe(publicEnemyFrame(a));
        try { resampleCombatContinuation(f.g,a,seeds,-1,&child); }
        catch(const std::exception &e) { throw std::runtime_error(context+" child: "+e.what()); }
    }
    ++roots;
}
void lifecycles() {
    for(auto e:batch) for(int seed=0;seed<24;++seed) {
        Fixture f(e,seed);
        for(int turn=0;turn<16 && f.b.outcome==Outcome::UNDECIDED;++turn) {
            f.observe(); f.observe(); sample(f); f.end();
        }
    }
}
void summons() {
    for(auto e:{ME::GREMLIN_LEADER,ME::AUTOMATON,ME::COLLECTOR,ME::REPTOMANCER})
        for(int seed=0;seed<16;++seed) {
            Fixture f(e,seed);
            const int leader=e==ME::GREMLIN_LEADER?3:e==ME::AUTOMATON?1:2;
            for(int turn=0;turn<18;++turn) {
                if(turn%3==2) for(int i=0;i<f.b.monsters.monsterCount;++i)
                    if(i!=leader && f.b.monsters.arr[i].isAlive()) f.kill(i);
                sample(f);
                const auto before=publicEnemyFrame(f.b); f.end();
                const auto after=publicEnemyFrame(f.b);
                for(int i=0;i<after.count;++i) if(after.slots[i].generation>before.slots[i].generation)
                    ++replacements;
            }
            f.kill(leader);
            check(f.b.outcome==Outcome::PLAYER_VICTORY,"leader death must finish combat");
        }
    check(replacements>100,"did not exercise repeated public slot replacements");
}
void phases() {
    for(int seed=0;seed<24;++seed) {
        Fixture dark(ME::THREE_DARKLINGS,seed);
        for(int round=0;round<3;++round) {
            dark.kill(round%3);
            check(dark.b.monsters.arr[round%3].halfDead,"Darkling did not enter regrow");
            for(int i=0;i<3;++i) {sample(dark);dark.end();}
            check(!dark.b.monsters.arr[round%3].halfDead,"Darkling failed to revive");
        }
        for(int i=0;i<3;++i) dark.kill(i);
        check(dark.b.outcome==Outcome::PLAYER_VICTORY,"all Darklings down should finish");

        Fixture awake(ME::AWAKENED_ONE,seed); awake.kill(2);
        check(awake.b.monsters.arr[2].halfDead,"Awakened first death did not enter rebirth");
        sample(awake); awake.end(); sample(awake);
        check(awake.b.monsters.arr[2].miscInfo==1 && !awake.b.monsters.arr[2].halfDead,"Awakened second phase");
        for(int i=0;i<8;++i) { sample(awake); awake.end(); }
        awake.kill(2); check(awake.b.outcome==Outcome::PLAYER_VICTORY,"Awakened second death should finish");

        for(auto e:{ME::CHAMP,ME::TIME_EATER}) {
            Fixture f(e,seed); f.end();
            f.b.monsters.arr[0].curHp=300; f.observe();
            for(int i=0;i<12;++i) { sample(f); f.end(); }
            check(f.b.monsters.arr[0].miscInfo!=0,"low HP did not exercise boss phase");
        }
        Fixture writhing(ME::WRITHING_MASS,seed);
        for(int turn=0;turn<8;++turn) {
            writhing.play(CardId::STRIKE_RED); sample(writhing);
            writhing.play(CardId::PUMMEL); sample(writhing);
            writhing.end();
        }
    }
    check(hiddenDamage>0,"Darkling hidden birth-damage prior not exercised");
}
void publicInteractions() {
    Fixture timer(ME::TIME_EATER); timer.b.cards=CardManager{}; timer.b.cards.nextUniqueCardId=100;
    for(int i=0;i<24;++i) {timer.play(CardId::ANGER);sample(timer);}
    check(timer.b.turn==2,"forced turn boundaries did not preserve observation continuity");
    for(int facing:{0,1}) {
        Fixture f(ME::SHIELD_AND_SPEAR);
        f.play(CardId::STRIKE_RED,facing);sample(f);
        f.kill(facing);sample(f);f.end();sample(f);
    }
    for(int seed=0;seed<24;++seed) {
        Fixture f(ME::AUTOMATON,seed);
        bool held=false;
        for(int turn=0;turn<6;++turn) {
            f.end();sample(f);
            for(int i:{0,2})if(f.b.monsters.arr[i].hasStatus<MS::STASIS>()) {
                const auto &c=f.b.cards.stasisCards[std::min(1,i)];
                check(c.id!=CardId::INVALID,"visible Stasis card missing");
                auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
                check(b.cards.stasisCards[std::min(1,i)].uniqueId==c.uniqueId,"sample lost Stasis identity");
                held=true;
            }
        }
        check(held,"did not exercise a publicly held card");
        f.kill(0);sample(f);f.kill(2);sample(f);
    }
    Fixture spawner(ME::COLLECTOR);spawner.end();
    auto malformed=spawner.b;malformed.monsters.arr[0].publicGeneration=-1;
    auto history=spawner.history;history.observe(publicEnemyFrame(malformed));
    rejects([&]{resampleCombatContinuation(spawner.g,malformed,seeds,-1,&history);},"decreasing spawn identity accepted");
}
void guards() {
    for(auto e:batch) {
        Fixture f(e);
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds);},"missing history accepted");
        auto stale=f.history; f.end();
        if(f.b.outcome!=Outcome::UNDECIDED) continue;
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&stale);},"stale history accepted");
        PublicEnemyHistory late; late.observe(publicEnemyFrame(f.b));
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&late);},"late history accepted");
        for(int asc:{11,18,20}) {auto b=f.b;b.ascension=asc;
            rejects([&]{validateEncounterRecipe(b);},"unreviewed ascension accepted");}
        auto b=f.b;b.addToBot(Actions::DrawCards(1));
        rejects([&]{resampleCombatContinuation(f.g,b,seeds,-1,&f.history);},"unresolved root accepted");
        b=f.b;b.monsters.arr[0].id=MonsterId::INVALID;b.monsters.arr[0].curHp=1;
        rejects([&]{validateEncounterRecipe(b);},"living invalid slot accepted");
    }
    Fixture facing(ME::SHIELD_AND_SPEAR); facing.b.player.lastTargetedMonster=4;
    rejects([&]{auto b=facing.b;resampleCombatContinuation(facing.g,b,seeds,-1,&facing.history);},"invalid facing accepted");
}
}
int main() {
    try {lifecycles();summons();phases();publicInteractions();guards();
        std::cout<<"remaining-encounters-history-v1: 26 identities, "<<roots<<" roots, "
            <<replacements<<" replacements, "<<hiddenDamage<<" hidden damage roots, "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
