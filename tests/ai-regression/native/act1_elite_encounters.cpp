#include <array>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;

namespace {

void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct EliteFixture {
    GameContext game;
    BattleContext battle;

    EliteFixture(MonsterEncounter encounter, std::uint64_t seed = 123456789)
        : game(CharacterClass::IRONCLAD, seed, 0) {
        game.floorNum = 8;
        game.enterBattle(encounter);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL,
                "elite fixture is not at player control");
    }
};

using PublicMonster = std::tuple<
    MonsterId, int, int, int, MMID, MMID, std::uint64_t,
    int, int, int, int, int, int>;

std::vector<PublicMonster> publicMonsters(const BattleContext &battle) {
    std::vector<PublicMonster> result;
    for (int idx = 0; idx < battle.monsters.monsterCount; ++idx) {
        const auto &monster = battle.monsters.arr[idx];
        result.emplace_back(
            monster.id, monster.curHp, monster.maxHp, monster.block,
            monster.moveHistory[0], monster.moveHistory[1], monster.statusBits,
            monster.artifact, monster.metallicize, monster.strength,
            monster.vulnerable, monster.weak, monster.miscInfo);
    }
    return result;
}

void sampleWithoutChangingPublicRoot(EliteFixture &fixture) {
    const std::array<std::uint64_t, 7> sampleSeeds {11, 22, 33, 44, 55, 66, 77};
    const auto before = publicMonsters(fixture.battle);
    BattleContext sampled(fixture.battle);
    public_sampling::resampleCombatContinuation(fixture.game, sampled, sampleSeeds);
    require(publicMonsters(sampled) == before,
            "resampling changed an elite public monster root");
}

void endTurn(BattleContext &battle) {
    search::Action action(search::ActionType::END_TURN);
    require(action.isValidAction(battle), "end turn is not legal");
    action.execute(battle);
    require(battle.inputState == InputState::PLAYER_NORMAL,
            "enemy turn did not return control");
}

void isolate(BattleContext &battle, int activeIdx, MMID current, MMID previous) {
    for (int idx = 0; idx < battle.monsters.monsterCount; ++idx) {
        if (idx != activeIdx) battle.monsters.arr[idx].curHp = 0;
    }
    auto &monster = battle.monsters.arr[activeIdx];
    monster.moveHistory[0] = current;
    monster.moveHistory[1] = previous;
    battle.player.curHp = battle.player.maxHp;
    battle.player.block = 0;
}

void setHand(BattleContext &battle, CardInstance card) {
    battle.cards.cardsInHand = 1;
    card.setUniqueId(100);
    battle.cards.hand[0] = card;
}

void play(BattleContext &battle, int target = 0) {
    const auto card = battle.cards.hand[0];
    require(card.canUse(battle, target, false), "fixture card is not playable");
    battle.addToBotCard(CardQueueItem(card, target, battle.player.energy));
    battle.inputState = InputState::EXECUTING_ACTIONS;
    battle.executeActions();
}

int dazedCount(const BattleContext &battle) {
    int count = 0;
    for (const auto &card : battle.cards.discardPile) {
        if (card.id == CardId::DAZED) ++count;
    }
    return count;
}

void verifyGremlinNob() {
    EliteFixture root(MonsterEncounter::GREMLIN_NOB);
    require(root.battle.monsters.monsterCount == 1, "Nob composition changed");
    require(root.battle.monsters.arr[0].moveHistory[0] == MMID::GREMLIN_NOB_BELLOW,
            "Nob opening intent changed");
    sampleWithoutChangingPublicRoot(root);

    EliteFixture bellow(MonsterEncounter::GREMLIN_NOB);
    isolate(bellow.battle, 0, MMID::GREMLIN_NOB_BELLOW, MMID::INVALID);
    endTurn(bellow.battle);
    auto &enraged = bellow.battle.monsters.arr[0];
    require(enraged.getStatus<MS::ENRAGE>() == 2, "Nob Bellow Enrage changed");
    setHand(bellow.battle, CardInstance(CardId::DEFEND_RED));
    play(bellow.battle);
    require(enraged.getStatus<MS::STRENGTH>() == 2,
            "Nob Enrage did not react to a public skill play");

    EliteFixture rush(MonsterEncounter::GREMLIN_NOB);
    isolate(rush.battle, 0, MMID::GREMLIN_NOB_RUSH, MMID::GREMLIN_NOB_BELLOW);
    endTurn(rush.battle);
    require(rush.battle.player.maxHp - rush.battle.player.curHp == 14,
            "Nob Rush damage changed");

    EliteFixture bash(MonsterEncounter::GREMLIN_NOB);
    isolate(bash.battle, 0, MMID::GREMLIN_NOB_SKULL_BASH, MMID::GREMLIN_NOB_RUSH);
    endTurn(bash.battle);
    require(bash.battle.player.maxHp - bash.battle.player.curHp == 6,
            "Nob Skull Bash damage changed");
    require(bash.battle.player.getStatus<PS::VULNERABLE>() == 2,
            "Nob Skull Bash Vulnerable changed");
}

