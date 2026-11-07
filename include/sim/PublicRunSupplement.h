#pragma once
#include "game/Deck.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace spire_run_public {
inline constexpr const char* version = "public-run-supplement-v1";

// Only master-deck identity and player-visible bottle assignments. No GameContext
// is accepted, so RNG streams and future encounter/relic pools are inaccessible.
inline nlohmann::json capture(const sts::Deck& deck) {
    for (int index : deck.bottleIdxs)
        if (index < -1 || index >= deck.size()) throw std::runtime_error("invalid bottled deck index");
    auto cards = nlohmann::json::array();
    for (int i = 0; i < deck.size(); ++i) {
        const auto& card = deck.cards[i];
        cards.push_back({{"id", sts::getCardEnumName(card.getId())},
            {"upgrades", card.getUpgraded()}, {"misc", card.misc},
            {"bottled_attack", deck.bottleIdxs[static_cast<int>(sts::CardType::ATTACK)] == i},
            {"bottled_skill", deck.bottleIdxs[static_cast<int>(sts::CardType::SKILL)] == i},
            {"bottled_power", deck.bottleIdxs[static_cast<int>(sts::CardType::POWER)] == i}});
    }
    return {{"version", version}, {"deck", cards}};
}
}
