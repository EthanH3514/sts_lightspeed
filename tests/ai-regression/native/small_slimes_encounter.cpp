#include <array>
#include <iostream>
#include <stdexcept>
#include <tuple>

#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct SlimeFixture {
    GameContext game;
    BattleContext battle;

    explicit SlimeFixture(std::uint64_t seed)
        : game(CharacterClass::IRONCLAD, seed, 0) {
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::SMALL_SLIMES);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "Small Slimes fixture is not at player control");
    }
};

int findMonster(const BattleContext &battle, MonsterId id) {
    for (int idx = 0; idx < battle.monsters.monsterCount; ++idx) {
        if (battle.monsters.arr[idx].id == id) return idx;
    }
    return -1;
}

int slimedCount(const BattleContext &battle) {
    int count = 0;
    for (const auto &card : battle.cards.discardPile) {
        if (card.id == CardId::SLIMED) ++count;
    }
    return count;
}

void isolate(BattleContext &battle, int activeIdx, MMID move) {
    for (int idx = 0; idx < battle.monsters.monsterCount; ++idx) {
        if (idx != activeIdx) battle.monsters.arr[idx].curHp = 0;
    }
    auto &monster = battle.monsters.arr[activeIdx];
    monster.moveHistory[0] = move;
    monster.moveHistory[1] = MMID::INVALID;
    battle.player.curHp = battle.player.maxHp;
    battle.player.block = 0;
}

void endTurn(BattleContext &battle) {
    search::Action action(search::ActionType::END_TURN);
    require(action.isValidAction(battle), "end turn is not legal");
    action.execute(battle);
    require(battle.inputState == InputState::PLAYER_NORMAL,
            "enemy turn did not return control");
}

template <typename Configure, typename Verify>
void checkMove(std::uint64_t seed, MonsterId id, MMID move,
               Configure configure, Verify verify) {
    SlimeFixture fixture(seed);
    const int idx = findMonster(fixture.battle, id);
    require(idx >= 0, "requested slime composition was not found");
    isolate(fixture.battle, idx, move);
    configure(fixture.battle);
    endTurn(fixture.battle);
    verify(fixture.battle, fixture.battle.monsters.arr[idx]);
}

std::pair<std::uint64_t, std::uint64_t> findCompositionSeeds() {
    std::uint64_t acidSmallSeed = 0;
    std::uint64_t spikeSmallSeed = 0;
    for (std::uint64_t seed = 1; seed <= 256 && (!acidSmallSeed || !spikeSmallSeed); ++seed) {
        SlimeFixture fixture(seed);
        if (findMonster(fixture.battle, MonsterId::ACID_SLIME_S) >= 0) {
            acidSmallSeed = seed;
        }
        if (findMonster(fixture.battle, MonsterId::SPIKE_SLIME_S) >= 0) {
            spikeSmallSeed = seed;
        }
    }
    require(acidSmallSeed != 0 && spikeSmallSeed != 0,
            "did not realize both public Small Slimes compositions");
    return {acidSmallSeed, spikeSmallSeed};
}

void verifyPublicCompositionAndSampling(std::uint64_t acidSmallSeed,
                                        std::uint64_t spikeSmallSeed) {
    const std::array<std::uint64_t, 7> sampleSeeds {11, 22, 33, 44, 55, 66, 77};
    for (const auto seed : {acidSmallSeed, spikeSmallSeed}) {
        SlimeFixture fixture(seed);
        const auto before = std::make_tuple(
            fixture.battle.monsters.arr[0].id,
            fixture.battle.monsters.arr[0].curHp,
            fixture.battle.monsters.arr[0].maxHp,
            fixture.battle.monsters.arr[0].moveHistory[0],
            fixture.battle.monsters.arr[1].id,
            fixture.battle.monsters.arr[1].curHp,
            fixture.battle.monsters.arr[1].maxHp,
            fixture.battle.monsters.arr[1].moveHistory[0]);
        BattleContext sampled(fixture.battle);
        public_sampling::resampleCombatContinuation(fixture.game, sampled, sampleSeeds);
        const auto after = std::make_tuple(
            sampled.monsters.arr[0].id, sampled.monsters.arr[0].curHp,
            sampled.monsters.arr[0].maxHp, sampled.monsters.arr[0].moveHistory[0],
            sampled.monsters.arr[1].id, sampled.monsters.arr[1].curHp,
            sampled.monsters.arr[1].maxHp, sampled.monsters.arr[1].moveHistory[0]);
        require(before == after, "resampling changed a public slime root");
    }

    SlimeFixture malformed(acidSmallSeed);
    malformed.battle.monsters.arr[0].id = MonsterId::CULTIST;
    bool malformedRejected = false;
    try {
        public_sampling::resampleCombatContinuation(
            malformed.game, malformed.battle, sampleSeeds);
    } catch (const std::runtime_error &) {
        malformedRejected = true;
    }
    require(malformedRejected, "resampler accepted a malformed slime composition");

    SlimeFixture impossible(acidSmallSeed);
    const int acidSmall = findMonster(impossible.battle, MonsterId::ACID_SLIME_S);
    require(acidSmall >= 0, "acid-small fixture changed composition");
    auto &monster = impossible.battle.monsters.arr[acidSmall];
    monster.moveHistory[1] = monster.moveHistory[0];
    bool historyRejected = false;
    try {
        public_sampling::resampleCombatContinuation(
            impossible.game, impossible.battle, sampleSeeds);
    } catch (const std::runtime_error &) {
        historyRejected = true;
    }
    require(historyRejected, "resampler accepted impossible Acid Slime history");

    SlimeFixture unsupportedAscension(spikeSmallSeed);
    unsupportedAscension.battle.ascension = 1;
    bool ascensionRejected = false;
    try {
        public_sampling::resampleCombatContinuation(
            unsupportedAscension.game, unsupportedAscension.battle, sampleSeeds);
    } catch (const std::runtime_error &) {
        ascensionRejected = true;
    }
    require(ascensionRejected, "resampler accepted unaudited Small Slimes ascension");
}

