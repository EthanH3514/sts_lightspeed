#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "constants/MonsterStatusEffects.h"

using namespace sts;

namespace {
void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

void verifyNames() {
    // Explicit enum/name pairs: an insertion or reordering must not silently pass.
#define STATUS(name) std::pair<MS, const char *>{MS::name, #name}
    const auto expected = {
        STATUS(ARTIFACT), STATUS(BLOCK_RETURN), STATUS(CHOKED),
        STATUS(CORPSE_EXPLOSION), STATUS(LOCK_ON), STATUS(MARK),
        STATUS(METALLICIZE), STATUS(PLATED_ARMOR), STATUS(POISON), STATUS(REGEN),
        STATUS(SHACKLED), STATUS(STRENGTH), STATUS(VULNERABLE), STATUS(WEAK),
        STATUS(ANGRY), STATUS(BEAT_OF_DEATH), STATUS(CURIOSITY), STATUS(CURL_UP),
        STATUS(ENRAGE), STATUS(FADING), STATUS(FLIGHT), STATUS(GENERIC_STRENGTH_UP),
        STATUS(INTANGIBLE), STATUS(MALLEABLE), STATUS(MODE_SHIFT), STATUS(RITUAL),
        STATUS(SLOW), STATUS(SPORE_CLOUD), STATUS(THIEVERY), STATUS(THORNS),
        STATUS(TIME_WARP), STATUS(INVINCIBLE), STATUS(REACTIVE), STATUS(SHARP_HIDE),
        STATUS(ASLEEP), STATUS(BARRICADE), STATUS(MINION), STATUS(MINION_LEADER),
        STATUS(PAINFUL_STABS), STATUS(REGROW), STATUS(SHIFTING), STATUS(STASIS),
        STATUS(INVALID),
    };
#undef STATUS
    const auto count = static_cast<std::size_t>(MS::INVALID) + 1;
    require(expected.size() == count, "enum coverage changed");
    require(std::size(monsterStatusEnumStrings) == count, "name table size changed");
    require(std::size(enemyStatusStrings) == count, "display table size changed");
    std::size_t index = 0;
    for (const auto &entry : expected) {
        require(static_cast<std::size_t>(entry.first) == index, "enum order changed");
        require(std::string(monsterStatusEnumStrings[index]) == entry.second,
                std::string(entry.second) + " exported as " + monsterStatusEnumStrings[index]);
        ++index;
    }
}

void verifySleepWake() {
    GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
    game.floorNum = 8;
    game.enterBattle(MonsterEncounter::LAGAVULIN);
    BattleContext battle;
    battle.init(game);
    battle.executeActions();
    auto &monster = battle.monsters.arr[0];
    require(monster.getStatusInternal(MS::ASLEEP) == 1, "missing native sleep");
    require(monster.getStatusInternal(MS::BARRICADE) == 0, "unexpected native Barricade");
    require(std::string(monsterStatusEnumStrings[static_cast<int>(MS::ASLEEP)]) == "ASLEEP",
            "sleep export name mismatch");
    const auto hp = monster.curHp;
    monster.attacked(battle, 1);
    require(monster.curHp == hp && monster.getStatusInternal(MS::ASLEEP) == 1,
            "blocked damage woke sleeping monster");
    monster.block = 0;
    monster.attacked(battle, 1);
    require(monster.curHp == hp - 1 && monster.getStatusInternal(MS::ASLEEP) == 0,
            "HP damage did not clear sleep");
    require(monster.getStatusInternal(MS::BARRICADE) == 0, "wake introduced Barricade");
    require(monster.getStatusInternal(MS::METALLICIZE) == 0, "sleep Metallicize retained");
}
}

int main() {
    try {
        verifyNames();
        verifySleepWake();
        std::cout << "MONSTER_STATUS_NAMES_OK (43 enum entries, sleep/blocked-hit/wake)\n";
    } catch (const std::exception &error) {
        std::cerr << "MONSTER_STATUS_NAMES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
