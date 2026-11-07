#include "sim/ConsoleSimulator.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <vector>
using namespace sts;
using J=nlohmann::json;
J failures=J::array(),cases=J::array();int checks=0;
void check(bool good,const std::string& name){++checks;if(!good)failures.push_back(name);}
GameContext base(Event event,int asc=20){
    GameContext g(CharacterClass::IRONCLAD,1234,asc);g.act=2;g.floorNum=25;g.curHp=70;g.gold=500;
    g.curRoom=Room::EVENT;g.curEvent=event;g.screenState=ScreenState::EVENT_SCREEN;
    g.regainControlAction=[](GameContext& x){x.screenState=ScreenState::MAP_SCREEN;};return g;
}
std::string menu(GameContext& g){ConsoleSimulator s;s.gc=&g;std::ostringstream o;s.printActions(o);return o.str();}
void action(GameContext& g,int a){ConsoleSimulator s;s.gc=&g;s.takeEventAction(std::to_string(a));}
J deck(const GameContext& g){J d=J::array();for(auto& c:g.deck.cards)d.push_back({{"id",cardEnumStrings[int(c.id)]},{"upgrades",c.getUpgraded()}});return d;}
J relics(const GameContext& g){J r=J::array();for(auto& x:g.relics.relics)r.push_back({{"id",relicEnumNames[int(x.id)]},{"counter",x.data}});return r;}
J snapshot(GameContext& g){return J{{"event",eventGameNames[int(g.curEvent)]},{"ascension",g.ascension},{"act",g.act},{"floor",g.floorNum},
    {"hp",g.curHp},{"max_hp",g.maxHp},{"gold",g.gold},{"screen",int(g.screenState)},{"menu",menu(g)},{"deck",deck(g)},{"relics",relics(g)}};}
void failedRoll(GameContext& g,int threshold){
    for(int s=0;s<1000;++s){Random r(s);if(r.random(99)<threshold){g.miscRng=Random(s);return;}}
    throw std::runtime_error("fixture RNG search failed");
}
int main(int argc,char**argv){try{
    bool old=argc>1&&std::string(argv[1])=="--original";
    for(int asc:{10,20})for(int a=0;a<6;++a){
        auto g=base(Event::DESIGNER_IN_SPIRE,asc);g.info.upgradeOne=a==0;g.info.cleanUpIsRemoveCard=a==2;
        auto before=snapshot(g);int n=g.deck.size(),hp=g.curHp,gold=g.gold;
        action(g,a);int immediate=int(g.screenState),selected=0;
        while(g.screenState==ScreenState::CARD_SELECT){if(++selected>3)throw std::runtime_error("selector loop");g.chooseSelectCardScreenOption(0);}
        int cost=a<=1?(asc>=15?50:40):a<=3?(asc>=15?75:60):a==4?(asc>=15?110:90):0;
        check(g.gold==gold-cost,"designer_gold_"+std::to_string(a));
        check(g.curHp==hp-(a==5?(asc>=15?5:3):0),"designer_hp_"+std::to_string(a));
        check(selected==(a==0||a==2||a==4?1:a==3?2:0),"designer_selector_"+std::to_string(a));
        check(g.deck.size()==n-(a==2||a==4),"designer_card_count_"+std::to_string(a));
        check(g.screenState==ScreenState::MAP_SCREEN,"designer_finish_"+std::to_string(a));
        if(a==3)check(deck(g)!=before["deck"],"designer_transform_changes_cards");
        if(a==4)check(deck(g)!=before["deck"],"designer_remove_then_upgrade");
        cases.push_back({{"before",before},{"action",a},{"immediate_screen",immediate},{"selections",selected},{"after",snapshot(g)}});
    }
    for(int asc:{10,20})for(int phase:{0,2}){
        auto g=base(Event::SCRAP_OOZE,asc);g.info.eventData=phase;failedRoll(g,99-(25+10*phase));
        auto before=snapshot(g);int hp=g.curHp;action(g,0);
        check(g.curHp==hp-(asc>=15?5:3)-phase,"scrap_increasing_damage");
        check(g.info.eventData==phase+1&&g.screenState==ScreenState::EVENT_SCREEN,"scrap_continue");
        auto next=snapshot(g);action(g,1);check(g.screenState==ScreenState::MAP_SCREEN,"scrap_leave");
        cases.push_back({{"before",before},{"action",0},{"after",next}});
    }
    {
        auto g=base(Event::DEAD_ADVENTURER);g.info.phase=2;g.info.rewards={0,1,0};
        for(int s=0;s<1000;++s){Random r(s);if(r.random(99)>=85){g.miscRng=Random(s);break;}}
        auto before=snapshot(g);action(g,0);check(g.screenState==ScreenState::MAP_SCREEN,"dead_adventurer_three_successes_finish");
        cases.push_back({{"before",before},{"action",0},{"after",snapshot(g)}});
    }
    for(bool bottled:{false,true}){
        auto g=base(Event::FALLING,0);g.deck=Deck();g.deck.obtainRaw(Card(CardId::STRIKE_RED));
        if(bottled){g.deck.obtainRaw(Card(CardId::INFLAME));g.deck.bottleIdxs[2]=1;}
        g.setupEvent();auto before=snapshot(g);
        check(g.info.powerCardDeckIdx==-1,"falling_excludes_bottled_power");
        check(g.info.attackCardDeckIdx==0,"falling_attack_candidate");
        check(menu(g).find("[Splat]")==std::string::npos,"falling_no_free_exit_with_attack");
        action(g,2);check(g.deck.size()==(bottled?1:0),"falling_selected_card_removed");
        cases.push_back({{"before",before},{"action",2},{"after",snapshot(g)}});
    }
    if(!old){
        auto g=base(Event::FALLING);g.deck=Deck();g.deck.obtainRaw(Card(CardId::ASCENDERS_BANE));g.deck.obtainRaw(Card(CardId::REGRET));
        g.setupEvent();check(g.info.attackCardDeckIdx==-1&&g.info.skillCardDeckIdx==-1&&g.info.powerCardDeckIdx==-1,"falling_non_playable_types_excluded");
        check(menu(g).find("[Splat]")!=std::string::npos,"falling_only_non_playable_exit");action(g,3);
        check(g.deck.size()==2&&g.screenState==ScreenState::MAP_SCREEN,"falling_no_out_of_range_selection");
    }
    for(int gold:{0,20,500}){
        auto g=base(Event::WORLD_OF_GOOP);g.gold=gold;g.setupEvent();auto before=snapshot(g);int loss=g.info.goldLoss;
        check(menu(g).find("Lose "+std::to_string(loss)+" Gold.")!=std::string::npos,"goop_public_exact_cost");
        action(g,1);check(g.gold==gold-loss,"goop_cost_matches_execution");cases.push_back({{"before",before},{"action",1},{"after",snapshot(g)}});
    }
    std::cout<<J{{"checks",checks},{"failures",failures},{"cases",cases},{"original",old}}.dump();
    return !old&&!failures.empty()?1:0;
}catch(std::exception&e){std::cerr<<e.what();return 1;}}
