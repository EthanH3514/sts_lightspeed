// Reward acquisition must preserve its parent menu across a nested screen.
#include "sim/ConsoleSimulator.h"
#include "sim/search/GameAction.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
using namespace sts;
int checks = 0;
void check(bool ok, const char *why) { ++checks; if (!ok) throw std::runtime_error(why); }
void claim(GameContext &g, int idx, bool native) {
    if (native) search::GameAction(search::GameAction::RewardsActionType::RELIC, idx).execute(g);
    else { ConsoleSimulator c; c.gc = &g; c.takeAction("relic " + std::to_string(idx)); }
}
int main() { try {
    for (bool native : {false, true}) {
        GameContext g(CharacterClass::IRONCLAD, 1234, 20);
        g.curRoom = Room::TREASURE;
        g.regainControlAction = [](GameContext &x) { x.screenState = ScreenState::MAP_SCREEN; };
        Rewards reward;
        reward.addGold(57);
        reward.addRelic(RelicId::ANCHOR);
        reward.addRelic(RelicId::CAULDRON);
        reward.sapphireKey = true;
        g.openCombatRewardScreen(reward);
        claim(g, 0, native);
        check(g.screenState == ScreenState::REWARDS && g.info.rewardsContainer.sapphireKey,
              "unlinked relic preserves sapphire alternative");
        check(g.info.rewardsContainer.relicCount == 1, "screenless claim removed exactly once");
        claim(g, 0, native);
        check(g.screenState == ScreenState::REWARDS && g.info.rewardsContainer.potionCount == 5,
              "nested reward is exposed without erasing its first item");
        auto copy = g;
        ConsoleSimulator c; c.gc = &g; c.takeAction("skip");
        check(g.screenState == ScreenState::REWARDS && g.info.rewardsContainer.gold[0] == 57,
              "closing child returns to parent reward");
        check(g.info.rewardsContainer.relicCount == 0 && !g.info.rewardsContainer.sapphireKey,
              "claim linked relic removes key and cannot claim twice");
        check(copy.info.rewardsContainer.potionCount == 5 && copy.info.rewardsContainer.goldRewardCount == 0,
              "parent restoration is isolated from cloned child");
        const auto gold = g.gold; c.takeAction("gold 0"); c.takeAction("proceed");
        check(g.gold == gold + 57 && g.screenState == ScreenState::MAP_SCREEN,
              "parent reward and original exit remain executable");
        c.gc = &copy; c.takeAction("skip"); c.takeAction("gold 0"); c.takeAction("proceed");
        check(copy.gold == g.gold && copy.screenState == ScreenState::MAP_SCREEN,
              "copied callback independently restores equivalent rewards");
    }
    std::cout << nlohmann::json{{"checks", checks}, {"failures", nlohmann::json::array()}}.dump();
    return 0;
} catch (const std::exception &e) {
    std::cout << nlohmann::json{{"checks", checks}, {"error", e.what()}}.dump();
    return 1;
} }
