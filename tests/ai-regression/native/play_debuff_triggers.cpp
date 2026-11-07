#include <algorithm>
#include <array>
#include <iostream>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) { ++checks; if(!ok && ++failures<=20) std::cerr<<message<<'\n'; }
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(bool frozen=false,RelicId relic=RelicId::INVALID,bool multi=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(multi?MonsterEncounter::SMALL_SLIMES:MonsterEncounter::CULTIST);
        b.init(game); b.executeActions(); b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=100; b.player.curHp=b.player.maxHp=200;
        for(int i=0;i<b.monsters.monsterCount;++i) b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
        for(int i=0;i<12;++i) b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
    }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(CardId id,bool up=false) {
        b.cards.createTempCardInHand(CardInstance(id,up)); auto c=b.cards.hand[b.cards.cardsInHand-1];
        check(c.canUse(b,0,false),"fixture unplayable card");
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); drain();
    }
};
void panache() {
    for(bool up:{false,true}) {
        Fixture f; f.play(CardId::PANACHE,up);
        const int damage=up?14:10;
        check(f.b.player.panacheCounter==5,"Panache initial counter must be five");
        check(f.b.player.energy==100 && f.b.cards.discardPile.empty() && f.b.cards.exhaustPile.empty(),"power lifecycle/cost");
        for(int i=1;i<=11;++i) {
            f.play(CardId::RAGE);
            check(f.b.player.panacheCounter==5-i%5,"Panache counter resets every fifth play");
            check(f.b.monsters.arr[0].curHp==1000-i/5*damage,"Panache only fifth/tenth play deals damage");
        }
        search::Action(search::ActionType::END_TURN).execute(f.b);
        check(f.b.player.panacheCounter==5,"Panache turn reset");
        Fixture stack; stack.play(CardId::PANACHE); stack.play(CardId::RAGE); stack.play(CardId::PANACHE,up);
        check(stack.b.player.panacheCounter==3,"stacking power play counts but must not reset");
        check(stack.b.player.getStatus<PS::PANACHE>()==10+damage,"Panache stacked damage");
    }
    // Every on-use path must reset, not just skills. Multi-hit counts once.
    for(auto id:{CardId::STRIKE_RED,CardId::RAGE,CardId::INFLAME,CardId::SLIMED}) {
        Fixture f; f.play(CardId::PANACHE); f.b.player.panacheCounter=1; f.play(id);
        check(f.b.player.panacheCounter==5,"all on-use paths reset counter");
        check(f.b.monsters.arr[0].curHp==(id==CardId::STRIKE_RED?984:990),"all on-use paths trigger damage once");
    }
    Fixture repeat; repeat.play(CardId::PANACHE); repeat.b.player.panacheCounter=3;
    repeat.play(CardId::DOUBLE_TAP); repeat.play(CardId::TWIN_STRIKE);
    check(repeat.b.player.panacheCounter==5,"Double Tap copy counts once, not per hit");
    check(repeat.b.monsters.arr[0].curHp==970,"Double Tap multi-hit + Panache damage");
    for(bool intangible:{false,true}) {
        Fixture f(false,RelicId::INVALID,true); f.play(CardId::PANACHE,true);
        f.b.player.strength=50; f.b.player.debuff<PS::WEAK>(3,false);
        for(int i=0;i<2;++i) {f.b.monsters.arr[i].addDebuff<MS::VULNERABLE>(3,false); f.b.monsters.arr[i].block=2;
            if(intangible) f.b.monsters.arr[i].buff<MS::INTANGIBLE>(1);}
        f.b.player.panacheCounter=1; f.play(CardId::RAGE);
        for(int i=0;i<2;++i) check(f.b.monsters.arr[i].curHp==1000-(intangible?0:12),"Panache area non-attack modifiers/block");
    }
}
void sadistic() {
    for(bool up:{false,true}) for(int artifact:{0,1,2}) for(bool multi:{false,true}) {
        Fixture f(false,RelicId::INVALID,multi); f.play(CardId::SADISTIC_NATURE,up);
        const int amount=up?7:5;
        check(f.b.player.getStatus<PS::SADISTIC>()==amount && f.b.player.energy==100,"Sadistic registration");
        check(f.b.cards.exhaustPile.empty() && f.b.cards.discardPile.empty(),"Sadistic power lifecycle");
        f.b.player.strength=50; f.b.player.debuff<PS::WEAK>(3,false);
        if(artifact) for(int i=0;i<f.b.monsters.monsterCount;++i) f.b.monsters.arr[i].buff<MS::ARTIFACT>(artifact);
        f.play(CardId::SHOCKWAVE,up);
        for(int i=0;i<f.b.monsters.monsterCount;++i) check(f.b.monsters.arr[i].curHp==1000-(2-artifact)*amount,"Sadistic per successful packet, not per stack, ignoring attack modifiers");
        auto hp=f.b.monsters.arr[0].curHp; f.play(CardId::BLIND);
        check(f.b.monsters.arr[0].curHp==hp-amount,"reapplying debuff triggers");
    }
    for(auto id:{CardId::DISARM,CardId::DARK_SHACKLES}) {
        Fixture f; f.play(CardId::SADISTIC_NATURE); f.play(id);
        check(f.b.monsters.arr[0].curHp==995,"negative Strength triggers once, no Shackled restoration trigger");
    }
    Fixture belt(false,RelicId::CHAMPION_BELT); belt.play(CardId::SADISTIC_NATURE); belt.play(CardId::TRIP);
    check(belt.b.monsters.arr[0].curHp==990,"Champion Belt separate Weak packet triggers");
    Fixture self; self.play(CardId::SADISTIC_NATURE); self.play(CardId::BERSERK);
    check(self.b.monsters.arr[0].curHp==1000,"self debuff is not enemy trigger");
    Fixture stack; stack.play(CardId::SADISTIC_NATURE); stack.play(CardId::SADISTIC_NATURE,true); stack.play(CardId::TRIP);
    check(stack.b.monsters.arr[0].curHp==988,"Sadistic stacks additively");
    for(bool intangible:{false,true}) {
        Fixture f; f.play(CardId::SADISTIC_NATURE); f.b.monsters.arr[0].block=2;
        if(intangible) f.b.monsters.arr[0].buff<MS::INTANGIBLE>(1);
        if(!intangible) f.b.monsters.arr[0].buff<MS::THORNS>(3);
        f.play(CardId::TRIP);
        check(f.b.monsters.arr[0].curHp==1000-(intangible?0:3),"Sadistic block/intangible");
        check(f.b.player.curHp==200,"Sadistic is non-attack, no Thorns retaliation");
    }
}
void sampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(bool frozen:{false,true}) for(bool up:{false,true}) for(int remaining:{1,2,3,4,5}) {
        Fixture f(frozen); f.play(CardId::PANACHE,up); f.play(CardId::SADISTIC_NATURE,up);
        f.b.player.panacheCounter=remaining; auto twin=f.b;
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<12;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"paired hidden order leak");
        check(f.b.player.panacheCounter==remaining,"sampling changed visible counter");
        f.play(CardId::TRIP); Fixture other; other.b=twin; other.play(CardId::TRIP);
        check(f.b.player.panacheCounter==other.b.player.panacheCounter && f.b.monsters.arr[0].curHp==other.b.monsters.arr[0].curHp,"paired play/debuff effects");
        auto counter=f.b.player.panacheCounter;
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.player.panacheCounter==counter,"post-play sampling changed counter");
    }
}
}
int main() {panache(); sadistic(); sampling(); std::cout<<"PLAY_DEBUFF_TRIGGERS "<<failures<<" failures / "<<checks<<" checks\n"; return failures?1:0;}
