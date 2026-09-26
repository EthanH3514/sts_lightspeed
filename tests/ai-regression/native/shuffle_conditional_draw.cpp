#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include "combat/BattleContext.h"
#include "game/Game.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) {
    ++checks; if(!ok) {++failures; if(failures<=20) std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game; BattleContext b;
    Fixture():game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={}; game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game); b.executeActions(); b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=3; b.player.curHp=70; b.player.maxHp=80;
        b.player.buff<PS::THORNS>(1); // Keep empty-active-deck control fixtures ongoing.
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
    }
    void draw(int n) {for(int i=0;i<n;++i) b.cards.createTempCardInDrawPile(i,CardInstance(CardId::DEFEND_RED));}
    void run() {b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();}
    void play(CardId id,bool up=false) {
        b.cards.createTempCardInHand(CardInstance(id,up));
        const auto c=b.cards.hand[b.cards.cardsInHand-1];
        check(c.canUse(b,0,false),"both skills remain legal independent of hand condition");
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); run();
    }
};
void impatience() {
    for(bool up:{false,true}) for(bool attack:{false,true}) for(bool blocked:{false,true})
    for(int occupied:{1,9}) for(int supply:{0,1,5}) {
        Fixture f; f.draw(supply);
        for(int i=0;i<occupied;++i) f.b.cards.createTempCardInHand(CardInstance(i==0&&attack?CardId::STRIKE_RED:CardId::DEFEND_RED));
        if(blocked) f.b.player.buff<PS::NO_DRAW>();
        f.play(CardId::IMPATIENCE,up);
        const int count=attack||blocked?0:std::min({up?3:2,supply,10-occupied});
        check(f.b.cards.cardsInHand==occupied+count,"no-attack hand condition and No Draw/capacity");
        check(f.b.cards.drawPile.size()==static_cast<unsigned>(supply-count),"conditional draw consumes exact supply");
        check(f.b.player.energy==3,"Impatience costs zero");
        check(f.b.cards.discardPile.size()==1&&f.b.cards.exhaustPile.empty(),"Impatience discards even when no draw");
    }
    for(bool up:{false,true}) for(bool addAttack:{false,true}) {
        Fixture f; f.draw(5);
        if(!addAttack) f.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
        // Construct a queued hand change to distinguish use-time from resolution-time checking.
        f.b.addToBot({[=](BattleContext &b) {
            if(addAttack) b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
            else {auto c=b.cards.hand[0]; b.cards.removeFromHandAtIdx(0); b.cards.moveToDiscardPile(c);}
        }});
        f.b.curCardQueueItem=CardQueueItem(CardInstance(CardId::IMPATIENCE,up),0,3);
        f.b.useSkillCard(); f.run();
        check(f.b.cards.cardsInHand==(addAttack?1:up?3:2),"condition is checked at queued resolution, not use time");
    }
}
void deepBreath() {
    for(bool up:{false,true}) for(bool blocked:{false,true}) for(int occupied:{0,9})
    for(int draws:{0,1,5}) for(int discards:{0,1,4}) for(int seed=1;seed<=8;++seed) {
        Fixture f; f.draw(draws);
        for(int i=0;i<discards;++i) f.b.cards.createTempCardInDiscard(CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
        for(int i=0;i<occupied;++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        if(blocked) f.b.player.buff<PS::NO_DRAW>();
        f.b.player.setHasRelic<RelicId::THE_ABACUS>(true);
        f.b.player.setHasRelic<RelicId::SUNDIAL>(true); f.b.player.sundialCounter=2;
        f.b.shuffleRng=Random(seed); auto rng=f.b.shuffleRng;
        auto expected=f.b.cards.drawPile, moved=f.b.cards.discardPile;
        if(discards) {
            java::Collections::shuffle(moved.begin(),moved.end(),java::Random(rng.randomLong()));
            expected.insert(expected.end(),moved.begin(),moved.end());
            java::Collections::shuffle(expected.begin(),expected.end(),java::Random(rng.randomLong()));
        }
        const int take=blocked?0:std::min({up?2:1,draws+discards,10-occupied});
        // Original DrawCardAction also queues EmptyDeckShuffle when its request
        // exceeds the nonempty supply, even if the discard is now empty.
        const bool drawShuffle=!blocked&&draws+discards>0&&std::min(up?2:1,10-occupied)>draws+discards;
        if(drawShuffle) rng.randomLong();
        const int shuffleTriggers=(discards?1:0)+(drawShuffle?1:0);
        f.play(CardId::DEEP_BREATH,up);
        check(f.b.cards.cardsInHand==occupied+take,"Deep Breath draw count/capacity/No Draw");
        check(f.b.cards.drawPile.size()==expected.size()-take,"merged pile has expected size");
        check(f.b.cards.discardPile.size()==1&&f.b.cards.discardPile[0].id==CardId::DEEP_BREATH,"played card is not shuffled into its own draw");
        check(f.b.shuffleRng.counter==rng.counter,"explicit and draw-shortfall shuffle RNG");
        check(f.b.player.block==shuffleTriggers*6,"one Abacus trigger per actual shuffle action");
        check(f.b.player.energy==(shuffleTriggers?5:3)&&f.b.player.sundialCounter==(shuffleTriggers?(shuffleTriggers-1):2),"Sundial counts explicit plus draw-shortfall shuffle actions");
        for(int i=0;i<take&&occupied+i<f.b.cards.cardsInHand;++i)
            check(f.b.cards.hand[occupied+i].uniqueId==expected[expected.size()-1-i].uniqueId,"draw follows combined shuffle");
        for(unsigned i=0;i<f.b.cards.drawPile.size()&&i<expected.size();++i)
            check(f.b.cards.drawPile[i].uniqueId==expected[i].uniqueId,"remaining draw order follows shuffle");
    }
}
void callbacks() {
    for(CardId id:{CardId::DEEP_BREATH,CardId::IMPATIENCE}) for(bool up:{false,true}) {
        Fixture f; f.draw(5); f.b.cards.drawPile.back().id=CardId::WOUND;
        f.b.player.buff<PS::EVOLVE>(); f.b.player.buff<PS::FIRE_BREATHING>(6);
        f.play(id,up);
        const int count=id==CardId::DEEP_BREATH?(up?2:1):(up?3:2);
        check(f.b.cards.cardsInHand==count+1&&f.b.monsters.arr[0].curHp==994,"actual draws trigger Evolve and Fire Breathing");
        Fixture exhaust; exhaust.draw(6); exhaust.b.player.buff<PS::CORRUPTION>();
        exhaust.b.player.buff<PS::FEEL_NO_PAIN>(3); exhaust.b.player.buff<PS::DARK_EMBRACE>();
        exhaust.play(id,up);
        check(exhaust.b.cards.exhaustPile.size()==1&&exhaust.b.player.block==3,"Corruption exhaust triggers Feel No Pain once");
        check(exhaust.b.cards.cardsInHand==count+1,"Dark Embrace follows skill draws");
        Fixture autoPlay; autoPlay.draw(5); autoPlay.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
        autoPlay.b.cards.createTempCardInDrawPile(5,CardInstance(id,up));
        autoPlay.b.playTopCardInDrawPile(0,true); autoPlay.run();
        check(autoPlay.b.cards.cardsInHand==(id==CardId::IMPATIENCE?1:count+1),"autoplay also obeys no-attack condition");
    }
}
void roots() {
    for(CardId id:{CardId::DEEP_BREATH,CardId::IMPATIENCE}) for(bool up:{false,true})
    for(bool frozen:{false,true}) for(int seed=1;seed<=16;++seed) {
        Fixture f; f.draw(8); f.b.cards.drawPile[2].id=CardId::STRIKE_RED;
        f.b.cards.createTempCardInDiscard(CardInstance(CardId::BASH));
        f.b.cards.createTempCardInHand(CardInstance(id,up));
        if(frozen) f.b.player.setHasRelic<RelicId::FROZEN_EYE>(true);
        auto twin=f.b; if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        twin.shuffleRng=Random(9999); twin.cardRandomRng=Random(8877);
        const std::array<std::uint64_t,7> seeds{11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        search::Action(search::ActionType::CARD,0,0).execute(f.b);
        search::Action(search::ActionType::CARD,0,0).execute(twin);
        check(f.b.cards.cardsInHand==twin.cards.cardsInHand,"paired sampled draw counts agree");
        for(int i=0;i<f.b.cards.cardsInHand;++i) check(f.b.cards.hand[i].uniqueId==twin.cards.hand[i].uniqueId,"paired roots ignore original hidden order/RNG");
        const auto count=f.b.cards.cardsInHand;
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.cards.cardsInHand==count&&f.b.cards.discardPile.back().id==id,"resolved root preserves public hand and played identity");
    }
}
}
int main() {
    impatience(); deepBreath(); callbacks(); roots();
    std::cout<<"SHUFFLE_CONDITIONAL_DRAW_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
