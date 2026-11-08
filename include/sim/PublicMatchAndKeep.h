#pragma once
#include "game/GameContext.h"
#include <nlohmann/json.hpp>

namespace spire_match_public {
inline constexpr const char* version = "public-match-and-keep-v1";
// Only observed tile memory. Unknown identities never leave this endpoint.
// Removed tiles were necessarily revealed; retaining those observations enables
// clients to remember a successful match without consulting a hidden board.
inline nlohmann::json capture(const sts::GameContext& g) {
    using namespace sts;
    nlohmann::json result = {{"version", version}, {"available", false}};
    if (g.outcome != GameOutcome::UNDECIDED || g.curRoom != Room::EVENT ||
        g.curEvent != Event::MATCH_AND_KEEP || g.screenState != ScreenState::EVENT_SCREEN)
        return result;
    if (g.info.toSelectCards.size() != 12 || g.info.eventData < 1 || g.info.eventData > 5)
        throw std::runtime_error("invalid active Match and Keep board");
    result["available"] = true;
    result["attempts_remaining"] = g.info.eventData;
    result["tiles"] = nlohmann::json::array();
    for (int i = 0; i < 12; ++i) {
        const auto& tile = g.info.toSelectCards[i];
        nlohmann::json row = {{"index", i}, {"removed", tile.deckIdx == 0}, {"known", tile.deckIdx >= 0}};
        if (tile.deckIdx >= 0)
            row["card"] = {{"id", getCardEnumName(tile.card.id)},
                {"upgrades", tile.card.getUpgraded()}, {"misc", tile.card.misc}};
        result["tiles"].push_back(row);
    }
    return result;
}
}
