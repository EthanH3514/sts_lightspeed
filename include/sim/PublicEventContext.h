#pragma once
#include "game/GameContext.h"
#include <nlohmann/json.hpp>

namespace spire_event_public {
inline constexpr const char* version = "public-event-context-v1";

// Current room identity only. In the game the event title remains visible
// behind its selection/reward overlay. Never export event rolls, hidden offers,
// subsequent rooms, encounter identity, RNG counters or persistent profile data.
inline nlohmann::json capture(const sts::GameContext& g) {
    using namespace sts;
    nlohmann::json result = {{"version", version}, {"available", false}};
    const bool ownScreen = g.screenState == ScreenState::EVENT_SCREEN ||
        g.screenState == ScreenState::CARD_SELECT || g.screenState == ScreenState::REWARDS;
    const bool eventRoom = g.curRoom == Room::EVENT ||
        (g.floorNum == 0 && g.curEvent == Event::NEOW);
    const int event = static_cast<int>(g.curEvent);
    if (!ownScreen || !eventRoom || event < static_cast<int>(Event::NEOW) ||
        event > static_cast<int>(Event::WORLD_OF_GOOP)) return result;
    result["available"] = true;
    result["event_name"] = eventGameNames[event];
    result["act"] = g.act;
    result["floor"] = g.floorNum;
    return result;
}
}
