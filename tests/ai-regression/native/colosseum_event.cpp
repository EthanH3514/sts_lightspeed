// Event boundary fixtures: combat is finished deterministically, not strength-tested.
#include "combat/BattleContext.h"
#include "sim/ConsoleSimulator.h"
#include "sim/PublicEventContext.h"
#include "sim/search/GameAction.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace sts;
using J=nlohmann::json;
int checks=0;
void check(bool good,const char* what){++checks;if(!good)throw std::runtime_error(what);}
GameContext root(int asc=20){
    GameContext g(CharacterClass::IRONCLAD,1234,asc);g.act=2;g.floorNum=25;
    g.curMapNodeY=8;g.curRoom=Room::EVENT;g.curEvent=Event::COLOSSEUM;g.curHp=50;
    g.info.eventData=99;g.setupEvent();return g;
}
void choose(GameContext& g,int n){ConsoleSimulator c;c.gc=&g;c.takeEventAction(std::to_string(n));}
std::string menu(GameContext& g){ConsoleSimulator c;c.gc=&g;std::ostringstream o;c.printActions(o);return o.str();}
void win(BattleContext& b,GameContext& g){
    b.addToBot(Actions::DamageAllEnemy(9999));b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();
    check(b.outcome==Outcome::PLAYER_VICTORY,"constructed lethal damage must win");b.exitBattle(g);
}
void first(GameContext& g){choose(g,0);check(g.screenState==ScreenState::BATTLE,"first choice must enter battle");}
int main(){try{
    for(int asc:{0,10,20}){
        auto g=root(asc);check(g.info.eventData==0,"setup resets event phase");
        check(g.canAddEvent(Event::COLOSSEUM),"Colosseum reachable after map row7");
        g.curMapNodeY=7;check(!g.canAddEvent(Event::COLOSSEUM),"spawn lower bound");g.curMapNodeY=8;
        check(search::GameAction::getValidEventSelectBits(g)==1,"initial only fight");
        auto initial=g;first(g);
        check(initial.screenState==ScreenState::EVENT_SCREEN,"clone start does not mutate source");
        check(g.info.encounter==MonsterEncounter::COLOSSEUM_EVENT_SLAVERS,"slaver encounter");
        BattleContext b;b.init(g);
        check(b.monsters.monsterCount==2&&b.monsters.arr[0].id==MonsterId::BLUE_SLAVER&&b.monsters.arr[1].id==MonsterId::RED_SLAVER,"first monster pair");
        // Witness first-combat consumption independently of a specific deck/play.
        b.aiRng.random(99);b.monsterHpRng.random(99);b.shuffleRng.random(99);b.cardRandomRng.random(99);
        auto expected=g;Rewards discarded;expected.addPotionRewards(discarded);
        const int gold=g.gold,cards=g.deck.size(),relics=g.relics.size();
        const auto cardCounter=g.cardRng.counter;
        win(b,g);
        check(g.screenState==ScreenState::EVENT_SCREEN&&g.info.eventData==1,"first win returns to event");
        check(g.curRoom==Room::EVENT&&g.act==2&&g.floorNum==25,"same event room/floor");
        check(g.info.rewardsContainer.getTotalCount()==0&&g.gold==gold&&g.deck.size()==cards&&g.relics.size()==relics,"first win grants no rewards");
        check(g.potionChance==expected.potionChance&&g.potionRng.counter==expected.potionRng.counter,"suppressed first potion drop still updates chance/RNG");
        check(g.cardRng.counter==cardCounter,"first win generates no card reward");
        check(search::GameAction::getValidEventSelectBits(g)==3&&menu(g).find("1: [VICTORY]")!=std::string::npos,"postfight two public choices");
        auto leave=g;choose(leave,0);check(leave.screenState==ScreenState::MAP_SCREEN&&leave.gold==gold,"leave after first fight");
        g.obtainRelic(RelicId::BLACK_STAR);g.obtainRelic(RelicId::GOLDEN_IDOL);
        g.obtainRelic(RelicId::WHITE_BEAST_STATUE);g.obtainRelic(RelicId::FACE_OF_CLERIC);
        expected=g;
        const auto rare=expected.returnRandomRelic(RelicTier::RARE),uncommon=expected.returnRandomRelic(RelicTier::UNCOMMON);
        choose(g,1);check(g.screenState==ScreenState::BATTLE&&g.info.encounter==MonsterEncounter::COLOSSEUM_EVENT_NOBS,"second encounter");
        check(g.info.rewardsContainer.relicCount==2&&g.info.rewardsContainer.relics[0]==rare&&g.info.rewardsContainer.relics[1]==uncommon,"rewards generated rare then uncommon before combat");
        check(g.gold==gold&&g.info.rewardsContainer.gold[0]==125,"Idol gold pending until claimed");
        auto branch=g;BattleContext second;second.init(branch);
        check(second.monsters.arr[0].id==MonsterId::TASKMASTER&&second.monsters.arr[1].id==MonsterId::GREMLIN_NOB,"second monster pair");
        check(second.aiRng.counter>branch.aiRng.counter&&second.monsterHpRng.counter>branch.monsterHpRng.counter,"second fight continues AI/HP streams");
        check(second.shuffleRng.counter>branch.shuffleRng.counter&&second.cardRandomRng.counter==branch.cardRandomRng.counter,"second fight continues shuffle/card streams");
        const auto cardBefore=branch.cardRng.counter;const int maxBefore=branch.maxHp;
        win(second,branch);const auto reward=branch.info.rewardsContainer;
        check(branch.screenState==ScreenState::REWARDS&&branch.maxHp==maxBefore+1,"second victory rewards and Cleric");
        check(reward.relicCount==2&&reward.relics[0]==rare&&reward.relics[1]==uncommon,"event has no Black Star extra relic");
        check(reward.goldRewardCount==1&&reward.gold[0]==125&&reward.cardRewardCount==1&&reward.potionCount==1,"second gold/card/potion rewards");
        check(branch.cardRng.counter>cardBefore&&!reward.emeraldKey&&!reward.sapphireKey,"event card generation but no elite key");
        check(spire_event_public::capture(branch)["event_name"]=="The Colosseum","reward retains public event origin");
        check(g.screenState==ScreenState::BATTLE&&g.cardRng.counter==cardBefore,"reward branch leaves source untouched");
        ConsoleSimulator console;console.gc=&branch;console.takeAction("gold 0");
        check(branch.gold==gold+125,"gold claimed once");console.takeAction("skip");
        check(branch.screenState==ScreenState::MAP_SCREEN,"reward skip exits to map");
    }
    for(bool nativeAction:{false,true}){
        auto g=root();first(g);BattleContext b;b.init(g);win(b,g);
        g.deck.obtain(g,Card(CardId::CLEAVE));
        g.uncommonRelicPool.insert(g.uncommonRelicPool.begin(),RelicId::BOTTLED_FLAME);
        choose(g,1);BattleContext second;second.init(g);win(second,g);
        check(g.info.rewardsContainer.relics[1]==RelicId::BOTTLED_FLAME,"non-basic attack allows Bottled Flame reward");
        ConsoleSimulator console;console.gc=&g;
        if(nativeAction) search::GameAction(search::GameAction::RewardsActionType::RELIC,1).execute(g);
        else console.takeAction("relic 1");
        check(g.screenState==ScreenState::CARD_SELECT,"reward relic can open card selector");
        check(spire_event_public::capture(g)["event_name"]=="The Colosseum","nested selector keeps event origin");
        auto isolated=g;
        console.takeAction("0");check(g.screenState==ScreenState::REWARDS,"relic selector returns to remaining rewards");
        check(g.info.rewardsContainer.relicCount==1&&g.info.rewardsContainer.goldRewardCount==1&&g.info.rewardsContainer.cardRewardCount==1,"nested selection preserves unclaimed rewards");
        check(isolated.screenState==ScreenState::CARD_SELECT&&!isolated.deck.anyCardBottled(),"nested callback leaves cloned source untouched");
        console.takeAction("skip");check(g.screenState==ScreenState::MAP_SCREEN,"nested reward completion exits");
    }
    for(int phase:{0,1}){
        auto g=root();if(phase){first(g);BattleContext b;b.init(g);win(b,g);}
        g.obtainRelic(RelicId::PRESERVED_INSECT);g.obtainRelic(RelicId::SLING_OF_COURAGE);g.obtainRelic(RelicId::SLAVERS_COLLAR);
        choose(g,phase);BattleContext b;b.init(g);
        check(b.player.energyPerTurn==(phase?4:3)&&b.player.getStatus<PS::STRENGTH>()==(phase?2:0),"elite relics only second event fight");
        for(int i=0;i<b.monsters.monsterCount;++i){auto& m=b.monsters.arr[i];check(m.curHp==(phase?int(m.maxHp*.75):m.maxHp),"Preserved Insect only second event fight");}
    }
    for(int phase:{0,1})for(bool escape:{false,true}){
        auto g=root();if(phase){first(g);BattleContext b;b.init(g);win(b,g);}
        g.obtainPotion(Potion::SMOKE_BOMB);choose(g,phase);BattleContext b;b.init(g);
        if(escape){b.drinkPotion(0,0);b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();check(b.outcome==Outcome::PLAYER_ESCAPED,"actual Smoke Bomb escape");}
        else{b.player.curHp=0;b.outcome=Outcome::PLAYER_LOSS;}
        b.exitBattle(g);
        if(!escape)check(g.outcome==GameOutcome::PLAYER_LOSS&&g.screenState!=ScreenState::REWARDS,"death cannot receive rewards or next choice");
        else check(g.screenState==(phase?ScreenState::MAP_SCREEN:ScreenState::EVENT_SCREEN)&&g.info.rewardsContainer.getTotalCount()==0,"first escape returns to choice; second exits without rewards");
    }
    {auto g=root();g.skipBattles=true;choose(g,0);check(g.screenState==ScreenState::EVENT_SCREEN&&g.info.eventData==1,"skipBattles first return");choose(g,1);check(g.screenState==ScreenState::REWARDS&&g.info.rewardsContainer.gold[0]==100,"skipBattles second rewards");}
    std::cout<<J{{"checks",checks},{"failures",J::array()}}.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cout<<J{{"checks",checks},{"error",e.what()}}.dump()<<'\n';return 1;}}
