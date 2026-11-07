#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>
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
const std::array<ME,3> batch{ME::CENTURION_AND_HEALER,ME::SNECKO,ME::TWO_THIEVES};
struct Fixture {
    GameContext g; BattleContext b; PublicEnemyHistory history;
    Fixture(ME encounter,int seed=7):g(CharacterClass::IRONCLAD,seed,0) {
        g.act=2; g.floorNum=20; g.enterBattle(encounter); b.init(g); b.executeActions();
        // Synthetic direct encounters have no map/event reward callback.
        g.regainControlAction=[](GameContext &) {};
        // Synthetic durability for bounded lifecycle paths, not strategy quality.
        b.player.curHp=b.player.maxHp=10000;
        for(int i=0;i<b.monsters.monsterCount;++i) b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
        observe();
    }
    void observe() { history.observe(publicEnemyFrame(b)); }
    void resetHistory() { history=PublicEnemyHistory(); observe(); }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); observe(); }
    void end(bool observeAfter=true) {
        search::Action(search::ActionType::END_TURN).execute(b); if(observeAfter) observe();
    }
    void kill(int slot) { b.monsters.arr[slot].damage(b,99999); drain(); }
    void clearCards() { b.cards=CardManager{}; b.cards.nextUniqueCardId=100; b.player.energy=100; }
};
using CostRow=std::tuple<int,int,int,bool>;
std::vector<CostRow> costs(const BattleContext &b) {
    std::vector<CostRow> result;
    auto add=[&](const CardInstance &c){ result.emplace_back(c.uniqueId,c.cost,c.costForTurn,c.freeToPlayOnce); };
    for(int i=0;i<b.cards.cardsInHand;++i) add(b.cards.hand[i]);
    for(const auto *pile:{&b.cards.drawPile,&b.cards.discardPile,&b.cards.exhaustPile})
        for(const auto &c:*pile) add(c);
    std::sort(result.begin(),result.end()); return result;
}
void sample(Fixture &f) {
    const auto source=dump(f.b);
    const auto frame=publicEnemyFrame(f.b);
    auto a=f.b,b=f.b;
    b.aiRng=Random(901); b.cardRandomRng=Random(902); b.miscRng=Random(903);
    b.monsterHpRng=Random(904); b.potionRng=Random(905); b.shuffleRng=Random(906);
    std::reverse(b.cards.drawPile.begin(),b.cards.drawPile.end());
    for(int i=0;i<b.monsters.monsterCount;++i) {
        b.monsters.arr[i].miscInfo=999; b.monsters.arr[i].moveHistory[1]=MMID::INVALID;
        b.monsters.arr[i].escapeNext=true;
    }
    if(f.b.encounter==ME::TWO_THIEVES) b.setRequiresStolenGoldCheck(!f.b.requiresStolenGoldCheck());
    check(publicEnemyFrame(b)==frame,"poisoning changed public enemy frame");
    resampleCombatContinuation(f.g,a,seeds,-1,&f.history);
    resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
    check(publicEnemyFrame(a)==frame && dump(a)==dump(b),"same-public samples depend on source hidden state");
    check(costs(a)==costs(f.b),"sampling rerolled already visible card costs/free flags");
    check(dump(f.b)==source,"sampling mutated source");
    for(int i=0;i<a.monsters.monsterCount;++i) {
        const auto &m=a.monsters.arr[i], &original=f.b.monsters.arr[i];
        check(m.statusBits==original.statusBits && m.curHp==original.curHp &&
              m.maxHp==original.maxHp && m.block==original.block && m.strength==original.strength,
              "sampling changed public ally HP/block/powers");
        // Dead/escaped foes never use their prior-intent cache again.
        if(!m.isDeadOrEscaped())
            check(m.moveHistory[1]==original.moveHistory[1],"observed prior intent mismatch");
        if(isThief(m.id)) {
            check(m.miscInfo==original.miscInfo,"per-thief public gold reconstruction");
            check(m.isEscapingB==original.isEscapingB,"per-thief escape state changed");
        }
    }
    check(a.player.hasStatus<PS::CONFUSED>()==f.b.player.hasStatus<PS::CONFUSED>(),
          "sampling changed public Confusion");
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
    for(auto e:batch) for(int seed=0;seed<32;++seed) {
        Fixture f(e,seed);
        for(int turn=0;turn<12 && f.b.outcome==Outcome::UNDECIDED;++turn) { sample(f); f.end(); }
        if(e==ME::CENTURION_AND_HEALER) for(int victim=0;victim<2;++victim) {
            Fixture group(e,seed); group.kill(victim);
            check(group.b.monsters.monsterCount==2,"dead slots compacted");
            for(int turn=0;turn<6;++turn) { sample(group); group.end(); }
        }
    }
}
void allyEffects() {
    Fixture f(ME::CENTURION_AND_HEALER);
    auto &knight=f.b.monsters.arr[0], &healer=f.b.monsters.arr[1];
    for(int round=0;round<8;++round) {
        knight.damage(f.b,8); healer.damage(f.b,8); f.drain(); sample(f); f.end(); sample(f);
    }
    // Public damage after the intent was rolled must not recompute that intent.
    auto visible=healer.moveHistory[0]; healer.damage(f.b,100); f.drain(); sample(f);
    check(healer.moveHistory[0]==visible,"root mutation re-decided a visible healer intent");
    // Defend can stay visible after the ally dies; only the future action targets self.
    Fixture solo(ME::CENTURION_AND_HEALER);
    for(int i=0;i<10 && solo.b.monsters.arr[0].moveHistory[0]!=MMID::CENTURION_DEFEND;++i) solo.end();
    check(solo.b.monsters.arr[0].moveHistory[0]==MMID::CENTURION_DEFEND,"fixture never reached Defend");
    solo.kill(1); sample(solo); solo.end();
    check(solo.b.monsters.arr[0].block==15,"already-announced Defend did not protect lone Centurion");
    sample(solo);
    // Independent Centurion decision table: current/prior actions plus public ally presence.
    const MMID moves[]{MMID::INVALID,MMID::CENTURION_SLASH,MMID::CENTURION_DEFEND,MMID::CENTURION_FURY};
    for(bool alive:{false,true}) for(auto current:moves) for(auto previous:moves) for(int roll=0;roll<100;++roll) {
        solo.b.monsters.arr[1].curHp=alive?100:0; solo.b.monsters.monstersAlive=alive?2:1;
        auto &m=solo.b.monsters.arr[0]; m.moveHistory[0]=current; m.moveHistory[1]=previous;
        const auto two=[&](MMID move){return current==move && previous==move;};
        const bool defend=(roll>=65 && !two(MMID::CENTURION_DEFEND) && !two(MMID::CENTURION_FURY)) ||
                           two(MMID::CENTURION_SLASH);
        const auto expected=defend?(alive?MMID::CENTURION_DEFEND:MMID::CENTURION_FURY):MMID::CENTURION_SLASH;
        int data=0; const auto before=rng(solo.b.aiRng);
        check(m.getMoveForRoll(solo.b,data,roll)==expected && rng(solo.b.aiRng)==before,"Centurion move/history table");
    }
}
void thiefEffects() {
    int splitEscapes=0;
    for(int initial:{0,1,14,15,16,20,29,30,31,45,70,100}) for(int seed=0;seed<8;++seed) {
        Fixture f(ME::TWO_THIEVES,seed); f.b.player.gold=initial; f.resetHistory();
        int remaining=initial; std::array<int,2> expected{};
        for(int turn=0;turn<7 && f.b.outcome==Outcome::UNDECIDED;++turn) {
            sample(f);
            for(int i=0;i<2;++i) if(!f.b.monsters.arr[i].isDeadOrEscaped() &&
                                     isTheftIntent(f.b.monsters.arr[i].moveHistory[0])) {
                const int amount=std::min(remaining,15); expected[i]+=amount; remaining-=amount;
            }
            f.end();
            check(f.b.player.gold==remaining && f.b.monsters.arr[0].miscInfo==expected[0] &&
                  f.b.monsters.arr[1].miscInfo==expected[1],"ordered capped theft accounting");
            if(f.b.outcome==Outcome::UNDECIDED &&
               f.b.monsters.arr[0].isEscapingB!=f.b.monsters.arr[1].isEscapingB) ++splitEscapes;
        }
        check(f.b.outcome==Outcome::PLAYER_VICTORY,"two thieves failed to escape");
        auto game=f.g; f.b.exitBattle(game); check(game.info.stolenGold==0,"escaped thieves refunded gold");
    }
    check(splitEscapes>0,"independent thief escape timing not exercised");
    for(int victim=0;victim<2;++victim) {
        Fixture before(ME::TWO_THIEVES); before.kill(victim); sample(before); before.end(); sample(before);
        check(before.b.monsters.arr[victim].miscInfo==0 && before.b.monsters.arr[1-victim].miscInfo==15,
              "dead-before-turn thief changed surviving theft");
        Fixture refund(ME::TWO_THIEVES); refund.end(); sample(refund);
        auto restored=refund.b; restored.monsters.arr[0].miscInfo=999; restored.monsters.arr[1].miscInfo=999;
        restored.setRequiresStolenGoldCheck(false);
        resampleCombatContinuation(refund.g,restored,seeds,-1,&refund.history);
        restored.monsters.arr[victim].damage(restored,99999);
        restored.inputState=InputState::EXECUTING_ACTIONS; restored.executeActions();
        for(int turn=0;turn<6 && restored.outcome==Outcome::UNDECIDED;++turn)
            search::Action(search::ActionType::END_TURN).execute(restored);
        auto game=refund.g; restored.exitBattle(game);
        check(game.info.stolenGold==15,"only killed thief should refund its own stolen gold");
        Fixture later(ME::TWO_THIEVES); later.end();
        later.b.monsters.arr[victim].curHp=1; later.b.player.buff<PS::THORNS>(1); later.observe(); later.end();
        check(later.b.monsters.arr[victim].isDeadOrEscaped(),"second-Mug retaliation fixture");
        sample(later); // Changed intent proves this attack happened, unlike the first repeated Mug.
        for(bool beforeAttack:{false,true}) for(int gold:{0,100}) {
            Fixture ambiguous(ME::TWO_THIEVES); ambiguous.b.player.gold=gold;
            ambiguous.b.monsters.arr[victim].curHp=1;
            if(beforeAttack) ambiguous.b.player.buff<PS::COMBUST>(1);
            else ambiguous.b.player.buff<PS::THORNS>(1);
            ambiguous.resetHistory(); ambiguous.end();
            check(ambiguous.b.monsters.arr[victim].isDeadOrEscaped(),"first-Mug death fixture");
            if(gold) {
                rejects([&]{auto b=ambiguous.b;resampleCombatContinuation(ambiguous.g,b,seeds,-1,&ambiguous.history);},
                        "ambiguous first-Mug death admitted");
            } else sample(ambiguous);
        }
    }
    Fixture gain(ME::TWO_THIEVES); gain.b.player.gold=20; gain.resetHistory(); gain.end(false);
    gain.b.player.gold+=40; gain.observe(); sample(gain);
    check(gain.b.monsters.arr[0].miscInfo==15 && gain.b.monsters.arr[1].miscInfo==5,
          "start-turn gold gain masked separate theft totals");
    Fixture unequal(ME::TWO_THIEVES); unequal.b.player.gold=25;
    unequal.b.monsters.arr[0].setStatus<MS::THIEVERY>(7);
    unequal.b.monsters.arr[1].setStatus<MS::THIEVERY>(11);
    unequal.resetHistory(); unequal.end(); sample(unequal);
    check(unequal.b.monsters.arr[0].miscInfo==7 && unequal.b.monsters.arr[1].miscInfo==11,
          "theft reconstruction hard-coded normal amount");
}
void confusion() {
    for(bool artifact:{false,true}) {
        Fixture f(ME::SNECKO);
        check(f.b.monsters.arr[0].moveHistory[0]==MMID::SNECKO_PERPLEXING_GLARE,"Snecko opening");
        if(artifact) f.b.player.artifact=1;
        f.end();
        check(f.b.player.hasStatus<PS::CONFUSED>()==!artifact && !f.b.player.hasStatus<PS::ARTIFACT>(),
              "Confusion Artifact interception");
        for(int turn=0;turn<8;++turn) {
            check(f.b.monsters.arr[0].moveHistory[0]!=MMID::SNECKO_PERPLEXING_GLARE,"Snecko repeated blocked opening");
            sample(f); f.end();
        }
    }
    Fixture f(ME::SNECKO); f.end(); f.clearCards();
    const CardId ids[]{CardId::STRIKE_RED,CardId::DEFEND_RED,CardId::BASH,CardId::SLIMED,
                       CardId::WHIRLWIND,CardId::DAZED,CardId::PAIN};
    for(auto id:ids) for(int seed=0;seed<32;++seed) for(bool corruption:{false,true}) {
        auto b=f.b; b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        if(corruption) b.player.buff<PS::CORRUPTION>();
        CardInstance card(id); card.costForTurn=0; card.freeToPlayOnce=true;
        b.cards.createTempCardInDrawPile(0,card); b.cardRandomRng=Random(seed);
        auto expectedRng=b.cardRandomRng; int expectedCost=card.cost, expectedTurn=0;
        bool expectedFree=true;
        if(card.cost>=0) {
            expectedCost=expectedRng.random(3);
            if(expectedCost!=card.cost) expectedTurn=expectedCost;
            expectedFree=false;
        }
        if(corruption && card.getType()==CardType::SKILL) expectedTurn=0;
        b.cards.draw(b,1); const auto &drawn=b.cards.hand[0];
        check(drawn.cost==expectedCost && drawn.costForTurn==expectedTurn &&
              drawn.freeToPlayOnce==expectedFree,"Confusion base/turn/free-play cost semantics");
        check(rng(b.cardRandomRng)==rng(expectedRng),"Confusion RNG consumption/X/unplayable exclusions");
    }
    for(int seed=0;seed<32;++seed) {
        Fixture cost(ME::SNECKO); cost.end(); cost.clearCards(); cost.b.cardRandomRng=Random(seed);
        cost.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED)); cost.b.cards.draw(cost.b,1);
        const auto card=cost.b.cards.hand[0]; cost.b.player.energy=0;
        check(card.canUse(cost.b,0,false)==(card.costForTurn==0),"random visible cost not reflected in legality");
        cost.b.player.energy=card.costForTurn;
        const int hp=cost.b.monsters.arr[0].curHp;
        cost.b.addToBotCard(CardQueueItem(card,0,cost.b.player.energy)); cost.drain();
        check(cost.b.player.energy==0 && cost.b.monsters.arr[0].curHp==hp-6,"random cost payment/actual damage");
        sample(cost);
    }
    // Keep observed costs; roll only future draw costs from the branch's independent stream.
    std::set<std::vector<CostRow>> futures;
    for(int i=0;i<10;++i) f.b.cards.createTempCardInDrawPile(i,CardInstance(CardId::STRIKE_RED));
    sample(f);
    for(int seed=0;seed<32;++seed) {
        auto a=f.b,b=f.b; auto latent=seeds; latent[1]=seed; b.cardRandomRng=Random(500+seed);
        resampleCombatContinuation(f.g,a,latent,-1,&f.history);
        resampleCombatContinuation(f.g,b,latent,-1,&f.history);
        a.cards.draw(a,5); b.cards.draw(b,5);
        check(costs(a)==costs(b),"future draw cost consulted original RNG");
        futures.insert(costs(a));
    }
    check(futures.size()>1,"independent confusion worlds never branch");
    auto noDraw=f.b; noDraw.player.buff<PS::NO_DRAW>(); const auto before=rng(noDraw.cardRandomRng);
    Actions::DrawCards(1).actFunc(noDraw); noDraw.inputState=InputState::EXECUTING_ACTIONS; noDraw.executeActions();
    check(noDraw.cards.cardsInHand==0 && rng(noDraw.cardRandomRng)==before,"No Draw consumed a Confusion roll");
    auto generated=f.b; const auto beforeGeneration=rng(generated.cardRandomRng);
    generated.cards.createTempCardInHand(CardInstance(CardId::BASH));
    check(generated.cards.hand[0].cost==2 && rng(generated.cardRandomRng)==beforeGeneration,
          "hand generation wrongly invoked Confusion");
    // Independent Snecko move table: no third consecutive Bite, no repeat Glare.
    const MMID moves[]{MMID::SNECKO_PERPLEXING_GLARE,MMID::SNECKO_BITE,MMID::SNECKO_TAIL_WHIP};
    auto &m=f.b.monsters.arr[0];
    for(auto current:moves) for(auto previous:moves) for(int roll=0;roll<100;++roll) {
        m.moveHistory[0]=current; m.moveHistory[1]=previous;
        const auto expected=roll<40 || (current==MMID::SNECKO_BITE && previous==MMID::SNECKO_BITE)?
                            MMID::SNECKO_TAIL_WHIP:MMID::SNECKO_BITE;
        int data=0; const auto state=rng(f.b.aiRng);
        check(m.getMoveForRoll(f.b,data,roll)==expected && rng(f.b.aiRng)==state,"Snecko move/history table");
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
    for(auto e:{ME::CENTURION_AND_HEALER,ME::TWO_THIEVES}) {
        Fixture wrong(e); std::swap(wrong.b.monsters.arr[0],wrong.b.monsters.arr[1]);
        rejects([&]{validateEncounterRecipe(wrong.b);},"reversed composition admitted");
    }
    Fixture phase(ME::SNECKO); phase.b.monsters.arr[0].moveHistory[0]=MMID::SNECKO_BITE; phase.resetHistory();
    rejects([&]{auto b=phase.b;resampleCombatContinuation(phase.g,b,seeds,-1,&phase.history);},"invalid Snecko opening");
    Fixture future(ME::GREMLIN_LEADER);
    future.b.ascension=11; // A0-A10 is admitted by its reviewed lifecycle recipe.
    rejects([&]{validateEncounterRecipe(future.b);},"unreviewed summon ascension admitted");
}
}
int main() {
    try { lifecycles(); allyEffects(); thiefEffects(); confusion(); refusals();
        std::cout<<"act2-ordinary-history-v1: 3 compositions, "<<roots<<" lifecycle roots, "<<checks<<" checks passed\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
