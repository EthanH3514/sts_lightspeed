#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *msg) {++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game; BattleContext b;
    explicit Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1;game.relics={};if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST);b.init(game);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=100;
        b.player.maxHp=80;b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=10000;
        for(int i=0;i<10;++i)b.cards.createTempCardInDrawPile(i,CardInstance(CardId::DEFEND_RED));
    }
    void add(CardId id,int upgrades=0) {
        CardInstance c(id);for(int i=0;i<upgrades;++i)c.upgrade();
        b.cards.createTempCardInHand(c);
    }
    int use(int index) {
        const int hp=b.monsters.arr[0].curHp;auto c=b.cards.hand[index];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy));
        b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();
        return hp-b.monsters.arr[0].curHp;
    }
    void play(CardId id,int upgrades=0) {add(id,upgrades);use(b.cards.cardsInHand-1);}
};
void scaling() {
    for(int up:{0,1})for(int count:{0,1,4})for(int strength:{-2,0,3})
    for(bool weak:{false,true})for(bool vulnerable:{false,true}) {
        Fixture f;f.add(CardId::PERFECTED_STRIKE,up);
        for(int i=0;i<count;++i)f.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED));
        f.b.cards.createTempCardInDiscard(CardInstance(CardId::POMMEL_STRIKE));
        f.b.cards.exhaustPile.push_back(CardInstance(CardId::WILD_STRIKE));
        // The current card and discard count; exhausted Strike does not.
        check(f.b.cards.strikeCount==count+2,"active Strike count includes self and discard, not exhaust");
        f.b.player.buff<PS::STRENGTH>(strength);
        if(weak)f.b.player.debuff<PS::WEAK>(1);
        if(vulnerable)f.b.monsters.arr[0].addDebuff<MS::VULNERABLE>(1,false);
        float expected=6+(up?3:2)*(count+2)+strength;
        if(weak)expected*=.75f;if(vulnerable)expected*=1.5f;
        f.b.monsters.arr[0].block=2;
        check(f.use(0)==std::max(0,int(expected)-2),"Perfected Strike modifiers applied after count scaling");
    }
    Fixture exhausted;exhausted.add(CardId::PERFECTED_STRIKE);exhausted.add(CardId::STRIKE_RED);
    const int strike=exhausted.b.cards.strikeCount;
    exhausted.b.addToBot(Actions::ExhaustSpecificCardInHand(1,exhausted.b.cards.hand[1].uniqueId));
    exhausted.b.inputState=InputState::EXECUTING_ACTIONS;exhausted.b.executeActions();
    check(exhausted.b.cards.strikeCount==strike-1,"actual exhaustion decrements Strike counter");
    check(exhausted.use(0)==8,"Perfected Strike observes decreased count");
    for(int n:{0,1,2,3,10,25}) {
        Fixture f;f.add(CardId::SEARING_BLOW,n);const int uid=f.b.cards.hand[0].uniqueId;
        check(f.b.cards.hand[0].getUpgradeCount()==n,"repeatable upgrade count retained");
        check(f.use(0)==12+n*(n+7)/2,"Searing Blow uses complete upgrade count");
        check(f.b.cards.discardPile.back().uniqueId==uid,"Searing Blow identity survives play");
        f.play(CardId::APOTHEOSIS);
        check(f.b.cards.discardPile.front().getUpgradeCount()==n+1,"Apotheosis adds one more upgrade even to plus-N");
    }
}
void growth() {
    for(int up:{0,1}) {
        Fixture f;f.add(CardId::RAMPAGE,up);f.add(CardId::RAMPAGE,up);
        const int uid=f.b.cards.hand[0].uniqueId;
        check(f.use(0)==8,"first Rampage hit precedes growth");
        check(f.b.cards.discardPile.back().specialData==(up?8:5),"growth persists on played card");
        check(f.b.cards.hand[0].specialData==0,"different Rampage identity unaffected");
        // Public discard -> top -> draw -> replay, without manipulating growth.
        f.play(CardId::HEADBUTT);
        if(f.b.inputState==InputState::CARD_SELECT)search::Action(search::ActionType::SINGLE_CARD_SELECT,0).execute(f.b);
        f.b.addToBot(Actions::DrawCards(1));f.b.inputState=InputState::EXECUTING_ACTIONS;f.b.executeActions();
        int found=-1;for(int i=0;i<f.b.cards.cardsInHand;++i)if(f.b.cards.hand[i].uniqueId==uid)found=i;
        check(found>=0,"Headbutt and draw recover the original Rampage identity");
        if(found>=0) {
            check(f.use(found)==8+(up?8:5),"replayed Rampage uses accumulated damage");
            check(f.b.cards.discardPile.back().specialData==2*(up?8:5),"second growth is additive");
        }
        f.play(CardId::APOTHEOSIS);
        for(const auto &c:f.b.cards.discardPile)if(c.uniqueId==uid)
            check(c.specialData==2*(up?8:5)&&c.isUpgraded(),"upgrade preserves already accumulated damage");
        f.b.cards.resetAttributesAtEndOfTurn();
        for(const auto &c:f.b.cards.discardPile)if(c.uniqueId==uid)
            check(c.specialData==2*(up?8:5),"turn reset preserves growth");
    }
    for(auto id:{CardId::RAMPAGE,CardId::SEARING_BLOW}) {
        Fixture f;f.add(id,1);f.b.monsters.arr[0].buff<MS::THORNS>(3);
        f.use(0);
        check(f.b.player.curHp==67,"dynamic damage is an attack with Thorns callback");
        Fixture angry;angry.add(id,1);angry.b.monsters.arr[0].buff<MS::ANGRY>(2);angry.use(0);
        check(angry.b.monsters.arr[0].getStatus<MS::STRENGTH>()==2,"dynamic attack triggers Angry");
    }
}
using State=std::map<int,std::array<int,4>>;
State publicCards(const BattleContext &b) {
    State result;
    auto add=[&](const CardInstance &c){result[c.uniqueId]={int(c.id),c.getUpgradeCount(),c.specialData,c.costForTurn};};
    for(int i=0;i<b.cards.cardsInHand;++i)add(b.cards.hand[i]);
    for(const auto &c:b.cards.drawPile)add(c);
    for(const auto &c:b.cards.discardPile)add(c);
    for(const auto &c:b.cards.exhaustPile)add(c);
    return result;
}
void roots() {
    const std::array<std::uint64_t,7> seeds={101,102,103,104,105,106,107};
    for(bool frozen:{false,true})for(auto id:{CardId::PERFECTED_STRIKE,CardId::RAMPAGE,CardId::SEARING_BLOW})
    for(int up:{0,1}) {
        Fixture f(frozen);f.add(id,up);f.add(CardId::RAMPAGE);f.add(CardId::SEARING_BLOW,4);
        f.b.cards.hand[1].specialData=13;
        f.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED));
        auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        twin.cardRandomRng=Random(999);
        const auto before=publicCards(f.b);
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        check(publicCards(f.b)==before,"sampling preserves per-identity growth/upgrades/costs");
        for(size_t i=0;i<f.b.cards.drawPile.size();++i)
            check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"paired hidden-order independence");
        twin.addToBotCard(CardQueueItem(twin.cards.hand[0],0,twin.player.energy));
        twin.inputState=InputState::EXECUTING_ACTIONS;twin.executeActions();f.use(0);
        check(f.b.monsters.arr[0].curHp==twin.monsters.arr[0].curHp,"paired dynamic hits agree");
        check(publicCards(f.b)==publicCards(twin),"paired growth state agrees after play");
        const auto after=publicCards(f.b);const auto draw=f.b.cards.drawPile;
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(publicCards(f.b)==after,"post-play roots preserve dynamic state");
        if(frozen)for(size_t i=0;i<draw.size();++i)
            check(f.b.cards.drawPile[i].uniqueId==draw[i].uniqueId,"Frozen Eye retains visible order");
    }
}
}
int main() {
    scaling();growth();roots();
    std::cout<<"DYNAMIC_DAMAGE_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
