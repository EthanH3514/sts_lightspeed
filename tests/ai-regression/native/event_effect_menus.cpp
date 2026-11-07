#include <iostream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "sim/ConsoleSimulator.h"
using namespace sts;
using J=nlohmann::json;
int checks=0;
void check(bool x,const char* m){++checks;if(!x)throw std::runtime_error(m);}
J deck(const GameContext& g){J a=J::array();for(auto& c:g.deck.cards)a.push_back({{"id",cardEnumStrings[int(c.id)]},{"upgrades",c.getUpgraded()}});return a;}
int main(int argc,char**argv){try{
    bool original=argc>1&&std::string(argv[1])=="--original";J cases=J::array();int missing=0;
    for(int asc:{10,20})for(auto event:{Event::AUGMENTER,Event::OMINOUS_FORGE,Event::GOLDEN_SHRINE,Event::THE_MAUSOLEUM}){
        int count=event==Event::THE_MAUSOLEUM?2:3;
        for(int action=0;action<count;++action){
            GameContext g(CharacterClass::IRONCLAD,1234,asc);g.act=2;g.floorNum=25;
            g.curEvent=event;g.curRoom=Room::EVENT;g.screenState=ScreenState::EVENT_SCREEN;
            g.regainControlAction=[](GameContext& c){c.screenState=ScreenState::MAP_SCREEN;};
            ConsoleSimulator sim;sim.gc=&g;std::ostringstream text;sim.printEventActions(text);
            auto before=deck(g);int n=g.deck.size(),relics=g.relics.size(),gold=g.gold;
            sim.takeEventAction(std::to_string(action));int selections=0;
            while(g.screenState==ScreenState::CARD_SELECT){check(selections<2,"too many selections");g.chooseSelectCardScreenOption(0);++selections;}
            check(g.screenState==ScreenState::MAP_SCREEN,"event did not complete");
            if(event==Event::AUGMENTER){
                if(action==0)check(g.deck.size()==n+1&&g.deck.getCountMatching([](const Card& c){return c.id==CardId::JAX;})==1,"JAX reward");
                if(action==1)check(g.deck.size()==n&&selections==2,"two transforms");
                if(action==2)check(g.hasRelic(RelicId::MUTAGENIC_STRENGTH),"mutagens reward");
            }else if(event==Event::OMINOUS_FORGE){
                if(action==0)check(g.deck.size()==n&&deck(g)!=before,"forge upgrade");
                if(action==1)check(g.hasRelic(RelicId::WARPED_TONGS)&&g.deck.size()==n+1,"forge tongs and curse");
                if(action==2)check(deck(g)==before&&g.relics.size()==relics,"forge leave");
            }else if(event==Event::GOLDEN_SHRINE){
                check(g.gold-gold==(action==0?(asc>=15?50:100):action==1?275:0),"gold reward");
                check(g.deck.size()==n+(action==1),"gold shrine curse");
            }else{
                int gain=g.relics.size()-relics;
                if(action==0){if(gain==0)++missing;check(gain==(original?0:1),"mausoleum must grant one relic");
                    if(asc>=15)check(g.deck.getCountMatching([](const Card& c){return c.id==CardId::WRITHE;})==1,"A15 curse");}
                else check(deck(g)==before&&gain==0,"mausoleum leave");
            }
            cases.push_back({{"event",eventGameNames[int(event)]},{"ascension",asc},{"action",action},
                {"menu",text.str()},{"before",before},{"after",deck(g)},{"relic_gain",g.relics.size()-relics},{"selections",selections}});
        }
    }
    std::cout<<J{{"checks",checks},{"missing_reward_cases",missing},{"cases",cases}}.dump()<<'\n';return 0;
}catch(std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
