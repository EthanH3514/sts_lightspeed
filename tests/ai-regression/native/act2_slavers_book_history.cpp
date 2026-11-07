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
int roots=0, checks=0, hiddenBookRoots=0;
void check(bool ok,const char *why) { ++checks; if(!ok) throw std::runtime_error(why); }
template<class F> void rejects(F f,const char *why) {
    ++checks; try { f(); } catch(const std::runtime_error &) { return; }
    throw std::runtime_error(why);
}
std::string dump(const BattleContext &b) { std::ostringstream s; s<<b; return s.str(); }
std::string rng(const Random &r) { std::ostringstream s; s<<r.counter<<':'<<r.seed0<<':'<<r.seed1; return s.str(); }
const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
const std::array<ME,2> batch{ME::SLAVERS,ME::BOOK_OF_STABBING};
struct Fixture {
    GameContext g; BattleContext b; PublicEnemyHistory history;
    Fixture(ME encounter,int seed=7):g(CharacterClass::IRONCLAD,seed,0) {
        g.act=2; g.floorNum=24; g.enterBattle(encounter); b.init(g); b.executeActions();
        g.regainControlAction=[](GameContext &) {};
        // Synthetic durability for bounded lifecycle paths, not policy quality.
        b.player.curHp=b.player.maxHp=10000;
        for(int i=0;i<b.monsters.monsterCount;++i) b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(b)); }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); observe(); }
    void kill(int slot) { b.monsters.arr[slot].damage(b,99999); drain(); observe(); }
};
int wounds(const BattleContext &b) {
    int n=0;
    for(int i=0;i<b.cards.cardsInHand;++i) n+=b.cards.hand[i].id==CardId::WOUND;
    for(const auto *pile:{&b.cards.drawPile,&b.cards.discardPile,&b.cards.exhaustPile})
        for(const auto &c:*pile) n+=c.id==CardId::WOUND;
    return n;
}
void sample(Fixture &f) {
    const auto source=dump(f.b);
    const auto frame=publicEnemyFrame(f.b);
    auto a=f.b,b=f.b;
    b.aiRng=Random(901); b.cardRandomRng=Random(902); b.miscRng=Random(903);
    b.monsterHpRng=Random(904); b.potionRng=Random(905); b.shuffleRng=Random(906);
    std::reverse(b.cards.drawPile.begin(),b.cards.drawPile.end());
    for(int i=0;i<b.monsters.monsterCount;++i) {
        auto &m=b.monsters.arr[i];
        // Multi Stab's count is CURRENT PUBLIC intent, not a poisonable hidden
        // variable. During Single Stab it is hidden and must come from history.
        if(m.id!=MonsterId::BOOK_OF_STABBING || m.moveHistory[0]!=MMID::BOOK_OF_STABBING_MULTI_STAB) {
            m.miscInfo=999;
            if(m.id==MonsterId::BOOK_OF_STABBING) ++hiddenBookRoots;
        }
        m.moveHistory[1]=MMID::INVALID; m.escapeNext=true;
    }
    check(publicEnemyFrame(b)==frame,"poisoning changed public frame");
    resampleCombatContinuation(f.g,a,seeds,-1,&f.history);
    resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
    check(publicEnemyFrame(a)==frame && dump(a)==dump(b),"same-public samples depend on hidden state");
    check(dump(f.b)==source,"sampling mutated source");
    check(wounds(a)==wounds(f.b),"sampling changed generated status cards");
    for(int i=0;i<a.monsters.monsterCount;++i) {
        const auto &m=a.monsters.arr[i], &original=f.b.monsters.arr[i];
        check(m.statusBits==original.statusBits && m.curHp==original.curHp && m.block==original.block,
              "sampling changed public enemy state");
        if(!m.isDeadOrEscaped()) {
            check(m.moveHistory[1]==original.moveHistory[1],"observed prior intent mismatch");
            check(m.miscInfo==original.miscInfo,"observed phase counter mismatch");
        }
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    check(dump(a)==dump(b),"same-public continuations diverge");
    if(a.outcome==Outcome::UNDECIDED) {
        auto child=f.history; child.observe(publicEnemyFrame(a));
        resampleCombatContinuation(f.g,a,seeds,-1,&child);
    }
    ++roots;
}
void lifecycles() {
    std::set<MMID> observed;
    for(auto e:batch) for(int seed=0;seed<32;++seed) {
        Fixture f(e,seed); int bookCount=1;
        for(int turn=0;turn<12;++turn) {
            if(e==ME::BOOK_OF_STABBING) {
                auto &m=f.b.monsters.arr[0]; observed.insert(m.moveHistory[0]);
                if(m.moveHistory[0]==MMID::BOOK_OF_STABBING_MULTI_STAB) ++bookCount;
                check(m.miscInfo==bookCount,"independent Book public announcement counter");
            }
            // Repeated observations within a player turn must not advance counters.
            f.observe(); f.observe(); sample(f); f.end();
        }
    }
    check(observed.size()==2 && hiddenBookRoots>0,"Book test never exercised hidden single-stab count");
    // Every nonempty survivor mask, preserving the original three target slots.
    for(int mask=1;mask<8;++mask) for(int seed=0;seed<16;++seed) {
        Fixture f(ME::SLAVERS,seed);
        for(int i=0;i<3;++i) if(!(mask&(1<<i))) f.kill(i);
        check(f.b.monsters.monsterCount==3,"dead target slots compacted");
        for(int turn=0;turn<8;++turn) { sample(f); f.end(); }
    }
}
void bookRules() {
    const MMID multi=MMID::BOOK_OF_STABBING_MULTI_STAB, single=MMID::BOOK_OF_STABBING_SINGLE_STAB;
    Fixture f(ME::BOOK_OF_STABBING); auto &m=f.b.monsters.arr[0];
    for(auto current:{MMID::INVALID,multi,single}) for(auto previous:{MMID::INVALID,multi,single})
        for(int count:{1,2,5,30}) for(int roll=0;roll<100;++roll) {
            m.moveHistory[0]=current; m.moveHistory[1]=previous;
            const bool useMulti=roll<15?current==single:!(current==multi && previous==multi);
            int data=count; const auto state=rng(f.b.aiRng);
            check(m.getMoveForRoll(f.b,data,roll)==(useMulti?multi:single),"Book A0 independent move table");
            check(data==count+useMulti && rng(f.b.aiRng)==state,"Book count update/RNG use");
        }
    // Resolve actual queued attacks, counting wounds per damaging hit, not turn.
    for(auto intent:{multi,single}) for(int block:{0,1,6,7,11,12,20,21,100})
        for(bool buffer:{false,true}) for(bool rod:{false,true}) {
            Fixture hit(ME::BOOK_OF_STABBING); auto &book=hit.b.monsters.arr[0];
            book.moveHistory[0]=intent; book.miscInfo=2;
            hit.b.player.block=block;
            if(buffer) hit.b.player.buff<PS::BUFFER>(1);
            hit.b.player.setHasRelic<RelicId::TUNGSTEN_ROD>(rod);
            int remaining=block, shield=buffer, loss=0, expectedWounds=0;
            for(int i=0;i<(intent==multi?2:1);++i) {
                int damage=intent==multi?6:21;
                const int blocked=std::min(damage,remaining); remaining-=blocked; damage-=blocked;
                if(damage>0 && shield) { --shield; damage=0; }
                if(damage>0 && rod) --damage;
                loss+=damage; expectedWounds+=damage>0;
            }
            book.takeTurn(hit.b); hit.drain();
            check(10000-hit.b.player.curHp==loss,"Book actual multi-hit/defense damage");
            check(wounds(hit.b)==expectedWounds,"Painful Stabs requires HP loss per hit");
            check(hit.b.cards.discardPile.size()==static_cast<std::size_t>(expectedWounds),
                  "Painful Stabs insertion must be discard, not hand/draw");
        }
}
void slaverEffects() {
    for(int block:{0,7,100}) for(bool artifact:{false,true}) {
        Fixture f(ME::SLAVERS); auto &taskmaster=f.b.monsters.arr[1];
        f.b.player.block=block; if(artifact) f.b.player.artifact=1;
        taskmaster.takeTurn(f.b); f.drain();
        check(10000-f.b.player.curHp==std::max(0,7-block),"Taskmaster A0 attack damage");
        check(wounds(f.b)==1 && f.b.cards.discardPile.size()==1,"Taskmaster unconditional wound insertion");
        check(taskmaster.strength==0 && taskmaster.moveHistory[1]==MMID::INVALID,
              "Taskmaster A0 strength/fixed history");
        check(f.b.player.artifact==artifact,"Wound insertion consumed Artifact");
    }
    for(bool artifact:{false,true}) {
        Fixture f(ME::SLAVERS);
        f.b.monsters.arr[0].moveHistory[0]=MMID::BLUE_SLAVER_RAKE;
        f.b.monsters.arr[2].moveHistory[0]=MMID::RED_SLAVER_ENTANGLE;
        f.history=PublicEnemyHistory(); f.observe();
        if(artifact) f.b.player.artifact=2;
        f.end();
        check(f.b.player.hasStatus<PS::WEAK>()==!artifact &&
              f.b.player.hasStatus<PS::ENTANGLED>()==!artifact,"combined debuffs/Artifact order");
        check(f.b.player.artifact==0 && wounds(f.b)==1,"trio status-card/debuff effects");
        f.b.player.energy=100;
        check(CardInstance(CardId::STRIKE_RED).canUse(f.b,0,false)==artifact,"Entangle attack legality");
        check(CardInstance(CardId::DEFEND_RED).canUse(f.b,0,false),"Entangle wrongly prevents skills");
        sample(f); f.end();
        check(!f.b.player.hasStatus<PS::ENTANGLED>(),"Entangle failed to expire");
        for(int turn=0;turn<8;++turn) {
            check(f.b.monsters.arr[2].moveHistory[0]!=MMID::RED_SLAVER_ENTANGLE,
                  "blocked/expired Entangle was reused");
            sample(f); f.end();
        }
    }
    // Generated wounds are ordinary status cards on a later draw: shared
    // Evolve/Fire Breathing callbacks, not special-case elite damage logic.
    Fixture draw(ME::SLAVERS); draw.b.cards=CardManager{}; draw.b.cards.nextUniqueCardId=100;
    draw.b.player.buff<PS::EVOLVE>(1); draw.b.player.buff<PS::FIRE_BREATHING>(6);
    draw.b.monsters.arr[1].takeTurn(draw.b); draw.drain();
    check(wounds(draw.b)==1,"draw-trigger fixture did not generate Wound");
    auto wound=draw.b.cards.discardPile.back(); draw.b.cards.discardPile.clear();
    draw.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED));
    draw.b.cards.drawPile.push_back(wound);
    draw.b.cards.draw(draw.b,1); draw.drain();
    check(draw.b.cards.cardsInHand==2,"generated Wound missed Evolve draw");
    for(int i=0;i<3;++i) check(draw.b.monsters.arr[i].curHp==994,"generated Wound missed Fire Breathing");
}
void refusals() {
    for(auto e:batch) {
        Fixture f(e);
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds);},"missing history admitted");
        for(int asc:{11,18,20}) { auto b=f.b; b.ascension=asc;
            rejects([&]{validateEncounterRecipe(b);},"unreviewed ascension admitted"); }
        auto stale=f.history; f.end();
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&stale);},"stale history admitted");
        PublicEnemyHistory late; late.observe(publicEnemyFrame(f.b));
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&late);},"late history admitted");
        auto gap=f.history; f.end(); f.end(); gap.observe(publicEnemyFrame(f.b));
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&gap);},"missing turn admitted");
        auto unresolved=f.b; unresolved.addToBot(Actions::DrawCards(1));
        rejects([&]{resampleCombatContinuation(f.g,unresolved,seeds,-1,&f.history);},"queued root admitted");
    }
    Fixture wrong(ME::SLAVERS); std::swap(wrong.b.monsters.arr[0],wrong.b.monsters.arr[2]);
    rejects([&]{validateEncounterRecipe(wrong.b);},"reversed trio admitted");
    Fixture hits(ME::BOOK_OF_STABBING);
    while(hits.b.monsters.arr[0].moveHistory[0]!=MMID::BOOK_OF_STABBING_MULTI_STAB) hits.end();
    ++hits.b.monsters.arr[0].miscInfo;
    rejects([&]{auto b=hits.b;resampleCombatContinuation(hits.g,b,seeds,-1,&hits.history);},
            "changed visible hits did not stale history");
    hits.observe();
    rejects([&]{auto b=hits.b;resampleCombatContinuation(hits.g,b,seeds,-1,&hits.history);},
            "contradictory hit-count observation silently accepted");
    for(auto e:{ME::GREMLIN_LEADER,ME::COLOSSEUM_EVENT_SLAVERS,ME::COLOSSEUM_EVENT_NOBS}) {
        Fixture future(e);
        future.b.ascension=11; // These identities have separate A0-A10 evidence.
        rejects([&]{validateEncounterRecipe(future.b);},"unreviewed event/summon ascension admitted");
    }
}
}
int main() {
    try { lifecycles(); bookRules(); slaverEffects(); refusals();
        std::cout<<"act2-slavers-book-history-v1: 2 compositions, "<<roots<<" lifecycle roots, "
                 <<hiddenBookRoots<<" hidden Book counters, "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
