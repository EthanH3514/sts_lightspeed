#include <iostream>
#include <memory>
#include <stdexcept>
#include <tuple>

#include "game/GameContext.h"

using namespace sts;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
auto rngWords(const Random &rng) {
    return std::make_tuple(rng.counter, rng.seed0, rng.seed1);
}
auto generationState(const GameContext &gc) {
    return std::make_tuple(
        rngWords(gc.cardRng), rngWords(gc.potionRng), rngWords(gc.miscRng),
        gc.cardRarityFactor, gc.potionChance
    );
}
GameContext cloneGame(const GameContext &gc) {
    GameContext clone(gc);
    if (gc.map) clone.map = std::make_shared<Map>(*gc.map);
    return clone;
}
void eventCallbackIsIndependent(Event event) {
    GameContext source(CharacterClass::IRONCLAD, 123456789, 0);
    source.curEvent = event;
    if (event == Event::DEAD_ADVENTURER) {
        source.info.phase = 2;
        source.info.rewards[2] = 2;
        source.info.encounter = MonsterEncounter::LAGAVULIN_EVENT;
        for (std::uint64_t seed = 1; ; ++seed) {
            Random candidate(seed);
            if (candidate.random(99) < 75) {
                source.miscRng = Random(seed);
                break;
            }
        }
    }
    source.chooseEventOption(0);
    require(source.screenState == ScreenState::BATTLE, "fixture did not enter battle");
    const auto sourceBefore = generationState(source);
    auto branch = cloneGame(source);
    auto twin = cloneGame(source);
    branch.regainControl();
    twin.regainControl();
    require(generationState(source) == sourceBefore, "event callback mutated source RNG");
    require(source.screenState == ScreenState::BATTLE, "event callback changed source screen");
    require(branch.screenState == ScreenState::REWARDS, "event callback did not open branch reward");
    require(generationState(branch) == generationState(twin), "copied callbacks are not deterministic");
}
void bossCallbackUsesBranchAct() {
    GameContext source(CharacterClass::IRONCLAD, 123456789, 0);
    source.enterBossTreasureRoom();
    auto branch = cloneGame(source);
    source.act = 3;
    branch.regainControl();
    require(branch.act == 2, "boss callback read source act instead of branch act");
    require(source.act == 3, "boss callback mutated source act");
}
void shopCallbackRetainsOnlyReturnAction() {
    GameContext source(CharacterClass::IRONCLAD, 123456789, 0);
    source.regainControlAction = [](GameContext &gc) { gc.gold += 7; };
    source.info.shop.removeCost = 0;
    source.info.shop.buyCardRemove(source);
    auto branch = cloneGame(source);
    const int sourceGold = source.gold;
    branch.regainControl();
    require(branch.screenState == ScreenState::SHOP_ROOM, "shop callback did not return to shop");
    branch.regainControl();
    require(branch.gold == sourceGold + 7, "shop callback lost previous return action");
    require(source.gold == sourceGold, "shop callback mutated source gold");
}
}
int main() {
    try {
        bossCallbackUsesBranchAct();
        eventCallbackIsIndependent(Event::HYPNOTIZING_COLORED_MUSHROOMS);
        eventCallbackIsIndependent(Event::MINDBLOOM);
        eventCallbackIsIndependent(Event::DEAD_ADVENTURER);
        shopCallbackRetainsOnlyReturnAction();
        std::cout << "EXACT_CLONE_CALLBACKS_OK (5 fixtures)\n";
    } catch (const std::exception &error) {
        std::cerr << "EXACT_CLONE_CALLBACKS_FAILED: " << error.what() << '\n';
        return 1;
    }
}
