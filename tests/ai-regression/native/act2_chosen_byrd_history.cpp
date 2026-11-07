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
int roots=0, checks=0;
void check(bool ok,const char *why) { ++checks; if(!ok) throw std::runtime_error(why); }
template<class F> void rejects(F f,const char *why) {
    ++checks; try { f(); } catch(const std::runtime_error &) { return; }
    throw std::runtime_error(why);
}
std::string dump(const BattleContext &b) { std::ostringstream s; s<<b; return s.str(); }
std::string rng(const Random &r) { std::ostringstream s; s<<r.counter<<':'<<r.seed0<<':'<<r.seed1; return s.str(); }
const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
const std::array<ME,4> batch{ME::CHOSEN,ME::THREE_BYRDS,ME::CHOSEN_AND_BYRDS,ME::CULTIST_AND_CHOSEN};
struct Fixture {
    GameContext g; BattleContext b; PublicEnemyHistory history;
    Fixture(ME encounter,int seed=7):g(CharacterClass::IRONCLAD,seed,0) {
        g.act=2; g.floorNum=20; g.enterBattle(encounter); b.init(g); b.executeActions();
        // Synthetic durability for bounded lifecycle paths, not win-rate evidence.
        b.player.curHp=b.player.maxHp=10000;
        for(int i=0;i<b.monsters.monsterCount;++i) b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(b)); }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); observe(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); observe(); }
    void hit(int slot) { b.monsters.arr[slot].attacked(b,1); drain(); }
    void clearCards() { b.cards=CardManager{}; b.cards.nextUniqueCardId=100; b.player.energy=100; }
    void play(CardId id,bool up=false,int target=0) {
        b.cards.createTempCardInHand(CardInstance(id,up));
        auto c=b.cards.hand[b.cards.cardsInHand-1];
        check(c.canUse(b,target,false),"unplayable fixture card");
        b.addToBotCard(CardQueueItem(c,target,b.player.energy)); drain();
    }
};
void sample(Fixture &f) {
    const auto source=dump(f.b);
    const auto frame=publicEnemyFrame(f.b);
    auto a=f.b,b=f.b;
    b.aiRng=Random(901); b.cardRandomRng=Random(902); b.miscRng=Random(903);
    b.monsterHpRng=Random(904); b.potionRng=Random(905); b.shuffleRng=Random(906);
    // A hidden-state twin may permute only UNKNOWN positions. The event overlay
    // can retain a publicly revealed top after Mayhem; reversing it as well
    // creates contradictory evidence, not a valid same-public-state twin.
    const auto bottom = b.cards.publicKnownBottomIds.size(), top = b.cards.publicKnownTopIds.size();
    check(bottom + top <= b.cards.drawPile.size(), "overlapping known draw boundaries in fixture");
    std::reverse(b.cards.drawPile.begin() + bottom, b.cards.drawPile.end() - top);
    for (std::size_t i = 0; i < top; ++i)
        check(b.cards.drawPile[b.cards.drawPile.size() - top + i].uniqueId == b.cards.publicKnownTopIds[i],
              "hidden twin changed known top identity");
    for (std::size_t i = 0; i < bottom; ++i)
        check(b.cards.drawPile[i].uniqueId == b.cards.publicKnownBottomIds[i],
              "hidden twin changed known bottom identity");
    for(int i=0;i<b.monsters.monsterCount;++i) {
        b.monsters.arr[i].miscInfo=999; b.monsters.arr[i].moveHistory[1]=MMID::INVALID;
        b.monsters.arr[i].escapeNext=true;
    }
    check(publicEnemyFrame(b)==frame,"poisoning changed public frame");
    resampleCombatContinuation(f.g,a,seeds,-1,&f.history);
    resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
    check(publicEnemyFrame(a)==frame && dump(a)==dump(b),"same-public samples depend on source hidden state");
    check(dump(f.b)==source,"sampling mutated source");
    for(int i=0;i<a.monsters.monsterCount;++i) {
        const auto &m=a.monsters.arr[i], &original=f.b.monsters.arr[i];
        check(m.statusBits==original.statusBits && m.uniquePower0==original.uniquePower0,
              "sampling changed public Flight/Ritual powers");
        if(!m.isDeadOrEscaped()) {
            const bool stunned=m.id==MonsterId::BYRD && m.moveHistory[0]==MMID::BYRD_STUNNED;
            check(m.moveHistory[1]==(stunned?MMID::INVALID:original.moveHistory[1]),"observed prior intent mismatch");
        }
    }
    check(a.player.hasStatus<PS::HEX>()==f.b.player.hasStatus<PS::HEX>(),"sampling changed public Hex");
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    check(dump(a)==dump(b),"sampled continuations diverge");
    if(a.outcome==Outcome::UNDECIDED) {
        auto child=f.history; child.observe(publicEnemyFrame(a));
        resampleCombatContinuation(f.g,a,seeds,-1,&child);
    }
    ++roots;
}
void lifecycles() {
    for(auto e:batch) for(int seed=0;seed<16;++seed) {
        Fixture f(e,seed);
        for(int turn=0;turn<10;++turn) { sample(f); f.end(); }
        if(f.b.monsters.monsterCount>1) for(int victim=0;victim<f.b.monsters.monsterCount;++victim) {
            auto group=f; group.b.monsters.arr[victim].damage(group.b,99999); group.drain();
            check(group.b.monsters.monsterCount==f.b.monsters.monsterCount,"dead slots compacted");
            for(int turn=0;turn<3;++turn) { sample(group); group.end(); }
        }
        for(int target=0;target<f.b.monsters.monsterCount;++target) if(f.b.monsters.arr[target].id==MonsterId::BYRD) {
            auto cycle=f; auto &m=cycle.b.monsters.arr[target];
            for(int round=0;round<2;++round) {
                for(int hit=0;hit<3;++hit) { cycle.hit(target); sample(cycle); }
                check(m.moveHistory[0]==MMID::BYRD_STUNNED && !m.hasStatus<MS::FLIGHT>(),"Byrd not grounded");
                cycle.end(); check(m.moveHistory[0]==MMID::BYRD_HEADBUTT,"stun recovery"); sample(cycle);
                cycle.end(); check(m.moveHistory[0]==MMID::BYRD_FLY,"headbutt recovery"); sample(cycle);
                cycle.end(); check(m.hasStatus<MS::FLIGHT>() && m.getStatus<MS::FLIGHT>()==3,"flight recovery"); sample(cycle);
            }
        }
    }
}
void byrdEffects() {
    Fixture f(ME::CHOSEN_AND_BYRDS); auto &m=f.b.monsters.arr[0];
    f.hit(0); f.end();
    check(m.getStatus<MS::FLIGHT>()==3,"Flight must reset on enemy turn if still flying"); sample(f);
    // One remaining hit after the enemy rolls, before the next public observation.
    // Mayhem knocks it down at player start; hidden intermediate intent is irrelevant.
    f.clearCards();
    for(int i=0;i<10;++i) f.b.cards.createTempCardInDrawPile(i,CardInstance(CardId::STRIKE_RED));
    f.b.player.buff<PS::MAYHEM>(3);
    // One live target makes all three automatic attacks deterministic.
    f.b.monsters.arr[1].damage(f.b,99999); f.drain(); f.end();
    check(m.moveHistory[0]==MMID::BYRD_STUNNED && !m.hasStatus<MS::FLIGHT>(),"Mayhem did not ground after roll");
    sample(f); f.end(); sample(f);
    for(auto move:{MMID::BYRD_CAW,MMID::BYRD_PECK,MMID::BYRD_SWOOP,MMID::BYRD_HEADBUTT}) {
        Fixture effect(ME::THREE_BYRDS); auto &bird=effect.b.monsters.arr[0];
        bird.moveHistory[0]=move; bird.takeTurn(effect.b); effect.drain();
        const int expected=move==MMID::BYRD_CAW?0:move==MMID::BYRD_PECK?5:move==MMID::BYRD_SWOOP?12:3;
        check(effect.b.player.curHp==10000-expected,"Byrd attack base/multi-hit damage");
        check(bird.strength==(move==MMID::BYRD_CAW?1:0),"Byrd Caw strength");
    }
}
void moveTables() {
    Fixture f(ME::THREE_BYRDS); auto &m=f.b.monsters.arr[0];
    const MMID moves[]{MMID::BYRD_PECK,MMID::BYRD_SWOOP,MMID::BYRD_CAW,MMID::BYRD_FLY};
    std::set<MMID> opening;
    for(int seed=0;seed<16;++seed) for(int roll=0;roll<100;++roll) {
        f.b.aiRng=Random(seed); auto expectedRng=f.b.aiRng;
        m.moveHistory[0]=m.moveHistory[1]=MMID::INVALID;
        auto expected=expectedRng.randomBoolean(.375f)?MMID::BYRD_CAW:MMID::BYRD_PECK;
        int data=0; auto actual=m.getMoveForRoll(f.b,data,roll); opening.insert(actual);
        check(actual==expected && rng(f.b.aiRng)==rng(expectedRng),"Byrd opening selection/RNG");
        for(auto current:moves) for(auto previous:moves) {
            m.moveHistory[0]=current; m.moveHistory[1]=previous;
            f.b.aiRng=Random(seed); expectedRng=f.b.aiRng;
            if(roll<50) expected=current==moves[0] && previous==moves[0]?
                (expectedRng.randomBoolean(.4f)?moves[1]:moves[2]):moves[0];
            else if(roll<70) expected=current==moves[1]?
                (expectedRng.randomBoolean(.375f)?moves[2]:moves[0]):moves[1];
            else expected=current==moves[2]?
                (expectedRng.randomBoolean(.2857f)?moves[1]:moves[0]):moves[2];
            check(m.getMoveForRoll(f.b,data,roll)==expected && rng(f.b.aiRng)==rng(expectedRng),"Byrd move history/RNG table");
        }
    }
    check(opening.size()==2,"both Byrd opening intents not exercised");
    Fixture c(ME::CHOSEN); auto &chosen=c.b.monsters.arr[0];
    const MMID chosenMoves[]{MMID::CHOSEN_POKE,MMID::CHOSEN_HEX,MMID::CHOSEN_DRAIN,
                            MMID::CHOSEN_DEBILITATE,MMID::CHOSEN_ZAP};
    for(int roll=0;roll<100;++roll) for(auto current:chosenMoves) {
        chosen.moveHistory[0]=current; chosen.moveHistory[1]=MMID::CHOSEN_POKE;
        const bool debuff=current==MMID::CHOSEN_DRAIN || current==MMID::CHOSEN_DEBILITATE;
        const auto expected=debuff?(roll<40?MMID::CHOSEN_ZAP:MMID::CHOSEN_POKE):
                                   (roll<50?MMID::CHOSEN_DEBILITATE:MMID::CHOSEN_DRAIN);
        int data=0; auto before=rng(c.b.aiRng);
        check(chosen.getMoveForRoll(c.b,data,roll)==expected && rng(c.b.aiRng)==before,"Chosen alternating move table");
    }
}
int countDraw(const BattleContext &b,CardId id) {
    return std::count_if(b.cards.drawPile.begin(),b.cards.drawPile.end(),[=](const CardInstance &c){return c.id==id;});
}
void hexEffects() {
    for(bool artifact:{false,true}) {
        Fixture f(ME::CHOSEN);
        check(f.b.monsters.arr[0].moveHistory[0]==MMID::CHOSEN_POKE,"Chosen A0 opening");
        f.end(); check(f.b.player.curHp==9990 && f.b.monsters.arr[0].moveHistory[0]==MMID::CHOSEN_HEX,"Chosen first Poke/Hex");
        if(artifact) f.b.player.artifact=1;
        f.end(); check(f.b.player.hasStatus<PS::HEX>()==!artifact && !f.b.player.hasStatus<PS::ARTIFACT>(),"Hex Artifact interception");
        f.clearCards(); sample(f);
        for(auto id:{CardId::DEFEND_RED,CardId::INFLAME,CardId::STRIKE_RED,CardId::SLIMED}) {
            const auto before=countDraw(f.b,CardId::DAZED); f.play(id);
            check(countDraw(f.b,CardId::DAZED)==before+(!artifact && id!=CardId::STRIKE_RED),"Hex non-attack trigger");
            sample(f);
        }
        for(int turn=0;turn<8;++turn) {
            f.end(); check(f.b.monsters.arr[0].moveHistory[0]!=MMID::CHOSEN_HEX,"Chosen repeats blocked/used Hex"); sample(f);
        }
    }
    Fixture timing(ME::CHOSEN); timing.end(); timing.end(); timing.clearCards();
    timing.play(CardId::SHRUG_IT_OFF);
    check(timing.b.cards.cardsInHand==0 && countDraw(timing.b,CardId::DAZED)==1,"Hex insertion must follow skill draw"); sample(timing);
    timing.play(CardId::INFLAME); timing.play(CardId::EVOLVE); timing.play(CardId::FIRE_BREATHING);
    // Created statuses have the normal ethereal lifecycle and draw callbacks.
    const auto hp=timing.b.monsters.arr[0].curHp;
    Actions::DrawCards(1).actFunc(timing.b); timing.drain();
    check(timing.b.monsters.arr[0].curHp<hp,"Hex Dazed failed Fire Breathing on draw");
    sample(timing);
    Fixture repeated(ME::CHOSEN); repeated.end(); repeated.end(); repeated.clearCards();
    repeated.b.player.buff<PS::DUPLICATION>(1); repeated.play(CardId::DEFEND_RED);
    check(countDraw(repeated.b,CardId::DAZED)==2 && repeated.b.player.block==10,"Hex per repeated non-attack play");
    sample(repeated);
    for(auto card:{CardId::DEFEND_RED,CardId::STRIKE_RED,CardId::DAZED}) {
        Fixture automatic(ME::CHOSEN); automatic.end(); automatic.end(); automatic.clearCards();
        automatic.b.cards.createTempCardInDrawPile(0,CardInstance(card));
        const int played=automatic.b.player.cardsPlayedThisTurn;
        automatic.b.playTopCardInDrawPile(0,false); automatic.drain();
        check(countDraw(automatic.b,CardId::DAZED)==(card==CardId::DEFEND_RED?1:0),
              "Hex successful/non-attack autoplay versus rejected status");
        check(automatic.b.player.cardsPlayedThisTurn==played+(card!=CardId::DAZED),"rejected autoplay counted as play");
        sample(automatic);
    }
    Fixture deadSource(ME::CHOSEN_AND_BYRDS); deadSource.end(); deadSource.end();
    deadSource.b.monsters.arr[1].damage(deadSource.b,99999); deadSource.drain(); deadSource.clearCards();
    deadSource.play(CardId::DEFEND_RED);
    check(countDraw(deadSource.b,CardId::DAZED)==1,"Hex incorrectly disappears when Chosen dies"); sample(deadSource);
    Fixture ordered(ME::CHOSEN); ordered.end(); ordered.end(); ordered.clearCards();
    ordered.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED));
    ordered.b.cards.publicKnownTopIds={ordered.b.cards.drawPile.back().uniqueId};
    ordered.play(CardId::DEFEND_RED);
    // Random insertion occurs BELOW the current top: one
    // known top remains certain, but multiple known top cards become conditional.
    check(!ordered.b.cards.publicTopOrderUncertain && ordered.b.cards.publicKnownTopIds.size()==1,
          "Hex insertion lost the unaffected known singleton top");
    sample(ordered);
    Fixture multi(ME::CHOSEN); multi.end(); multi.end(); multi.clearCards();
    multi.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED));
    multi.b.cards.createTempCardInDrawPile(1,CardInstance(CardId::BASH));
    for(const auto& c:multi.b.cards.drawPile) multi.b.cards.publicKnownTopIds.push_back(c.uniqueId);
    multi.play(CardId::DEFEND_RED);
    check(multi.b.cards.publicTopOrderUncertain && multi.b.cards.publicKnownTopIds.size()==1,
          "Hex did not preserve the top and flag deeper conditional order");
    rejects([&]{auto b=multi.b;resampleCombatContinuation(multi.g,b,seeds,-1,&multi.history);},
            "legacy edge-only sampler ignored conditional multi-card order");
    for(auto move:{MMID::CHOSEN_DEBILITATE,MMID::CHOSEN_DRAIN,MMID::CHOSEN_ZAP}) {
        Fixture effect(ME::CHOSEN); auto &m=effect.b.monsters.arr[0];
        m.moveHistory[0]=move; m.takeTurn(effect.b); effect.drain();
        check(effect.b.player.curHp==10000-(move==MMID::CHOSEN_DEBILITATE?10:move==MMID::CHOSEN_ZAP?18:0),"Chosen move damage");
        if(move==MMID::CHOSEN_DEBILITATE) check(effect.b.player.getStatus<PS::VULNERABLE>()==2,"Chosen Vulnerable");
        if(move==MMID::CHOSEN_DRAIN) check(effect.b.player.getStatus<PS::WEAK>()==3 && m.strength==3,"Chosen Drain effects");
    }
}
void refusals() {
    for(auto e:batch) {
        Fixture f(e);
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds);},"missing history admitted");
        auto high=f.b; high.ascension=11;
        rejects([&]{validateEncounterRecipe(high);},"unreviewed A11 admitted");
        auto stale=f.history; f.end();
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&stale);},"stale history admitted");
        PublicEnemyHistory late; late.observe(publicEnemyFrame(f.b));
        rejects([&]{auto b=f.b;resampleCombatContinuation(f.g,b,seeds,-1,&late);},"late history admitted");
    }
    Fixture wrong(ME::CHOSEN_AND_BYRDS); std::swap(wrong.b.monsters.arr[0],wrong.b.monsters.arr[1]);
    rejects([&]{validateEncounterRecipe(wrong.b);},"reversed composition admitted");
    Fixture flight(ME::THREE_BYRDS); flight.b.monsters.arr[0].removeStatus<MS::FLIGHT>();
    rejects([&]{auto b=flight.b;resampleCombatContinuation(flight.g,b,seeds,-1,&flight.history);},"inconsistent Flight admitted");
    Fixture phase(ME::CHOSEN); phase.b.monsters.arr[0].moveHistory[0]=MMID::CHOSEN_HEX;
    phase.history=PublicEnemyHistory(); phase.observe();
    rejects([&]{auto b=phase.b;resampleCombatContinuation(phase.g,b,seeds,-1,&phase.history);},"invalid opening admitted");
    Fixture future(ME::GREMLIN_LEADER);
    future.b.ascension=11; // A0-A10 is admitted by its reviewed lifecycle recipe.
    rejects([&]{validateEncounterRecipe(future.b);},"unreviewed summon ascension admitted");
}
}
int main() {
    try { lifecycles(); byrdEffects(); moveTables(); hexEffects(); refusals();
        std::cout<<"act2-chosen-byrd-history-v1: 4 compositions, "<<roots<<" lifecycle roots, "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
