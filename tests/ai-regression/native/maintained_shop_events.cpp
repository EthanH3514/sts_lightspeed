#include "game/GameContext.h"
#include "combat/BattleContext.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <tuple>
#include <cmath>
using namespace sts;
int main(){
    int checks=0;auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("maintained runtime check "+std::to_string(checks));};
    auto words=[](const Random&r){return std::make_tuple(r.counter,r.seed0,r.seed1);};
    try{
        for(int mode=0;mode<3;++mode){
            GameContext g(CharacterClass::IRONCLAD,6173,10);g.curRoom=Room::SHOP;g.screenState=ScreenState::SHOP_ROOM;
            g.info.shop.setup(g);g.gold=200;g.info.shop.potions[0]=Potion::FIRE_POTION;g.info.shop.potionPrice(0)=40;
            if(mode==1)while(g.potionCount<g.potionCapacity)g.obtainPotion(Potion::BLOCK_POTION);
            if(mode==2)g.obtainRelic(RelicId::SOZU);
            const auto rng=words(g.potionRng);const int count=g.potionCount;
            g.info.shop.buyPotion(g,0);
            check(g.gold==(mode?200:160));check(g.potionCount==count+(mode?0:1));check(words(g.potionRng)==rng);
            check(g.info.shop.potionPrice(0)==(mode?40:-1));
            if(mode)check(g.info.shop.potions[0]==Potion::FIRE_POTION);
        }
        for(int variant=0;variant<3;++variant){
            GameContext g(CharacterClass::IRONCLAD,6173,10);g.curRoom=Room::SHOP;g.screenState=ScreenState::SHOP_ROOM;
            if(variant==1)g.obtainRelic(RelicId::QUESTION_CARD);
            if(variant==2)g.obtainRelic(RelicId::BUSTED_CROWN);
            g.info.shop.setup(g);g.gold=500;g.info.shop.relics[0]=RelicId::ORRERY;g.info.shop.relicPrice(0)=100;
            g.info.shop.buyRelic(g,0);
            check(g.gold==400&&g.hasRelic(RelicId::ORRERY));check(g.screenState==ScreenState::REWARDS);
            check(g.info.rewardsContainer.cardRewardCount==5);
            for(int i=0;i<5;++i)check(g.info.rewardsContainer.cardRewards[i].size()==(variant==1?4:variant==2?1:3));
            check(g.info.shop.relicPrice(0)==-1);
            g.regainControl();check(g.screenState==ScreenState::SHOP_ROOM);
            g.regainControl();check(g.screenState==ScreenState::MAP_SCREEN);
        }
        {
            GameContext g(CharacterClass::IRONCLAD,6173,10);g.curEvent=Event::THE_NEST;g.curRoom=Room::EVENT;g.setupEvent();
            g.regainControlAction=[](GameContext&x){x.screenState=ScreenState::MAP_SCREEN;};g.chooseEventOption(1);
            bool found=false;for(auto&c:g.deck.cards)if(c.id==CardId::RITUAL_DAGGER){check(c.misc==15);found=true;}check(found);
        }
        {
            GameContext g(CharacterClass::IRONCLAD,6173,10);g.curRoom=Room::REST;g.screenState=ScreenState::REST_ROOM;g.obtainRelic(RelicId::PEACE_PIPE);
            g.regainControlAction=[](GameContext&x){x.screenState=ScreenState::MAP_SCREEN;};const int count=g.deck.size();
            g.chooseCampfireOption(4);check(g.screenState==ScreenState::CARD_SELECT);g.chooseSelectCardScreenOption(0);
            check(g.deck.size()==count-1&&g.screenState==ScreenState::MAP_SCREEN);
        }
        for(int act=1;act<=3;++act)for(int buff=0;buff<4;++buff){
            GameContext g(CharacterClass::IRONCLAD,123,0);g.floorNum=1;g.relics={};g.enterBattle(MonsterEncounter::GREMLIN_LEADER);
            BattleContext b;b.init(g);b.executeActions();auto&slot=b.monsters.arr[0];slot=Monster{};slot.id=MonsterId::INVALID;slot.curHp=slot.maxHp=slot.block=0;slot.halfDead=false;
            auto&live=b.monsters.arr[3];check(live.id==MonsterId::GREMLIN_LEADER);const auto hp=live.maxHp;
            const int strength=live.getStatus<MS::STRENGTH>(),metal=live.getStatus<MS::METALLICIZE>(),regen=live.getStatus<MS::REGEN>();
            b.monsters.applyEmeraldEliteBuff(b,buff,act);
            check(slot.id==MonsterId::INVALID&&slot.curHp==0&&slot.maxHp==0&&slot.block==0);
            check(slot.getStatus<MS::STRENGTH>()==0&&slot.getStatus<MS::METALLICIZE>()==0&&slot.getStatus<MS::REGEN>()==0);
            check(live.getStatus<MS::STRENGTH>()==strength+(buff==0?act:0));
            check(live.getStatus<MS::METALLICIZE>()==metal+(buff==2?act*2+2:0));
            check(live.getStatus<MS::REGEN>()==regen+(buff==3?act*2+1:0));
            check(live.maxHp==hp+(buff==1?int(std::round(float(hp)*.25f)):0));
        }
        std::cout<<nlohmann::json{{"checks",checks},{"complete",true}}.dump();return 0;
    }catch(const std::exception&e){std::cerr<<e.what();return 1;}
}
