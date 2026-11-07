#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include "combat/BattleContext.h"
#include "constants/CardPools.h"
#include "game/Game.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *m){++checks;if(!ok){++failures;if(failures<=12)std::cerr<<"FAIL: "<<m<<'\n';}}
struct Fixture {
    GameContext g; BattleContext b;
    Fixture():g(CharacterClass::IRONCLAD,123456789,0){
        g.floorNum=1;g.relics={};g.enterBattle(MonsterEncounter::CULTIST);b.init(g);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=3;
        b.player.curHp=70;b.player.maxHp=80;b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
    }
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void play(CardId id,bool up){b.cards.createTempCardInHand(CardInstance(id,up));b.addToBotCard(CardQueueItem(b.cards.hand[b.cards.cardsInHand-1],0,b.player.energy));run();}
};
void variants(){
    std::set<CardId> attacks, colorless;
    bool sawX=false;
    for(CardId id:{CardId::INFERNAL_BLADE,CardId::JACK_OF_ALL_TRADES})
    for(bool up:{false,true})for(int occupied:{0,9})for(int seed=1;seed<=512;++seed){
        Fixture f;f.b.cardRandomRng=Random(seed);
        auto expectedRng=f.b.cardRandomRng;
        const int count=id==CardId::JACK_OF_ALL_TRADES&&up?2:1;
        std::vector<CardId> expected;
        for(int i=0;i<count;++i)expected.push_back(id==CardId::INFERNAL_BLADE
            ?getTrulyRandomCardInCombat(expectedRng,CharacterClass::IRONCLAD,CardType::ATTACK)
            :getTrulyRandomColorlessCardInCombat(expectedRng));
        for(int i=0;i<occupied;++i)f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        f.b.player.buff<PS::NO_DRAW>();f.b.player.buff<PS::FEEL_NO_PAIN>(3);
        f.play(id,up);
        check(f.b.player.energy==(id==CardId::INFERNAL_BLADE&&!up?2:3),"generator pays only its own cost");
        check(f.b.player.block==3&&f.b.cards.exhaustPile.size()==1&&f.b.cards.exhaustPile[0].id==id,"generator exhausts with payoff");
        check(f.b.player.cardsPlayedThisTurn==1&&f.b.player.attacksPlayedThisTurn==0,"generation is not play");
        check(f.b.cards.cardsInHand==std::min(10,occupied+count),"No Draw does not prevent generation");
        check(f.b.cards.discardPile.size()==static_cast<unsigned>(std::max(0,occupied+count-10)),"overflow goes to discard");
        for(int i=0;i<count;++i){
            const auto &c=occupied+i<10?f.b.cards.hand[occupied+i]:f.b.cards.discardPile[occupied+i-10];
            check(c.id==expected[i],"generated cards retain selection order even at hand capacity");
            check(c.uniqueId==100+occupied+1+i,"generated cards have fresh sequential identities");
            check(!c.upgraded,"upgraded generator still creates base cards");
            CardInstance base(expected[i]);
            check(c.cost==base.cost,"base combat cost retained");
            check(c.costForTurn==(id==CardId::INFERNAL_BLADE&&base.cost>=0?0:base.cost),"only Infernal Blade sets turn zero; X remains X");
            if(id==CardId::INFERNAL_BLADE){
                attacks.insert(c.id);sawX|=c.isXCost();
                check(c.getType()==CardType::ATTACK&&c.id!=CardId::FEED&&c.id!=CardId::REAPER&&c.id!=CardId::STRIKE_RED&&c.id!=CardId::BASH,"attack pool excludes starters and healing-tagged cards");
            }else{
                colorless.insert(c.id);check(c.id!=CardId::BANDAGE_UP,"colorless pool excludes healing");
            }
        }
        check(f.b.cardRandomRng.counter==expectedRng.counter,"one pool sample per generated card");
        f.b.cards.resetAttributesAtEndOfTurn();
        for(int i=0;i<count;++i){
            const auto &c=occupied+i<10?f.b.cards.hand[occupied+i]:f.b.cards.discardPile[occupied+i-10];
            check(c.costForTurn==c.cost,"temporary zero resets in hand and discard");
        }
    }
    check(attacks.size()==28&&sawX,"fixed seed corpus reaches all 28 attacks including Whirlwind");
    check(colorless.size()==34,"fixed seed corpus reaches all 34 non-healing colorless cards");
}
void timing(){
    for(CardId id:{CardId::INFERNAL_BLADE,CardId::JACK_OF_ALL_TRADES})for(bool up:{false,true}){
        Fixture f;f.b.cardRandomRng=Random(42);
        Random expected=f.b.cardRandomRng;
        int count=id==CardId::JACK_OF_ALL_TRADES&&up?2:1;
        std::vector<CardId> cards;
        for(int i=0;i<count;++i)cards.push_back(id==CardId::INFERNAL_BLADE
            ?getTrulyRandomCardInCombat(expected,CharacterClass::IRONCLAD,CardType::ATTACK)
            :getTrulyRandomColorlessCardInCombat(expected));
        f.b.cards.createTempCardInHand(CardInstance(id,up));
        f.b.playCardQueueItem(CardQueueItem(f.b.cards.hand[0],0,3));
        check(f.b.cardRandomRng.counter==expected.counter,"random identity chosen during use, before queued insertion");
        check(f.b.cards.cardsInHand==0,"insertion stays queued");
        // Diagnostic interleaving, not a claimed natural game sequence.
        f.b.cardRandomRng.random(50);
        f.run();
        for(int i=0;i<count;++i)check(f.b.cards.hand[i].id==cards[i],"later RNG consumption cannot change already chosen identities");
    }
}
void callbacksAndRoots(){
    Fixture draw;
    CardInstance top(CardId::DEFEND_RED);top.setUniqueId(44);draw.b.cards.drawPile.push_back(top);
    draw.b.player.buff<PS::DARK_EMBRACE>(1);
    draw.play(CardId::JACK_OF_ALL_TRADES,true);
    check(draw.b.cards.cardsInHand==3&&draw.b.cards.hand[2].uniqueId==44,"both generations precede exhaust-triggered draw");
    Fixture full;
    for(int i=0;i<10;++i)full.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
    full.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::INFERNAL_BLADE));
    full.b.playTopCardInDrawPile(0,true);full.run();
    check(full.b.cards.cardsInHand==10&&full.b.cards.discardPile.size()==1,"autoplay generator over full hand sends card to discard");

    for(CardId id:{CardId::INFERNAL_BLADE,CardId::JACK_OF_ALL_TRADES})for(int seed=1;seed<=24;++seed){
        Fixture f;
        for(int i=0;i<8;++i)f.b.cards.createTempCardInDrawPile(0,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
        auto twin=f.b;std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        twin.cardRandomRng=Random(999999);twin.shuffleRng=Random(333333);
        std::array<std::uint64_t,7> seeds={11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.g,f.b,seeds);public_sampling::resampleCombatContinuation(f.g,twin,seeds);
        f.play(id,true);twin.cards.createTempCardInHand(CardInstance(id,true));twin.addToBotCard(CardQueueItem(twin.cards.hand[0],0,3));
        twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();
        check(f.b.cards.cardsInHand==twin.cards.cardsInHand,"paired worlds generate same count");
        for(int i=0;i<f.b.cards.cardsInHand;++i){
            check(f.b.cards.hand[i].id==twin.cards.hand[i].id&&f.b.cards.hand[i].uniqueId==twin.cards.hand[i].uniqueId,"sampled generation ignores original hidden RNG/order");
        }
        auto hand=f.b.cards.hand;
        public_sampling::resampleCombatContinuation(f.g,f.b,seeds);
        for(int i=0;i<f.b.cards.cardsInHand;++i)check(hand[i].id==f.b.cards.hand[i].id&&hand[i].costForTurn==f.b.cards.hand[i].costForTurn,"resolved roots retain public generated cards and costs");
    }
}
void generatedBloodCost(){
    for(int hits:{0,1,2,4,6}){
        Fixture f;
        for(int i=0;i<hits;++i){f.play(CardId::BLOODLETTING,false);}
        check(f.b.player.timesDamagedThisCombat==hits,"public damage event count retained");
        int seed=1;
        for(;seed<10000;++seed){Random trial(seed);if(getTrulyRandomCardInCombat(trial,CharacterClass::IRONCLAD,CardType::ATTACK)==CardId::BLOOD_FOR_BLOOD)break;}
        check(seed<10000,"fixed audit-only seed search reaches Blood for Blood");
        f.b.cardRandomRng=Random(seed);f.play(CardId::INFERNAL_BLADE,true);
        check(f.b.cards.hand[0].id==CardId::BLOOD_FOR_BLOOD,"generated Blood for Blood selected");
        check(f.b.cards.hand[0].cost==std::max(0,4-hits)&&f.b.cards.hand[0].costForTurn==0,"generated Blood for Blood inherits prior damage reduction");
        f.b.cards.resetAttributesAtEndOfTurn();
        check(f.b.cards.hand[0].costForTurn==std::max(0,4-hits),"generated Blood for Blood retains reduction after turn-zero expires");
    }
}
}
int main(){variants();timing();callbacksAndRoots();generatedBloodCost();std::cout<<"RANDOM_HAND_GENERATION_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
