#include "sim/PublicEventContext.h"
#include <iostream>
#include <stdexcept>
using namespace sts;
int main() { try {
    int checks = 0;
    auto check = [&](bool v) { ++checks; if (!v) throw std::runtime_error("public event context"); };
    GameContext g(CharacterClass::IRONCLAD, 1234, 20);
    auto context = spire_event_public::capture(g);
    check(context.at("event_name") == "NEOW" && context.at("floor") == 0);
    g.act=2; g.floorNum=25; g.curRoom=Room::EVENT;
    g.regainControlAction=[](GameContext& x){x.screenState=ScreenState::MAP_SCREEN;};
    for(auto e : {Event::LAB, Event::BONFIRE_SPIRITS}) {
        g.curEvent=e;g.setupEvent();
        check(g.screenState != ScreenState::EVENT_SCREEN);
        context=spire_event_public::capture(g);
        check(context.at("available") == true && context.at("event_name") == eventGameNames[int(e)]);
        auto hiddenTwin=g;hiddenTwin.miscRng=Random(9876);hiddenTwin.potionRng=Random(777);
        hiddenTwin.info.eventData=99;
        check(spire_event_public::capture(hiddenTwin)==context);
        check(context.size()==5);
    }
    for(auto screen : {ScreenState::BATTLE,ScreenState::MAP_SCREEN,ScreenState::SHOP_ROOM,
            ScreenState::REST_ROOM,ScreenState::BOSS_RELIC_REWARDS}) {
        g.screenState=screen;check(!spire_event_public::capture(g).at("available").get<bool>());
    }
    g.curRoom=Room::SHOP;g.screenState=ScreenState::CARD_SELECT;
    check(!spire_event_public::capture(g).at("available").get<bool>());
    g.curRoom=Room::MONSTER;g.screenState=ScreenState::REWARDS;
    check(!spire_event_public::capture(g).at("available").get<bool>());
    g.curRoom=Room::EVENT;g.curEvent=Event::MONSTER;
    check(!spire_event_public::capture(g).at("available").get<bool>());
    // Do not disguise a combat-capable event's post-battle rewards as pure.
    g.curEvent=Event::MASKED_BANDITS;
    check(spire_event_public::capture(g).at("event_name")=="Masked Bandits");
    std::cout << "{\"checks\":" << checks << ",\"passed\":true}";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what();return 1; } }