void verifySmallSlimeMoves(std::uint64_t acidSmallSeed,
                           std::uint64_t spikeSmallSeed) {
    checkMove(
        acidSmallSeed, MonsterId::ACID_SLIME_S, MMID::ACID_SLIME_S_LICK,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &monster) {
            require(battle.player.getStatus<PS::WEAK>() == 1,
                    "small Acid Slime Lick Weak changed");
            require(monster.moveHistory[0] == MMID::ACID_SLIME_S_TACKLE,
                    "small Acid Slime did not alternate to Tackle");
        });
    checkMove(
        acidSmallSeed, MonsterId::ACID_SLIME_S, MMID::ACID_SLIME_S_TACKLE,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &monster) {
            require(battle.player.maxHp - battle.player.curHp == 3,
                    "small Acid Slime Tackle damage changed");
            require(monster.moveHistory[0] == MMID::ACID_SLIME_S_LICK,
                    "small Acid Slime did not alternate to Lick");
        });
    checkMove(
        spikeSmallSeed, MonsterId::SPIKE_SLIME_S, MMID::SPIKE_SLIME_S_TACKLE,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &monster) {
            require(battle.player.maxHp - battle.player.curHp == 5,
                    "small Spike Slime Tackle damage changed");
            require(monster.moveHistory[0] == MMID::SPIKE_SLIME_S_TACKLE,
                    "small Spike Slime intent changed");
        });
}

void verifyMediumSlimeMoves(std::uint64_t acidSmallSeed,
                            std::uint64_t spikeSmallSeed) {
    checkMove(
        spikeSmallSeed, MonsterId::ACID_SLIME_M,
        MMID::ACID_SLIME_M_CORROSIVE_SPIT,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &) {
            require(battle.player.maxHp - battle.player.curHp == 7,
                    "medium Acid Slime Corrosive Spit damage changed");
            require(slimedCount(battle) == 1,
                    "medium Acid Slime did not add one Slimed");
        });
    checkMove(
        spikeSmallSeed, MonsterId::ACID_SLIME_M, MMID::ACID_SLIME_M_LICK,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &) {
            require(battle.player.getStatus<PS::WEAK>() == 1,
                    "medium Acid Slime Lick Weak changed");
        });
    checkMove(
        spikeSmallSeed, MonsterId::ACID_SLIME_M, MMID::ACID_SLIME_M_TACKLE,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &) {
            require(battle.player.maxHp - battle.player.curHp == 10,
                    "medium Acid Slime Tackle damage changed");
        });
    checkMove(
        acidSmallSeed, MonsterId::SPIKE_SLIME_M, MMID::SPIKE_SLIME_M_LICK,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &) {
            require(battle.player.getStatus<PS::FRAIL>() == 1,
                    "medium Spike Slime Lick Frail changed");
        });
    checkMove(
        acidSmallSeed, MonsterId::SPIKE_SLIME_M,
        MMID::SPIKE_SLIME_M_FLAME_TACKLE,
        [](BattleContext &) {},
        [](const BattleContext &battle, const Monster &) {
            require(battle.player.maxHp - battle.player.curHp == 8,
                    "medium Spike Slime Flame Tackle damage changed");
            require(slimedCount(battle) == 1,
                    "medium Spike Slime did not add one Slimed");
        });
}

}  // namespace

int main() {
    try {
        const auto [acidSmallSeed, spikeSmallSeed] = findCompositionSeeds();
        verifyPublicCompositionAndSampling(acidSmallSeed, spikeSmallSeed);
        verifySmallSlimeMoves(acidSmallSeed, spikeSmallSeed);
        verifyMediumSlimeMoves(acidSmallSeed, spikeSmallSeed);
        std::cout << "SMALL_SLIMES_ENCOUNTER_OK (2 compositions, 8 moves)\n";
    } catch (const std::exception &error) {
        std::cerr << "SMALL_SLIMES_ENCOUNTER_FAILED: " << error.what() << '\n';
        return 1;
    }
}
