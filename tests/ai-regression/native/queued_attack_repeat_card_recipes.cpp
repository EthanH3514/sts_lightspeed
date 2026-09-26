#include <algorithm>
#include <array>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "corruption_cost_watch.h"
using namespace sts;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* msg){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game;BattleContext b;
    Fixture(RelicId relic=RelicId::INVALID,bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0){
        game.floorNum=1;game.relics={};if(relic!=RelicId::INVALID)game.relics.add({relic});if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST);b.init(game);b.executeActions();b.cards=CardManager{};b.cards.nextUniqueCardId=100;
        b.player.energy=20;b.player.curHp=70;b.player.maxHp=80;b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(int i=0;i<10;++i)b.cards.createTempCardInDrawPile(0,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
    }
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    int play(CardId id,bool up=false){b.cards.createTempCardInHand(CardInstance(id,up));auto c=b.cards.hand[b.cards.cardsInHand-1];b.addToBotCard(CardQueueItem(c,0,b.player.energy));run();return c.uniqueId;}
};
int pileCount(const BattleContext &b,CardId id){int n=0;for(auto c:b.cards.discardPile)n+=c.id==id;for(auto c:b.cards.exhaustPile)n+=c.id==id;return n;}
void variants(){
    for(bool up:{false,true}){
        Fixture f;f.play(CardId::DOUBLE_TAP,up);check(f.b.player.getStatus<PS::DOUBLE_TAP>()==(up?2:1),"base/plus repeat the next one/two attacks");
        f.play(CardId::DEFEND_RED);f.play(CardId::INFLAME);
        check(f.b.player.getStatus<PS::DOUBLE_TAP>()==(up?2:1),"Skills and Powers do not consume attack-repeat charges");
        for(int i=0;i<3;++i){int hp=f.b.monsters.arr[0].curHp;int energy=f.b.player.energy;f.play(CardId::STRIKE_RED);
            const int repeats=i<(up?2:1)?2:1;check(hp-f.b.monsters.arr[0].curHp==8*repeats,"one additional play per eligible attack, not recursive repeats");
            check(f.b.player.energy==energy-1,"normal attack pays once");}
        check(pileCount(f.b,CardId::STRIKE_RED)==3,"purged replays do not add permanent pile instances");
        f.play(CardId::DOUBLE_TAP,up);f.play(CardId::DOUBLE_TAP,false);
        check(f.b.player.getStatus<PS::DOUBLE_TAP>()==(up?3:2),"multiple Double Taps add charges");
        f.b.endTurn();f.run();check(!f.b.player.hasStatus<PS::DOUBLE_TAP>(),"unused charges expire at end of turn");
        check(corruption_cost_watch::differences(f.b.cards).empty(),"repeat path avoids deferred Corruption cost boundary");
    }
}
void interactions(){
    for(bool autoplay:{false,true})for(bool up:{false,true}){
        Fixture f;f.b.player.energy=3;CardInstance c(CardId::WHIRLWIND,up);f.b.cards.createTempCardInHand(c);
        auto item=CardQueueItem(f.b.cards.hand[0],0,3);item.autoplay=autoplay;f.b.addToBotCard(item);f.run();
        check(f.b.player.energy==(autoplay?3:0),"autoplay X card is free, manual X card still consumes energy");
        check(1000-f.b.monsters.arr[0].curHp==3*(up?8:5),"autoplay preserves X effect amount, not zero-X effect");
    }
    for(bool up:{false,true}){
        Fixture ramp;ramp.play(CardId::DOUBLE_TAP);const int uid=ramp.play(CardId::RAMPAGE,up);
        check(1000-ramp.b.monsters.arr[0].curHp==16+(up?8:5),"same-UUID Rampage replay sees first growth");
        auto it=std::find_if(ramp.b.cards.discardPile.begin(),ramp.b.cards.discardPile.end(),[&](auto c){return c.uniqueId==uid;});
        check(it!=ramp.b.cards.discardPile.end()&&it->specialData==(up?16:10),"both Rampage plays grow the original identity");
        Fixture corruption;corruption.b.player.buff<PS::CORRUPTION>();corruption.b.player.buff<PS::FEEL_NO_PAIN>(3);
        corruption.play(CardId::DOUBLE_TAP,up);
        check(corruption.b.player.energy==20&&corruption.b.player.block==3&&corruption.b.cards.exhaustPile.size()==1,"Corruption makes setup free and exhausts it once");
        check(corruption.b.player.getStatus<PS::DOUBLE_TAP>()==(up?2:1),"setup exhaustion does not remove repeat charges");
        Fixture thorns;thorns.b.monsters.arr[0].buff<MS::THORNS>(3);thorns.play(CardId::DOUBLE_TAP);thorns.play(CardId::STRIKE_RED,up);
        check(thorns.b.player.curHp==64,"replayed attack triggers Thorns again");
        Fixture pummel;pummel.b.player.buff<PS::RAGE>(3);pummel.b.player.buff<PS::FEEL_NO_PAIN>(4);pummel.play(CardId::DOUBLE_TAP);pummel.play(CardId::PUMMEL,up);
        check(1000-pummel.b.monsters.arr[0].curHp==(up?20:16),"repeat is a whole multi-hit card play");
        check(pummel.b.player.block==10,"Rage triggers twice, source exhaust triggers Feel No Pain once");
        check(pummel.b.cards.exhaustPile.size()==1,"purged repeat does not exhaust twice");
        Fixture bash;bash.play(CardId::DOUBLE_TAP);bash.play(CardId::BASH,up);
        check(1000-bash.b.monsters.arr[0].curHp==(up?25:20),"repeat recalculates Vulnerable from first Bash");
        Fixture draw;draw.play(CardId::DOUBLE_TAP);draw.play(CardId::POMMEL_STRIKE,up);
        check(draw.b.cards.cardsInHand==(up?4:2),"repeat executes draw effects twice");
        Fixture fire;fire.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));fire.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));fire.play(CardId::DOUBLE_TAP);fire.play(CardId::FIEND_FIRE,up);
        check(1000-fire.b.monsters.arr[0].curHp==(up?20:14),"Fiend Fire recounts empty hand on repeated play");
        check(fire.b.cards.exhaustPile.size()==3,"only real hand cards and original Fiend Fire exhaust");
        Fixture x;x.b.player.energy=4;x.play(CardId::DOUBLE_TAP);x.play(CardId::WHIRLWIND,up);
        check(1000-x.b.monsters.arr[0].curHp==6*(up?8:5),"X replay retains original energyOnUse");
        check(x.b.player.energy==0,"ordinary X cost consumed once");
        // Reachable public relic counter: original X attack triggers Nunchaku after spending its energy.
        // Its free replay must retain that newly gained energy, not spend it again.
        Fixture nunchaku(RelicId::NUNCHAKU);nunchaku.b.player.nunchakuCounter=9;nunchaku.b.player.energy=4;
        nunchaku.play(CardId::DOUBLE_TAP);nunchaku.play(CardId::WHIRLWIND,up);
        check(nunchaku.b.player.energy==1,"free X replay preserves Nunchaku energy gained after original attack");
        check(nunchaku.b.player.nunchakuCounter==1,"both original and replay trigger attack counter");
        Fixture kill;kill.b.monsters.arr[0].curHp=5;kill.play(CardId::DOUBLE_TAP);kill.play(CardId::STRIKE_RED,up);
        check(kill.b.player.attacksPlayedThisTurn==1,"lethal original does not fabricate another live-target play");
    }
}
void roots(){
    const std::array<std::uint64_t,7> seeds={11,22,33,44,55,66,77};
    for(bool up:{false,true})for(bool frozen:{false,true}){
        Fixture f(RelicId::INVALID,frozen);f.play(CardId::DOUBLE_TAP,up);auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());twin.cardRandomRng=Random(999);
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        check(f.b.player.getStatus<PS::DOUBLE_TAP>()==(up?2:1),"resolved root preserves public charge count");
        f.play(CardId::POMMEL_STRIKE);twin.cards.createTempCardInHand(CardInstance(CardId::POMMEL_STRIKE));auto c=twin.cards.hand[twin.cards.cardsInHand-1];twin.addToBotCard(CardQueueItem(c,0,twin.player.energy));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        check(f.b.player.energy==twin.player.energy&&f.b.monsters.arr[0].curHp==twin.monsters.arr[0].curHp,"paired repeat outcomes independent of old hidden order/RNG");
        for(int i=0;i<f.b.cards.cardsInHand;++i)check(f.b.cards.hand[i].uniqueId==twin.cards.hand[i].uniqueId,"paired repeated draws have identical public identities");
        auto before=f.b;public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.player.getStatus<PS::DOUBLE_TAP>()==before.player.getStatus<PS::DOUBLE_TAP>(),"post-repeat root preserves remaining charge count");
        if(frozen)for(size_t i=0;i<f.b.cards.drawPile.size();++i)check(f.b.cards.drawPile[i].uniqueId==before.cards.drawPile[i].uniqueId,"Frozen Eye order retained");
    }
}
}
int main(){variants();interactions();roots();std::cout<<"QUEUED_ATTACK_REPEAT_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
