#pragma once
#include "game/GameContext.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace spire_shop_public {
inline constexpr const char* version = "public-shop-shelf-v1";

// All currently displayed goods, including unaffordable ones. Never generate
// stock, inspect a pool, or expose anything while the shop is not on screen.
inline nlohmann::json capture(const sts::GameContext& game) {
    using namespace sts;
    nlohmann::json out{{"version", version}, {"available", false}};
    if (game.screenState != ScreenState::SHOP_ROOM) return out;
    auto items = nlohmann::json::array();
    const auto& shop = game.info.shop;
    auto price = [](int value) {
        if (value < -1 || value > 1000000) throw std::runtime_error("invalid shop price");
        return value;
    };
    for (int i=0; i<7; ++i) if (price(shop.cardPrice(i)) >= 0)
        items.push_back({{"kind", "buy_card"}, {"slot", i}, {"price", shop.cardPrice(i)},
            {"card", {{"id", getCardEnumName(shop.cards[i].id)}, {"upgrades", shop.cards[i].getUpgraded()}}}});
    for (int i=0; i<3; ++i) {
        if (price(shop.relicPrice(i)) >= 0)
            items.push_back({{"kind", "buy_relic"}, {"slot", i}, {"price", shop.relicPrice(i)},
                {"item_id", getRelicName(shop.relics[i])}});
        if (price(shop.potionPrice(i)) >= 0)
            items.push_back({{"kind", "buy_potion"}, {"slot", i}, {"price", shop.potionPrice(i)},
                {"item_id", getPotionName(shop.potions[i])}});
    }
    out.update({{"available", true}, {"complete", true}, {"act", game.act},
        {"floor", game.floorNum}, {"items", items},
        {"removal_price", price(shop.removeCost) < 0 ? nlohmann::json(nullptr) : nlohmann::json(shop.removeCost)}});
    return out;
}
}
