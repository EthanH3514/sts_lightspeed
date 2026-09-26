#include <algorithm>
#include <array>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "corruption_cost_watch.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char* msg){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game; BattleContext b;
    explicit Fixture(RelicId relic=RelicId::INVALID,MonsterEncounter encounter=MonsterEncounter::CULTIST):game(CharacterClass::IRONCLAD,123456789,0){
        game.floorNum=1;game.relics={};if(relic!=RelicId::INVALID)game.relics.add({relic});
        game.enterBattle(encounter);b.init(game);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=3;
        b.player.curHp=70;b.player.maxHp=80;for(int i=0;i<b.monsters.monsterCount;++i)b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
    }
    int top(CardId id,bool up=false){CardInstance c(id,up);c.setUniqueId(b.cards.nextUniqueCardId++);b.cards.drawPile.push_back(c);return c.uniqueId;}
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void play(CardId id=CardId::HAVOC,bool up=false){b.cards.createTempCardInHand(CardInstance(id,up));auto c=b.cards.hand[b.cards.cardsInHand-1];b.addToBotCard(CardQueueItem(c,0,b.player.energy));run();}
};
int count(const std::vector<CardInstance>&pile,CardId id){return std::count_if(pile.begin(),pile.end(),[&](const auto& c){return c.id==id;});}
void ordinary(){
    for(bool up:{false,true}){
        Fixture f;int uid=f.top(CardId::BLUDGEON);f.play(CardId::HAVOC,up);
        check(f.b.player.energy==(up?3:2),"only Havoc pays; expensive top card is free");
        check(f.b.monsters.arr[0].curHp==968,"top attack executes full damage");
        check(f.b.cards.exhaustPile.size()==1&&f.b.cards.exhaustPile[0].uniqueId==uid,"top card exhausts with original identity");
        check(count(f.b.cards.discardPile,CardId::HAVOC)==1,"Havoc itself does not inherently exhaust");
        check(f.b.player.cardsPlayedThisTurn==2&&f.b.player.attacksPlayedThisTurn==1,"both successful plays are counted");
        Fixture block;block.top(CardId::DEFEND_RED,up);block.b.player.buff<PS::NO_DRAW>();
        for(int i=0;i<9;++i)block.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
        block.play(CardId::HAVOC,up);
        check(block.b.player.block==(up?8:5)&&block.b.cards.cardsInHand==9,"autoplay bypasses hand capacity and No Draw");
        Fixture power;power.top(CardId::INFLAME,up);power.b.player.buff<PS::FEEL_NO_PAIN>(3);power.play(CardId::HAVOC,up);
        check(power.b.player.getStatus<PS::STRENGTH>()==(up?3:2),"top Power executes");
        check(power.b.cards.exhaustPile.empty()&&power.b.player.block==0,"Power removal is not an exhaust event");
        Fixture x;x.top(CardId::WHIRLWIND,up);x.play(CardId::HAVOC,up);
        check(x.b.player.energy==(up?3:2),"top X card never consumes energy");
        check(1000-x.b.monsters.arr[0].curHp==(up?3:2)*(up?8:5),"top X uses energy after paying Havoc");
        Fixture empty;empty.play(CardId::HAVOC,up);
        check(empty.b.player.cardsPlayedThisTurn==1&&empty.b.cards.exhaustPile.empty(),"empty piles cause no phantom play");
        Fixture shuffle;shuffle.top(CardId::STRIKE_RED);shuffle.b.cards.discardPile=shuffle.b.cards.drawPile;shuffle.b.cards.drawPile.clear();shuffle.play(CardId::HAVOC,up);
        check(shuffle.b.monsters.arr[0].curHp==994&&count(shuffle.b.cards.exhaustPile,CardId::STRIKE_RED)==1,"empty draw pile shuffles discard then autoplays");
    }
}
void rejected(){
    for(CardId id:{CardId::WOUND,CardId::BURN,CardId::DAZED,CardId::REGRET,CardId::CLASH}){
        Fixture f;f.top(id);f.b.player.buff<PS::FEEL_NO_PAIN>(3);f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));f.play();
        check(count(f.b.cards.exhaustPile,id)==1,"unplayable autoplay card still settles into exhaust");
        check(f.b.player.block==3,"failed autoplay still triggers exhaust payoff");
        check(f.b.player.cardsPlayedThisTurn==1&&f.b.player.attacksPlayedThisTurn==0,"rejected autoplay is not a played card");
        check(f.b.player.curHp==70,"rejected status/curse does not execute end-turn damage");
    }
    Fixture entangle;entangle.top(CardId::STRIKE_RED);entangle.b.player.buff<PS::ENTANGLED>();entangle.play();
    check(count(entangle.b.cards.exhaustPile,CardId::STRIKE_RED)==1&&entangle.b.monsters.arr[0].curHp==1000,"Entangled blocks effect but not lifecycle cleanup");
    Fixture choker(RelicId::VELVET_CHOKER);choker.top(CardId::STRIKE_RED);choker.b.player.cardsPlayedThisTurn=5;choker.b.player.buff<PS::FEEL_NO_PAIN>(3);choker.play();
    check(choker.b.player.cardsPlayedThisTurn==6&&choker.b.player.attacksPlayedThisTurn==0,"Choker blocks the seventh autoplay");
    check(choker.b.player.block==3&&count(choker.b.cards.exhaustPile,CardId::STRIKE_RED)==1,"Choker-rejected top card is not lost");
    Fixture noExhaust;noExhaust.top(CardId::WOUND);noExhaust.b.playTopCardInDrawPile(0,false);noExhaust.run();
    check(count(noExhaust.b.cards.discardPile,CardId::WOUND)==1&&noExhaust.b.cards.exhaustPile.empty(),"non-exhausting rejected autoplay settles to discard");
    Fixture manual;manual.b.cards.createTempCardInHand(CardInstance(CardId::WOUND));manual.b.addToBotCard(CardQueueItem(manual.b.cards.hand[0],0,3));manual.run();
    check(manual.b.cards.cardsInHand==1&&manual.b.cards.exhaustPile.empty(),"rejected manual play is not consumed by autoplay cleanup");
    Fixture noDraw;noDraw.top(CardId::DEFEND_RED);noDraw.top(CardId::WOUND);noDraw.b.player.buff<PS::EVOLVE>(2);noDraw.b.player.buff<PS::FIRE_BREATHING>(6);noDraw.play();
    check(noDraw.b.cards.cardsInHand==0&&noDraw.b.monsters.arr[0].curHp==1000,"top removal is not a status draw for Evolve/Fire Breathing");
}
void interactions(){
    Fixture f;f.top(CardId::DEFEND_RED);f.top(CardId::SENTINEL);f.b.player.buff<PS::FEEL_NO_PAIN>(3);f.b.player.buff<PS::DARK_EMBRACE>(1);f.b.player.buff<PS::EVOLVE>(1);f.play();
    check(f.b.player.energy==4&&f.b.player.block==8,"Sentinel plays block then exhaustion grants energy and block");
    check(f.b.cards.cardsInHand==1&&f.b.cards.hand[0].id==CardId::DEFEND_RED,"exhaust triggers Dark Embrace draw");
    Fixture draw;for(int i=0;i<4;++i)draw.top(CardId::STRIKE_RED);draw.top(CardId::POMMEL_STRIKE,true);draw.b.player.buff<PS::DOUBLE_TAP>(1);draw.play();
    check(draw.b.cards.cardsInHand==4&&draw.b.monsters.arr[0].curHp==980,"auto Attack triggers Double Tap and both draws");
    check(count(draw.b.cards.exhaustPile,CardId::POMMEL_STRIKE)==1,"purged repeat does not duplicate exhaustion");
    Fixture thorns;thorns.top(CardId::STRIKE_RED);thorns.b.monsters.arr[0].buff<MS::THORNS>(3);thorns.play();
    check(thorns.b.player.curHp==67,"successful automatic attack triggers Thorns");
    Fixture corruption;corruption.top(CardId::DEFEND_RED);corruption.b.player.buff<PS::CORRUPTION>();corruption.b.player.buff<PS::FEEL_NO_PAIN>(3);corruption.play();
    check(corruption.b.player.energy==3&&corruption.b.player.block==11&&corruption.b.cards.exhaustPile.size()==2,"Corruption exhausts Havoc plus its top Skill exactly once each");
    check(corruption_cost_watch::differences(corruption.b.cards).empty(),"autoplay does not cross deferred Corruption cost boundary");
    Fixture chain;chain.top(CardId::STRIKE_RED);for(int i=0;i<12;++i)chain.top(CardId::HAVOC,true);chain.play();
    check(chain.b.player.cardsPlayedThisTurn==14&&chain.b.cards.exhaustPile.size()==13,"nested top autoplay resolves as a queue");
    check(chain.b.actionQueue.isEmpty()&&chain.b.cardQueue.isEmpty(),"nested queue returns resolved control");
    Fixture selection;selection.top(CardId::TRUE_GRIT,true);selection.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));selection.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));selection.play();
    check(selection.b.inputState==InputState::CARD_SELECT,"autoplay may suspend for a real public selection");
    bool rejected=false;try{public_sampling::resampleCombatContinuation(selection.game,selection.b,{11,22,33,44,55,66,77});}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"sampler never resamples a suspended autoplay queue");
}
void roots(){
    for(bool frozen:{false,true})for(bool known:{false,true})for(int seed=1;seed<=12;++seed){
        Fixture f(frozen?RelicId::FROZEN_EYE:RelicId::INVALID);
        for(int i=0;i<8;++i)f.top(i%2?CardId::DEFEND_RED:CardId::STRIKE_RED);
        int uid=f.top(CardId::BASH);auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),known?twin.cards.drawPile.end()-1:twin.cards.drawPile.end());
        twin.cardRandomRng=Random(999999);twin.shuffleRng=Random(333333);
        std::array<std::uint64_t,7> seeds={11,static_cast<std::uint64_t>(seed),33,44,55,66,static_cast<std::uint64_t>(seed)};
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds,known?uid:-1);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds,known?uid:-1);
        if(known||frozen)check(f.b.cards.drawPile.back().uniqueId==uid,"public known/Frozen Eye top is retained");
        f.play();twin.cards.createTempCardInHand(CardInstance(CardId::HAVOC));auto c=twin.cards.hand[twin.cards.cardsInHand-1];twin.addToBotCard(CardQueueItem(c,0,twin.player.energy));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        check(f.b.player.block==twin.player.block&&f.b.monsters.arr[0].curHp==twin.monsters.arr[0].curHp,"paired autoplay outcome ignores old hidden order/RNG");
        check(f.b.cards.exhaustPile[0].uniqueId==twin.cards.exhaustPile[0].uniqueId,"paired autoplay consumes same sampled identity");
        const int played=f.b.cards.exhaustPile[0].uniqueId;public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.cards.exhaustPile[0].uniqueId==played,"resolved post-autoplay root retains exhausted identity");
    }
    bool seen[2]={false,false};
    for(int seed=1;seed<=32;++seed){
        Fixture f(RelicId::INVALID,MonsterEncounter::SMALL_SLIMES);f.top(CardId::DEFEND_RED);const int top=f.top(CardId::BASH);
        auto twin=f.b;twin.cardRandomRng=Random(999999);
        std::array<std::uint64_t,7> seeds={11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds,top);public_sampling::resampleCombatContinuation(f.game,twin,seeds,top);
        f.play();twin.cards.createTempCardInHand(CardInstance(CardId::HAVOC));twin.addToBotCard(CardQueueItem(twin.cards.hand[0],0,3));twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        int damaged=0;for(int i=0;i<2;++i){seen[i]|=f.b.monsters.arr[i].curHp<1000;damaged+=1000-f.b.monsters.arr[i].curHp;
            check(f.b.monsters.arr[i].curHp==twin.monsters.arr[i].curHp,"paired random targets ignore original card RNG");}
        check(damaged==8,"targeted top attack damages exactly one live enemy");
    }
    check(seen[0]&&seen[1],"different sampled worlds can select either live target");
}
}
int main(){ordinary();rejected();interactions();roots();std::cout<<"TOP_CARD_AUTOPLAY_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
