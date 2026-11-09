#pragma once
#include "game/GameContext.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace sts::public_deck_selector {
inline nlohmann::json capture(ScreenState screen, const Deck& deck, const ScreenStateInfo& info) {
    using J = nlohmann::json;
    J result = {{"version", "public-deck-selector-v1"}, {"available", false}};
    if (screen != ScreenState::CARD_SELECT) return result;
    const char* task = nullptr;
    if (info.selectScreenType == CardSelectScreenType::UPGRADE) task = "upgrade";
    if (info.selectScreenType == CardSelectScreenType::REMOVE) task = "remove";
    if (!task) return result; // Other selectors can contain newly generated cards.
    auto cards = J::array();
    for (int i = 0; i < info.toSelectCards.size(); ++i) {
        const auto& item = info.toSelectCards[i];
        int index = item.deckIdx;
        if (index < 0 || index >= deck.size()) throw std::runtime_error("invalid public selector deck index");
        const auto& card = deck.cards[index];
        if (item.card.id != card.id || item.card.getUpgraded() != card.getUpgraded() || item.card.misc != card.misc)
            throw std::runtime_error("stale public selector card");
        cards.push_back({{"option_index", i}, {"deck_index", index},
            {"card", {{"id", getCardEnumName(card.id)}, {"upgrades", card.getUpgraded()}, {"misc", card.misc}}}});
    }
    result.update({{"available", true}, {"task", task}, {"selected_count", info.haveSelectedCards.size()},
        {"selection_count", info.toSelectCount}, {"cards", cards}});
    return result;
}
}
