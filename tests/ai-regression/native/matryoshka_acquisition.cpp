#include "game/GameContext.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace sts;

int main() {
    int checks = 0;
    auto check = [&](bool ok) {
        if (!ok) throw std::runtime_error("Matryoshka check " + std::to_string(checks + 1));
        ++checks;
    };
    try {
        for (bool shop : {false, true}) {
            GameContext g(CharacterClass::IRONCLAD, 6173, 10);
            if (shop) {
                g.curRoom = Room::SHOP;
                g.screenState = ScreenState::SHOP_ROOM;
                g.info.shop.setup(g);
                g.gold = 240;
                g.info.shop.relics[0] = RelicId::MATRYOSHKA;
                g.info.shop.relicPrice(0) = 198;
                g.info.shop.buyRelic(g, 0);
                check(g.gold == 42);
            } else {
                check(!g.obtainRelic(RelicId::MATRYOSHKA));
            }
            check(g.hasRelic(RelicId::MATRYOSHKA));
            check(g.relics.getRelicValue(RelicId::MATRYOSHKA) == 2);
            for (int chest = 0; chest < 3; ++chest) {
                g.curRoom = Room::TREASURE;
                g.info.haveGold = false;
                g.info.tier = RelicTier::COMMON;
                g.openTreasureRoomChest();
                check(g.info.rewardsContainer.relicCount == (chest < 2 ? 2 : 1));
                check(g.relics.getRelicValue(RelicId::MATRYOSHKA) == (chest == 0 ? 1 : 0));
                // Duplicate acquisition cannot replenish an owned relic.
                check(!g.obtainRelic(RelicId::MATRYOSHKA));
                check(g.relics.getRelicValue(RelicId::MATRYOSHKA) == (chest == 0 ? 1 : 0));
            }
        }
        GameContext control(CharacterClass::IRONCLAD, 6173, 10);
        control.curRoom = Room::TREASURE;
        control.info.haveGold = false;
        control.info.tier = RelicTier::COMMON;
        control.openTreasureRoomChest();
        check(control.info.rewardsContainer.relicCount == 1);
        std::cout << "{\"complete\":true,\"checks\":" << checks << "}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
