#include <algorithm>
#include <array>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) {
    ++checks; if(!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game; BattleContext b;
    explicit Fixture(int draws=10,bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100; b.player.energy=20;
        b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(int i=0;i<draws;++i) b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
    }
    void add(CardId id,bool up=false) {b.cards.createTempCardInHand(CardInstance(id,up));}
    void play(CardId id,bool up=false) {
        add(id,up); auto c=b.cards.hand[b.cards.cardsInHand-1];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy));
        b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();
    }
};
void restriction() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(bool up:{false,true}) for(bool artifact:{false,true}) for(bool already:{false,true}) {
        Fixture f;
        if(already) f.b.player.debuff<PS::NO_DRAW>(1,false);
        if(artifact) f.b.player.buff<PS::ARTIFACT>(1);
        f.play(CardId::BATTLE_TRANCE,up);
        check(f.b.cards.cardsInHand==(already?0:up?4:3),"Battle Trance draws before its own No Draw");
        check(f.b.player.hasStatus<PS::NO_DRAW>()==(already||!artifact),"Artifact blocks new No Draw only");
        check(!f.b.player.hasStatus<PS::ARTIFACT>(),"Artifact consumed by debuff");
        check(f.b.player.energy==20&&f.b.cards.exhaustPile.empty(),"Trance zero cost and not exhaust");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.player.hasStatus<PS::NO_DRAW>()==(already||!artifact),"resolved Trance root retains public restriction");
        const int count=f.b.cards.cardsInHand;
        f.play(CardId::MASTER_OF_STRATEGY);
        check(f.b.cards.cardsInHand==count+((already||!artifact)?0:3),"No Draw blocks later draw");
        f.b.monsters.arr[0].setMove(MMID::CULTIST_INCANTATION);
        search::Action(search::ActionType::END_TURN).execute(f.b);
        check(!f.b.player.hasStatus<PS::NO_DRAW>()&&f.b.cards.cardsInHand==5,"No Draw gone before next turn draw");
    }
    Fixture evolve; evolve.b.cards.drawPile.back().id=CardId::WOUND;
    evolve.b.player.buff<PS::EVOLVE>(1); evolve.play(CardId::BATTLE_TRANCE);
    check(evolve.b.cards.cardsInHand==3,"Evolve queues at bottom; Trance No Draw blocks its later callback");
    Fixture unblocked; unblocked.b.cards.drawPile.back().id=CardId::WOUND;
    unblocked.b.player.buff<PS::EVOLVE>(1); unblocked.b.player.buff<PS::ARTIFACT>(1);
    unblocked.play(CardId::BATTLE_TRANCE);
    check(unblocked.b.cards.cardsInHand==4,"Artifact allows the queued Evolve draw");
}
void movement() {
    for(auto id:{CardId::WARCRY,CardId::THINKING_AHEAD}) for(bool up:{false,true})
    for(int initial:{0,1,2,9}) for(int draws:{0,1,4}) for(bool blocked:{false,true}) {
        Fixture f(draws);
        // Avoid the simulator's unrelated empty-active-deck loss shortcut in
        // zero-candidate tests: a public damage power keeps combat viable.
        f.b.player.buff<PS::THORNS>(1);
        for(int i=0;i<initial;++i) f.add(CardId::DEFEND_RED);
        if(blocked) f.b.player.debuff<PS::NO_DRAW>(1,false);
        const int requested=id==CardId::WARCRY&&!up?1:2;
        const int total=initial+(blocked?0:std::min({requested,draws,10-initial}));
        const auto rngBefore=f.b.cardRandomRng.counter;
        f.play(id,up);
        check((f.b.inputState==InputState::CARD_SELECT)==(total>1),"selection only for multiple post-draw candidates");
        int chosen=-1;
        if(total>1) {
            check(f.b.cardSelectInfo.cardSelectTask==CardSelectTask::WARCRY,"shared hand-to-top task");
            chosen=f.b.cards.hand[total-1].uniqueId;
            search::Action(search::ActionType::SINGLE_CARD_SELECT,total-1).execute(f.b);
        } else if(total==1) chosen=f.b.publicMovedDrawTopUniqueId;
        check(f.b.inputState==InputState::PLAYER_NORMAL,"selection returns normal control");
        check(f.b.cards.cardsInHand==std::max(0,total-1),"draw then defer exactly one if nonempty");
        check(f.b.publicMovedDrawTopUniqueId==chosen,"public movement identity recorded for manual and automatic choices");
        if(chosen>=0) check(f.b.cards.drawPile.back().uniqueId==chosen,"chosen identity on top");
        check(f.b.cardRandomRng.counter-rngBefore==(total==1?1:0),"automatic singleton consumes original RNG call; manual choice does not");
        check(f.b.player.energy==20,"both movement cards cost zero");
        check(f.b.cards.exhaustPile.size()==(id==CardId::WARCRY||!up?1:0),"Warcry always exhausts; Thinking Ahead only base");
    }
}
void callbacksAndSampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::WARCRY,CardId::THINKING_AHEAD}) for(bool up:{false,true})
    for(bool frozen:{false,true}) {
        Fixture f(10,frozen); f.add(CardId::DEFEND_RED);
        auto twin=f.b; twin.cardRandomRng=Random(98765);
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden-order/RNG independence before play");
        f.play(id,up); const int chosen=f.b.cards.hand[0].uniqueId;
        bool rejected=false;
        try { auto pending=f.b; public_sampling::resampleCombatContinuation(f.game,pending,seeds); }
        catch(const std::exception &) {rejected=true;}
        check(rejected,"pending selection is not a sampled root");
        search::Action(search::ActionType::SINGLE_CARD_SELECT,0).execute(f.b);
        const auto order=f.b.cards.drawPile;
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds,chosen);
        check(f.b.cards.drawPile.back().uniqueId==chosen,"resolved sampled root retains publicly selected top");
        if(frozen) for(size_t i=0;i<order.size();++i)
            check(f.b.cards.drawPile[i].uniqueId==order[i].uniqueId,"Frozen Eye preserves complete order");
        Fixture g; g.add(CardId::DEFEND_RED);
        g.b.player.buff<PS::DARK_EMBRACE>(1); g.b.player.buff<PS::FEEL_NO_PAIN>(3);
        g.play(id,up); const int uid=g.b.cards.hand[0].uniqueId;
        search::Action(search::ActionType::SINGLE_CARD_SELECT,0).execute(g.b);
        const bool exhaust=id==CardId::WARCRY||!up;
        check(g.b.player.block==(exhaust?3:0),"Feel No Pain uses variant exhaustion");
        check((g.b.cards.drawPile.back().uniqueId==uid)==!exhaust,"Dark Embrace immediately consumes put-back card");
    }
}
}
int main() {
    restriction(); movement(); callbacksAndSampling();
    std::cout<<"DRAW_RESTRICTION_TOPDECK_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
