#include "game/GameContext.h"
#include "sim/PublicDeckSelector.h"
#include <iostream>
#include <stdexcept>
using namespace sts;
int checks=0;
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
auto view(const GameContext& g){return public_deck_selector::capture(g.screenState,g.deck,g.info);}
int main(){try{
    GameContext g(CharacterClass::IRONCLAD,7,20);g.deck=Deck{};
    for(auto id:{CardId::ASCENDERS_BANE,CardId::DEFEND_RED,CardId::DEFEND_RED,CardId::RITUAL_DAGGER})g.deck.obtainRaw(Card(id));
    g.deck.bottleCard(2,CardType::SKILL);g.deck.cards[3].misc=23;
    g.openCardSelectScreen(CardSelectScreenType::UPGRADE,1);
    auto p=view(g);check(p["available"]==true && p["cards"].size()==3,"upgrade candidates");
    check(p["cards"][0]["deck_index"]==1 && p["cards"][1]["deck_index"]==2,"duplicate identities distinct");
    check(p["cards"][1]["option_index"]==1 && p["cards"][2]["card"]["misc"]==23,"gapped deck index and persistent misc");
    GameContext twin(CharacterClass::IRONCLAD,999,20);twin.deck=g.deck;twin.info=g.info;twin.screenState=g.screenState;
    check(view(twin)==p,"hidden RNG twins identical");
    g.openCardSelectScreen(CardSelectScreenType::REMOVE,1);p=view(g);
    check(p["cards"].size()==2 && p["cards"][0]["deck_index"]==1 && p["cards"][1]["deck_index"]==3,"unremovable/bottled exclusions");
    g.info.toSelectCards[0].deckIdx=99;bool rejected=false;
    try{view(g);}catch(const std::exception&){rejected=true;}check(rejected,"invalid mapping rejects");
    g.openCardSelectScreen(CardSelectScreenType::UPGRADE,1);g.deck.cards[1].upgrade();rejected=false;
    try{view(g);}catch(const std::exception&){rejected=true;}check(rejected,"stale card rejects");
    g.screenState=ScreenState::EVENT_SCREEN;check(view(g)==nlohmann::json({{"version","public-deck-selector-v1"},{"available",false}}),"non-selector reveals nothing");
    g.screenState=ScreenState::CARD_SELECT;g.info.selectScreenType=CardSelectScreenType::OBTAIN;
    check(view(g)["available"]==false,"generated cards are not deck identities");
    std::cout<<"public deck selector checks: "<<checks<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
