#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <set>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/BattleSimulator.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0,failures=0;
void check(bool ok,const char *msg){++checks;if(!ok){++failures;if(failures<=20)std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game;BattleContext b;
    Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0){
        game.floorNum=1;game.relics={};if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST);b.init(game);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=3;
        b.player.curHp=70;b.player.maxHp=80;b.player.buff<PS::THORNS>(1);
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
    }
    void add(CardId id,bool up=false){b.cards.createTempCardInHand(CardInstance(id,up));}
    void supply(int attacks,int skills=3){
        for(int i=0;i<attacks;++i){CardInstance c(i%2?CardId::RAMPAGE:CardId::STRIKE_RED,i%2);if(c.id==CardId::RAMPAGE)c.specialData=13;c.costForTurn=0;b.cards.createTempCardInDrawPile(0,c);}
        for(int i=0;i<skills;++i)b.cards.createTempCardInDrawPile(0,CardInstance(CardId::DEFEND_RED));
    }
    void use(int i){search::Action a(search::ActionType::CARD,i,0);check(a.isValidAction(b),"legal source play");if(a.isValidAction(b))a.execute(b);}
};
using PublicCards=std::map<int,std::array<int,5>>;
PublicCards cards(const BattleContext &b){
    PublicCards r;bool unique=true;auto add=[&](const CardInstance &c){unique&=!r.count(c.uniqueId);r[c.uniqueId]={int(c.id),c.getUpgradeCount(),c.cost,c.costForTurn,c.specialData};};
    for(int i=0;i<b.cards.cardsInHand;++i)add(b.cards.hand[i]);
    for(const auto &c:b.cards.drawPile)add(c);for(const auto &c:b.cards.discardPile)add(c);for(const auto &c:b.cards.exhaustPile)add(c);check(unique,"each combat identity occurs in exactly one pile");return r;
}
void violence(){
    for(bool up:{false,true})for(int supply:{0,1,2,3,4,7})for(int occupied:{0,8,9})for(bool noDraw:{false,true}){
        Fixture f;f.supply(supply);for(int i=0;i<occupied;++i)f.add(CardId::DEFEND_RED);f.add(CardId::VIOLENCE,up);
        if(noDraw)f.b.player.buff<PS::NO_DRAW>();auto before=cards(f.b);const int n=std::min(supply,up?4:3);
        f.use(occupied);
        check(f.b.cards.cardsInHand==std::min(10,occupied+n),"retrieval bypasses No Draw and respects capacity");
        check(f.b.cards.drawPile.size()==static_cast<unsigned>(supply+3-n),"selected attacks leave draw pile even when supply is short");
        check(f.b.cards.discardPile.size()==static_cast<unsigned>(std::max(0,occupied+n-10)),"overflow moves to discard");
        check(f.b.cards.exhaustPile.size()==1&&f.b.cards.exhaustPile[0].id==CardId::VIOLENCE,"Violence exhausts itself");
        check(f.b.player.energy==3,"Violence costs zero");
        // DrawPileToHandAction sends overflow through Soul discard movement.
        // Only overflow resets temporary cost; retrieved/remaining cards keep it.
        for(const auto &c:f.b.cards.discardPile) before.at(c.uniqueId)[3]=before.at(c.uniqueId)[2];
        check(cards(f.b)==before,"movement preserves identity/base cost/growth; settled overflow resets turn cost");
        for(int i=occupied;i<f.b.cards.cardsInHand;++i)check(f.b.cards.hand[i].getType()==CardType::ATTACK,"only attacks retrieved");
    }
    Fixture callback;callback.supply(2,8);callback.add(CardId::VIOLENCE);
    callback.b.player.buff<PS::FEEL_NO_PAIN>(3);callback.b.player.buff<PS::DARK_EMBRACE>(1);
    callback.use(0);check(callback.b.player.block==3,"source exhaust triggers Feel No Pain once");
    check(callback.b.cards.cardsInHand==3&&callback.b.cards.drawPile.size()==7,"Dark Embrace is the separate post-retrieval draw");
}
void purity(){
    for(bool up:{false,true})for(int n:{0,1,3,6})for(int mask=0;mask<(1<<n);++mask){
        int selected=0;for(int i=0;i<n;++i)selected+=(mask>>i)&1;if(selected>(up?5:3))continue;
        Fixture f;f.supply(0,12);for(int i=0;i<n;++i)f.add(i%2?CardId::STRIKE_RED:CardId::WOUND);f.add(CardId::PURITY,up);
        auto before=cards(f.b);std::set<int> ids;for(int i=0;i<n;++i)if(mask&(1<<i))ids.insert(f.b.cards.hand[i].uniqueId);
        f.b.player.buff<PS::FEEL_NO_PAIN>(3);f.use(n);
        if(n){
            check(f.b.inputState==InputState::CARD_SELECT&&f.b.cardSelectInfo.pickCount==(up?5:3),"Purity prompts with variant maximum");
            search::Action a(search::ActionType::MULTI_CARD_SELECT,mask);check(a.isValidAction(f.b),"every subset up to limit is legal including zero");a.execute(f.b);
        }
        check(f.b.inputState==InputState::PLAYER_NORMAL,"selection resolves without deadlock");
        check(f.b.cards.cardsInHand==n-selected,"only selected cards leave hand");
        check(f.b.cards.exhaustPile.size()==static_cast<unsigned>(selected+1),"selected cards plus source exhaust exactly once");
        check(f.b.player.block==3*(selected+1),"each exhaust has its own callback");
        for(const auto &c:f.b.cards.exhaustPile)if(c.id!=CardId::PURITY)check(ids.erase(c.uniqueId)==1,"selected identity exhausted");
        check(ids.empty()&&cards(f.b)==before,"no selected identities lost or duplicated");
    }
    Fixture callback;callback.supply(0,10);callback.add(CardId::SENTINEL,true);callback.add(CardId::WOUND);callback.add(CardId::PURITY);
    callback.b.player.buff<PS::DARK_EMBRACE>(1);callback.use(2);
    search::Action(search::ActionType::MULTI_CARD_SELECT,3).execute(callback.b);
    check(callback.b.player.energy==6,"selected upgraded Sentinel grants three energy");
    check(callback.b.cards.cardsInHand==3&&callback.b.cards.drawPile.size()==7,"selected and source exhaust each draw once");
    // Exercise the actual console command adapter, including its zero-selection spelling.
    for(bool none:{false,true}){Fixture f;f.supply(0);f.add(CardId::WOUND);f.add(CardId::STRIKE_RED);f.add(CardId::PURITY);f.use(2);
        BattleSimulator console;*console.bc=f.b;console.takeAction(none?"none":"1 0");
        check(console.bc->inputState==InputState::PLAYER_NORMAL&&console.bc->cards.exhaustPile.size()==(none?1u:3u),"console none / descending subset commands resume queue");}
}
void roots(){
    for(int sample=1;sample<=8;++sample)for(bool frozen:{false,true})for(auto id:{CardId::VIOLENCE,CardId::PURITY})for(bool up:{false,true}){
        Fixture f(frozen);f.supply(7,5);f.add(CardId::WOUND);f.add(id,up);auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());twin.cardRandomRng=Random(9999);
        const std::array<std::uint64_t,7> seeds={101,static_cast<unsigned>(sample),103,104,105,106,107};
        auto before=cards(f.b);auto draw=f.b.cards.drawPile;public_sampling::resampleCombatContinuation(f.game,f.b,seeds);public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        check(cards(f.b)==before,"root retains public identities and mutable state");
        if(frozen)for(size_t i=0;i<draw.size();++i)check(draw[i].uniqueId==f.b.cards.drawPile[i].uniqueId,"Frozen Eye order retained");
        f.use(1);search::Action(search::ActionType::CARD,1,0).execute(twin);
        if(id==CardId::PURITY){bool rejected=false;try{public_sampling::resampleCombatContinuation(f.game,f.b,seeds);}catch(const std::runtime_error&){rejected=true;}
            check(rejected,"pending multi-selection is not a resampling root");search::Action(search::ActionType::MULTI_CARD_SELECT,1).execute(f.b);search::Action(search::ActionType::MULTI_CARD_SELECT,1).execute(twin);}
        check(cards(f.b)==cards(twin),"paired post-play identities agree");check(f.b.cards.cardsInHand==twin.cards.cardsInHand,"paired hand sizes agree");
        for(int i=0;i<f.b.cards.cardsInHand;++i)check(f.b.cards.hand[i].uniqueId==twin.cards.hand[i].uniqueId,"paired retrieval outcome/order agrees");
        auto after=cards(f.b);public_sampling::resampleCombatContinuation(f.game,f.b,seeds);check(cards(f.b)==after,"resolved post-play root preserves state");
    }
}
}
int main(){violence();purity();roots();std::cout<<"BULK_EXHAUST_RETRIEVAL_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