void verifyLagavulin() {
    EliteFixture root(MonsterEncounter::LAGAVULIN);
    auto &sleeping = root.battle.monsters.arr[0];
    require(sleeping.hasStatus<MS::ASLEEP>(), "Lagavulin no longer starts asleep");
    require(sleeping.getStatus<MS::METALLICIZE>() == 8 && sleeping.block == 8,
            "Lagavulin sleeping defense changed");
    require(sleeping.moveHistory[0] == MMID::LAGAVULIN_SLEEP,
            "Lagavulin opening intent changed");
    sampleWithoutChangingPublicRoot(root);
    sleeping.block = 0;
    sleeping.attacked(root.battle, 1);
    require(!sleeping.hasStatus<MS::ASLEEP>() &&
            sleeping.getStatus<MS::METALLICIZE>() == 0,
            "public damage did not wake Lagavulin");

    EliteFixture cycle(MonsterEncounter::LAGAVULIN);
    auto &monster = cycle.battle.monsters.arr[0];
    monster.removeStatus<MS::ASLEEP>();
    monster.removeStatus<MS::METALLICIZE>();
    monster.block = 0;
    isolate(cycle.battle, 0, MMID::LAGAVULIN_ATTACK, MMID::LAGAVULIN_SLEEP);
    endTurn(cycle.battle);
    require(cycle.battle.player.maxHp - cycle.battle.player.curHp == 18,
            "Lagavulin first attack damage changed");
    require(monster.moveHistory[0] == MMID::LAGAVULIN_ATTACK,
            "Lagavulin did not schedule its second attack");
    cycle.battle.player.curHp = cycle.battle.player.maxHp;
    endTurn(cycle.battle);
    require(cycle.battle.player.maxHp - cycle.battle.player.curHp == 18,
            "Lagavulin second attack damage changed");
    require(monster.moveHistory[0] == MMID::LAGAVULIN_SIPHON_SOUL,
            "Lagavulin did not schedule Siphon Soul");
    cycle.battle.player.curHp = cycle.battle.player.maxHp;
    endTurn(cycle.battle);
    require(cycle.battle.player.getStatus<PS::DEXTERITY>() == -1 &&
            cycle.battle.player.getStatus<PS::STRENGTH>() == -1,
            "Lagavulin Siphon Soul debuffs changed");
    require(monster.moveHistory[0] == MMID::LAGAVULIN_ATTACK,
            "Lagavulin did not restart its attack cycle");
}

void verifySentries() {
    EliteFixture root(MonsterEncounter::THREE_SENTRIES);
    require(root.battle.monsters.monsterCount == 3, "Sentry composition changed");
    const MMID expected[] {MMID::SENTRY_BOLT, MMID::SENTRY_BEAM, MMID::SENTRY_BOLT};
    for (int idx = 0; idx < 3; ++idx) {
        const auto &monster = root.battle.monsters.arr[idx];
        require(monster.moveHistory[0] == expected[idx],
                "Sentry opening intent order changed");
        require(monster.getStatus<MS::ARTIFACT>() == 1,
                "Sentry Artifact changed");
    }
    sampleWithoutChangingPublicRoot(root);

    EliteFixture beam(MonsterEncounter::THREE_SENTRIES);
    isolate(beam.battle, 1, MMID::SENTRY_BEAM, MMID::INVALID);
    endTurn(beam.battle);
    require(beam.battle.player.maxHp - beam.battle.player.curHp == 9,
            "Sentry Beam damage changed");
    require(beam.battle.monsters.arr[1].moveHistory[0] == MMID::SENTRY_BOLT,
            "Sentry Beam did not alternate to Bolt");

    EliteFixture bolt(MonsterEncounter::THREE_SENTRIES);
    isolate(bolt.battle, 0, MMID::SENTRY_BOLT, MMID::INVALID);
    const int before = dazedCount(bolt.battle);
    endTurn(bolt.battle);
    require(dazedCount(bolt.battle) - before == 2,
            "Sentry Bolt Dazed insertion changed");
    require(bolt.battle.monsters.arr[0].moveHistory[0] == MMID::SENTRY_BEAM,
            "Sentry Bolt did not alternate to Beam");
}

void verifyRecipeRejections() {
    const std::array<std::uint64_t, 7> seeds {1, 2, 3, 4, 5, 6, 7};

    EliteFixture malformed(MonsterEncounter::THREE_SENTRIES);
    malformed.battle.monsters.arr[0].id = MonsterId::CULTIST;
    bool malformedRejected = false;
    try {
        public_sampling::resampleCombatContinuation(
            malformed.game, malformed.battle, seeds);
    } catch (const std::runtime_error &) {
        malformedRejected = true;
    }
    require(malformedRejected, "resampler accepted malformed Sentries");

    EliteFixture history(MonsterEncounter::THREE_SENTRIES);
    history.battle.monsters.arr[0].moveHistory[1] = MMID::SENTRY_BOLT;
    bool historyRejected = false;
    try {
        public_sampling::resampleCombatContinuation(history.game, history.battle, seeds);
    } catch (const std::runtime_error &) {
        historyRejected = true;
    }
    require(historyRejected, "resampler accepted non-alternating Sentry history");

    for (const auto encounter : {
        MonsterEncounter::GREMLIN_NOB,
        MonsterEncounter::LAGAVULIN,
        MonsterEncounter::THREE_SENTRIES,
    }) {
        EliteFixture unsupported(encounter);
        unsupported.battle.ascension = 1;
        bool ascensionRejected = false;
        try {
            public_sampling::resampleCombatContinuation(
                unsupported.game, unsupported.battle, seeds);
        } catch (const std::runtime_error &) {
            ascensionRejected = true;
        }
        require(ascensionRejected, "resampler accepted an unaudited elite ascension");
    }
}

}  // namespace

int main() {
    try {
        verifyGremlinNob();
        verifyLagavulin();
        verifySentries();
        verifyRecipeRejections();
        std::cout << "ACT1_ELITE_ENCOUNTERS_OK (3 encounters, 8 moves)\n";
    } catch (const std::exception &error) {
        std::cerr << "ACT1_ELITE_ENCOUNTERS_FAILED: " << error.what() << '\n';
        return 1;
    }
}
