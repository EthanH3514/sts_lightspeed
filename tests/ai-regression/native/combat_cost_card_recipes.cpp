#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
#include "corruption_cost_watch.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *msg) {++checks; if(!ok) {++failures; std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game; BattleContext b;
    bool watchNaturalCosts=true;
    void watch(const char *stage) {
        if(!watchNaturalCosts) return; // Explicit synthetic-matrix control, not an ignored failure.
        const auto found=corruption_cost_watch::differences(b.cards);
        for(const auto &item:found) std::cerr<<stage<<": "<<item<<'\n';
        check(found.empty(),"natural action reached Corruption cost boundary; preserve sequence and review deferred PR");
    }
    explicit Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={}; if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100; b.player.energy=20;
        b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(int i=0;i<10;++i) b.cards.createTempCardInDrawPile(i,CardInstance(CardId::STRIKE_RED));
    }
    void add(CardId id,int base=-99,int turn=-99) {
        CardInstance c(id); if(base!=-99) c.cost=base; if(turn!=-99) c.costForTurn=turn;
        b.cards.createTempCardInHand(c);
    }
    void use(int index) {
        watch("before card play");
        auto c=b.cards.hand[index]; b.addToBotCard(CardQueueItem(c,0,b.player.energy));
        b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();
        watch("after card play");
    }
    void play(CardId id,bool up=false) {b.cards.createTempCardInHand(CardInstance(id,up)); use(b.cards.cardsInHand-1);}
};
void enlightenment() {
    for(bool up:{false,true}) for(int base:{-2,-1,0,1,2,4}) for(int turn:{-2,-1,0,1,2,4}) {
        Fixture f; f.watchNaturalCosts=false; f.add(CardId::BLUDGEON,base,turn); const int uid=f.b.cards.hand[0].uniqueId;
        f.play(CardId::ENLIGHTENMENT,up);
        check(f.b.cards.hand[0].cost==(up&&base>1?1:base),"Enlightenment combat cost independently capped");
        check(f.b.cards.hand[0].costForTurn==(turn>1?1:turn),"Enlightenment preserves temporary zero/negative turn cost");
        check(f.b.cards.hand[0].uniqueId==uid&&f.b.cards.exhaustPile.empty(),"Enlightenment identity/non-exhaust");
        check(f.b.player.energy==20,"Enlightenment zero cost");
    }
    for(bool up:{false,true}) {
        Fixture f; f.add(CardId::BLUDGEON); f.play(CardId::ENLIGHTENMENT,up);
        f.b.cards.resetAttributesAtEndOfTurn();
        check(f.b.cards.hand[0].costForTurn==(up?1:3),"turn-only/combat duration");
        Fixture g; g.add(CardId::DARK_EMBRACE,2,0); g.play(CardId::ENLIGHTENMENT,up);
        g.b.player.energy=0;
        check(g.b.cards.hand[0].canUseOnAnyTarget(g.b),"temporary-zero power remains playable at zero energy");
        g.b.cards.hand[0].upgrade();
        check(g.b.cards.hand[0].cost==1&&g.b.cards.hand[0].costForTurn==0,"upgrade composes with preserved zero");
    }
}
void corruptionCosts() {
    for(int base:{-2,-1,0,1,3,12}) for(int turn:{-2,-1,0,1,4,12}) {
        Fixture f; f.watchNaturalCosts=false; f.add(CardId::DEFEND_RED,base,turn); auto c=f.b.cards.hand[0];
        f.b.cards.createTempCardInDrawPile(0,c); f.b.cards.createTempCardInDiscard(c);
        c.setUniqueId(f.b.cards.nextUniqueCardId++); f.b.cards.exhaustPile.push_back(c);
        int expectedBase=base, expectedTurn=turn;
        if(turn>0) expectedBase=expectedTurn=std::max(0,turn-9);
        else if(base>=0) {expectedBase=std::max(0,base-9); expectedTurn=0;}
        f.play(CardId::CORRUPTION);
        for(const auto *card:{&f.b.cards.hand[0],&f.b.cards.drawPile[0],&f.b.cards.discardPile[0],&f.b.cards.exhaustPile[0]}) {
            check(card->cost==expectedBase&&card->costForTurn==expectedTurn,"Corruption matches modifyCostForCombat(-9) in all four piles");
        }
        check(f.b.cards.drawPile[1].cost==1,"Corruption leaves attacks unchanged");
    }
    for(bool up:{false,true}) {
        Fixture f; f.add(CardId::DEFEND_RED); f.play(CardId::CORRUPTION,up);
        check(f.b.player.energy==20-(up?2:3),"Corruption variant costs");
        check(f.b.player.hasStatus<PS::CORRUPTION>(),"Corruption persistent power");
        f.b.player.buff<PS::FEEL_NO_PAIN>(3); f.b.player.buff<PS::DARK_EMBRACE>(1);
        f.b.player.energy=0; check(f.b.cards.hand[0].canUseOnAnyTarget(f.b),"Corrupted skill legal at zero energy");
        f.use(0);
        check(f.b.player.block==8&&f.b.cards.exhaustPile.size()==1,"skill block plus exhaust block exactly once");
        check(f.b.cards.cardsInHand==1&&f.b.player.energy==0,"Dark Embrace draws; skill spends no energy");
        f.b.cards.createTempCardInDrawPile(f.b.cards.drawPile.size(),CardInstance(CardId::DEFEND_RED));
        f.b.addToBot(Actions::DrawCards(1)); f.b.inputState=InputState::EXECUTING_ACTIONS; f.b.executeActions();
        check(f.b.cards.hand[1].costForTurn==0,"later skill draw gets turn zero");
        f.b.player.energy=20; f.play(CardId::MADNESS,true);
        check(f.b.cards.exhaustPile.size()==2&&f.b.player.block==11,"intrinsic exhaust plus Corruption triggers once");
        for(int i=0;i<f.game.deck.size();++i) check(f.game.deck.cards[i].getId()!=CardId::CORRUPTION,"combat mutation not master-deck addition");
    }
    Fixture special; special.watchNaturalCosts=false;
    special.add(CardId::DEFEND_RED,0,3);
    check(corruption_cost_watch::differences(special.b.cards).size()==1,"watch detects known synthetic0/3 before local fix");
    special.play(CardId::CORRUPTION);
    special.b.player.energy=0;
    check(special.b.cards.hand[0].canUseOnAnyTarget(special.b),"Corruption handles zero-base/positive-turn skill");
    check(corruption_cost_watch::differences(special.b.cards).empty(),"local fix drains boundary; watch must inspect before play");
}
void corruptionWatchControls() {
    Fixture f;
    for(int pile=0;pile<4;++pile) {
        auto cards=f.b.cards;
        CardInstance c(CardId::DEFEND_RED); c.cost=0; c.costForTurn=3; c.setUniqueId(999);
        if(pile==0) {cards.hand[0]=c; cards.cardsInHand=1;}
        else if(pile==1) cards.drawPile.push_back(c);
        else if(pile==2) cards.discardPile.push_back(c);
        else cards.exhaustPile.push_back(c);
        const auto findings=corruption_cost_watch::differences(cards);
        check(findings.size()==1&&findings[0].find("before=0/3 legacy=0/3 original=0/0")!=std::string::npos,
              "watch diagnostics identify cost pair across all four piles");
    }
    for(int sequence=0;sequence<4;++sequence) {
        Fixture g; g.add(CardId::DEFEND_RED); g.add(CardId::IMPERVIOUS);
        if(sequence==0) g.play(CardId::MADNESS,true);
        if(sequence==1) g.play(CardId::ENLIGHTENMENT,true);
        if(sequence==2) g.play(CardId::APOTHEOSIS,true);
        if(sequence==3) {g.play(CardId::ENLIGHTENMENT); g.play(CardId::MADNESS,true);}
        g.b.cards.resetAttributesAtEndOfTurn(); g.watch("after cost reset");
        g.play(CardId::CORRUPTION);
    }
}
void madnessAndRoots() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(bool up:{false,true}) {
        Fixture f; f.add(CardId::BLUDGEON,3,0); f.add(CardId::DEFEND_RED,1,1);
        f.play(CardId::MADNESS,up);
        check(f.b.cards.hand[0].cost==3&&f.b.cards.hand[1].cost==0,"Madness prefers positive turn cost over positive base");
        check(f.b.player.energy==20-(up?0:1)&&f.b.cards.exhaustPile.size()==1,"Madness cost/exhaust variants");
        Fixture fallback; fallback.add(CardId::BLUDGEON,3,0); fallback.play(CardId::MADNESS,up);
        check(fallback.b.cards.hand[0].cost==0,"Madness falls back to positive combat cost");
        fallback.b.cards.resetAttributesAtEndOfTurn();
        check(fallback.b.cards.hand[0].costForTurn==0,"Madness zero persists across turn reset");
        Fixture none; none.add(CardId::ANGER); none.add(CardId::WHIRLWIND); none.add(CardId::WOUND);
        auto rng=none.b.cardRandomRng; none.play(CardId::MADNESS,up);
        check(none.b.cardRandomRng.counter==rng.counter,"no eligible card consumes no selection RNG");
    }
    std::set<int> targets;
    for(int seed=1;seed<=32;++seed) {
        Fixture f; f.add(CardId::BLUDGEON); f.add(CardId::DEFEND_RED); f.add(CardId::ANGER);
        f.b.cardRandomRng=Random(seed); f.play(CardId::MADNESS,true);
        const bool first=f.b.cards.hand[0].cost==0, second=f.b.cards.hand[1].cost==0;
        check(first!=second,"one eligible random target only"); targets.insert(first?0:1);
    }
    check(targets.size()==2,"seeded examples exercise both eligible targets");
    for(auto id:{CardId::CORRUPTION,CardId::MADNESS,CardId::ENLIGHTENMENT})
    for(bool up:{false,true}) for(bool frozen:{false,true}) {
        Fixture f(frozen); f.add(CardId::BLUDGEON); f.add(CardId::DEFEND_RED);
        auto twin=f.b; twin.cardRandomRng=Random(999);
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden-order independence");
        twin.cards.createTempCardInHand(CardInstance(id,up));
        twin.addToBotCard(CardQueueItem(twin.cards.hand[twin.cards.cardsInHand-1],0,twin.player.energy));
        twin.inputState=InputState::EXECUTING_ACTIONS; twin.executeActions();
        f.play(id,up); const auto before=f.b;
        for(int i=0;i<f.b.cards.cardsInHand;++i) check(f.b.cards.hand[i].cost==twin.cards.hand[i].cost&&f.b.cards.hand[i].costForTurn==twin.cards.hand[i].costForTurn,"paired sampled worlds produce same cost outcome despite original hidden RNG");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        for(int i=0;i<f.b.cards.cardsInHand;++i) check(f.b.cards.hand[i].cost==before.cards.hand[i].cost&&f.b.cards.hand[i].costForTurn==before.cards.hand[i].costForTurn,"post-play roots preserve both costs");
        if(frozen) for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==before.cards.drawPile[i].uniqueId,"Frozen Eye preserves order");
    }
}
}
int main() {
    enlightenment(); corruptionCosts(); madnessAndRoots(); corruptionWatchControls();
    std::cout<<"COMBAT_COST_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
