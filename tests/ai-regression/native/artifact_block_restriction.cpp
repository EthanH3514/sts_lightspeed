#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0;
void check(bool ok,const char *message) { ++checks; if(!ok) throw std::runtime_error(message); }
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100; b.player.energy=20;
        b.player.curHp=b.player.maxHp=200;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(int i=0;i<15;++i) {
            CardInstance c(i%2?CardId::DEFEND_RED:CardId::STRIKE_RED);
            c.setUniqueId(b.cards.nextUniqueCardId++); b.cards.drawPile.push_back(c);
        }
    }
    void execute() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(CardId id,bool up=false) {
        CardInstance c(id,up); c.setUniqueId(b.cards.nextUniqueCardId++);
        check(b.cards.cardsInHand<10,"fixture full hand");
        b.cards.hand[b.cards.cardsInHand++]=c;
        check(c.canUse(b,0,false),"card not playable");
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); execute();
    }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); }
};
void matrix() {
    for(bool up:{false,true}) for(int artifact:{0,1,2}) for(int dex:{-40,-3,0,7})
    for(bool frail:{false,true}) for(int restricted:{0,1,2}) {
        Fixture f; f.b.player.dexterity=dex;
        if(frail) f.b.player.debuff<PS::FRAIL>(3,false);
        if(restricted) f.b.player.debuff<PS::NO_BLOCK>(restricted,false);
        if(artifact) f.b.player.buff<PS::ARTIFACT>(artifact);
        f.play(CardId::PANIC_BUTTON,up);
        int block=restricted?0:std::max(0,(up?40:30)+dex);
        if(frail) block=block*3/4;
        check(f.b.player.block==block,"Panic block/modifier order");
        check(f.b.player.getStatus<PS::ARTIFACT>()==std::max(0,artifact-1),"Artifact consumes one packet");
        check(f.b.player.getStatus<PS::NO_BLOCK>()==restricted+(artifact?0:2),"No Block stacking/protection");
        check(f.b.player.energy==20 && f.b.cards.exhaustPile.size()==1,"zero cost/exhaust lifecycle");
        f.play(CardId::DEFEND_RED);
        int defend=(restricted||!artifact)?0:std::max(0,5+dex);
        if(frail) defend=defend*3/4;
        check(f.b.player.block==block+defend,"subsequent card block");
    }
}
void chargesAndTiming() {
    for(bool up:{false,true}) {
        Fixture f; f.play(CardId::PANACEA,up); f.play(CardId::PANACEA,up);
        check(f.b.player.getStatus<PS::ARTIFACT>()==(up?4:2),"Panacea stacking");
        check(f.b.player.energy==20 && f.b.cards.exhaustPile.size()==2,"Panacea cost/exhaust");
        for(int n=0;n<(up?4:2);++n) {
            f.b.player.debuff<PS::WEAK>(3,false);
            check(!f.b.player.hasStatus<PS::WEAK>(),"one charge blocks whole debuff");
        }
        f.b.player.debuff<PS::WEAK>(3,false);
        check(f.b.player.getStatus<PS::WEAK>()==3,"debuff after charges spent");
        Fixture t; t.play(CardId::PANIC_BUTTON,up);
        check(t.b.player.getStatus<PS::NO_BLOCK>()==2,"initial duration");
        t.end(); check(t.b.player.getStatus<PS::NO_BLOCK>()==1,"first round duration");
        t.play(CardId::DEFEND_RED); check(t.b.player.block==0,"next player turn restricted");
        t.end(); check(!t.b.player.hasStatus<PS::NO_BLOCK>(),"second round expiration");
        t.play(CardId::DEFEND_RED); check(t.b.player.block==5,"block restored");
        Fixture late; late.play(CardId::PANIC_BUTTON); late.play(CardId::PANACEA,up);
        check(late.b.player.getStatus<PS::NO_BLOCK>()==2,"Artifact must not cleanse existing debuff");
        Fixture combo; combo.play(CardId::PANACEA,up); combo.play(CardId::PANIC_BUTTON);
        check(!combo.b.player.hasStatus<PS::NO_BLOCK>(),"Panacea then Panic protection");
        combo.play(CardId::DEFEND_RED); check(combo.b.player.block==35,"protected follow-up block");
    }
}
void nonCardSources() {
    Fixture f; f.b.player.buff<PS::FEEL_NO_PAIN>(3); f.b.player.buff<PS::JUGGERNAUT>(5);
    f.play(CardId::PANIC_BUTTON);
    check(f.b.player.block==33,"No Block must not suppress exhaust block");
    check(f.b.monsters.arr[0].curHp==990,"two block callbacks");
    f.play(CardId::ENTRENCH);
    check(f.b.player.block==66,"Entrench bypasses card block modifiers");
    f.play(CardId::RAGE); f.play(CardId::STRIKE_RED);
    check(f.b.player.block==69,"Rage bypasses No Block");
    f.play(CardId::BARRICADE); f.play(CardId::METALLICIZE); f.end();
    check(f.b.player.block==72,"Metallicize bypasses No Block");
    Fixture stack; stack.play(CardId::PANIC_BUTTON); stack.play(CardId::PANIC_BUTTON,true);
    check(stack.b.player.block==30 && stack.b.player.getStatus<PS::NO_BLOCK>()==4,"second Panic gives zero and extends duration");
}
void sampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::PANACEA,CardId::PANIC_BUTTON}) for(bool up:{false,true})
    for(bool frozen:{false,true}) for(int artifact:{0,1}) for(int restriction:{0,1}) {
        Fixture f(frozen);
        if(restriction) f.b.player.debuff<PS::NO_BLOCK>(restriction,false);
        if(artifact) f.b.player.buff<PS::ARTIFACT>(artifact);
        auto twin=f.b;
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<15;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden order leak");
        Fixture other(frozen); other.b=twin; f.play(id,up); other.play(id,up);
        check(f.b.player.block==other.b.player.block && f.b.player.getStatus<PS::ARTIFACT>()==other.b.player.getStatus<PS::ARTIFACT>() && f.b.player.getStatus<PS::NO_BLOCK>()==other.b.player.getStatus<PS::NO_BLOCK>(),"paired effects differ");
        const int block=f.b.player.block, art=f.b.player.getStatus<PS::ARTIFACT>(), noBlock=f.b.player.getStatus<PS::NO_BLOCK>();
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.player.block==block && f.b.player.getStatus<PS::ARTIFACT>()==art && f.b.player.getStatus<PS::NO_BLOCK>()==noBlock,"resampling mutated public power");
        if(frozen) for(int i=0;i<15;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"Frozen Eye changed");
    }
}
}
int main() {
    try { matrix(); chargesAndTiming(); nonCardSources(); sampling();
        std::cout<<"ARTIFACT_BLOCK_RESTRICTION_OK "<<checks<<" checks\n";
    } catch(const std::exception &e) {std::cerr<<"FAILED after "<<checks<<" checks: "<<e.what()<<'\n'; return 1;}
}
