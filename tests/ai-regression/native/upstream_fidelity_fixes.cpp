#include <array>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"

using namespace sts;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

BattleContext start(
        std::initializer_list<RelicId> relics,
        Card card = Card(CardId::FLEX),
        int count = 10,
        MonsterEncounter encounter = MonsterEncounter::JAW_WORM) {
    GameContext gc(CharacterClass::IRONCLAD, 100123, 20);
    gc.floorNum = 1;
    gc.curRoom = Room::MONSTER;
    gc.relics = {};
    for (const auto relic : relics) {
        gc.relics.add({relic, 0});
    }
    gc.deck = {};
    for (int i = 0; i < count; ++i) {
        gc.deck.obtainRaw(card);
    }

    BattleContext bc;
    bc.init(gc, encounter);
    require(bc.inputState == InputState::PLAYER_NORMAL, "expected player control");
    return bc;
}

void play(BattleContext &bc, int index = 0) {
    search::Action action(search::ActionType::CARD, index, 0);
    require(action.isValidAction(bc), "test attempted an illegal card action");
    action.execute(bc);
}

void endTurn(BattleContext &bc) {
    search::Action(search::ActionType::END_TURN).execute(bc);
}

void testRedMask() {
    auto bc = start({RelicId::RED_MASK});
    require(bc.monsters.arr[0].getStatus<MS::WEAK>() == 1,
            "Red Mask must apply one Weak");
    endTurn(bc);
    require(bc.monsters.arr[0].getStatus<MS::WEAK>() == 0,
            "Red Mask Weak must expire after the first enemy turn");
    endTurn(bc);
    require(bc.monsters.arr[0].getStatus<MS::WEAK>() == 0,
            "Red Mask must not reapply Weak");

    auto sentries = start(
            {RelicId::RED_MASK}, Card(CardId::FLEX), 10,
            MonsterEncounter::THREE_SENTRIES);
    for (int i = 0; i < sentries.monsters.monsterCount; ++i) {
        require(sentries.monsters.arr[i].getStatus<MS::WEAK>() == 0,
                "Artifact must block Red Mask Weak");
        require(sentries.monsters.arr[i].getStatus<MS::ARTIFACT>() == 0,
                "Red Mask Weak must consume Artifact");
    }
}

void testVelvetChoker() {
    auto chain = start({RelicId::VELVET_CHOKER}, Card(CardId::HAVOC, true), 20);
    play(chain);
    require(chain.player.cardsPlayedThisTurn == 6,
            "Velvet Choker must stop Havoc autoplay at six cards");
    require(chain.actionQueue.isEmpty() && chain.cardQueue.isEmpty(),
            "stopped autoplay must finish resolving");

    auto unrestricted = start({}, Card(CardId::HAVOC, true), 20);
    play(unrestricted);
    require(unrestricted.player.cardsPlayedThisTurn > 6,
            "Havoc must exceed six cards without Velvet Choker");

    auto replay = start({RelicId::VELVET_CHOKER}, Card(CardId::FLEX), 10);
    for (int i = 0; i < 4; ++i) {
        play(replay);
    }
    replay.cards.createTempCardInHand(CardInstance(CardId::DOUBLE_TAP));
    play(replay, replay.cards.cardsInHand - 1);
    replay.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
    play(replay, replay.cards.cardsInHand - 1);
    require(replay.player.cardsPlayedThisTurn == 7,
            "the sixth card's queued Double Tap copy must still resolve");
}

void testUnceasingTop() {
    auto shuffle = start({RelicId::UNCEASING_TOP}, Card(CardId::FLEX), 5);
    for (int i = 0; i < 5; ++i) {
        play(shuffle);
    }
    require(shuffle.cards.cardsInHand == 1,
            "Unceasing Top must finish shuffle and draw before player control");
    require(shuffle.actionQueue.isEmpty(),
            "Unceasing Top must not leave a pending draw at player control");
    require(shuffle.cards.drawPile.size() == 4 && shuffle.cards.discardPile.empty(),
            "Unceasing Top must draw exactly one shuffled card");

    auto blocked = start({RelicId::UNCEASING_TOP}, Card(CardId::BATTLE_TRANCE), 10);
    play(blocked);
    while (blocked.cards.cardsInHand != 0) {
        play(blocked);
    }
    require(blocked.cards.drawPile.size() == 2 && blocked.actionQueue.isEmpty(),
            "No Draw must stop Unceasing Top without leaving work queued");

    auto empty = start(
            {RelicId::UNCEASING_TOP, RelicId::BRONZE_SCALES},
            Card(CardId::OFFERING), 1);
    play(empty);
    require(empty.inputState == InputState::PLAYER_NORMAL && empty.actionQueue.isEmpty(),
            "Unceasing Top with no drawable cards must return control without looping");
}

int finishBattle(
        int hp,
        int maxHp,
        const std::vector<RelicId> &relics,
        bool victory) {
    GameContext gc(CharacterClass::IRONCLAD, 100123, 20);
    gc.floorNum = 1;
    gc.curRoom = Room::MONSTER;
    gc.curHp = hp;
    gc.maxHp = maxHp;
    gc.relics = {};
    for (const auto relic : relics) {
        gc.relics.add({relic, 0});
    }
    gc.deck = {};
    for (int i = 0; i < 10; ++i) {
        gc.deck.obtainRaw(Card(CardId::STRIKE_RED));
    }

    bool returnedToGame = false;
    gc.regainControlAction = [&returnedToGame](GameContext &) {
        returnedToGame = true;
    };

    BattleContext bc;
    bc.init(gc, MonsterEncounter::JAW_WORM);
    if (victory) {
        bc.monsters.arr[0].curHp = 1;
        bc.monsters.arr[0].block = 0;
        play(bc);
        require(bc.outcome == Outcome::PLAYER_VICTORY,
                "controlled Strike must finish the battle");
    } else {
        bc.player.curHp = 0;
        bc.outcome = Outcome::PLAYER_LOSS;
    }
    bc.exitBattle(gc);
    require(returnedToGame == victory,
            "only victory may return to the game callback");
    return gc.curHp;
}

void testMeatOnTheBone() {
    const std::array<std::vector<RelicId>, 8> profiles = {{
        {},
        {RelicId::MEAT_ON_THE_BONE},
        {RelicId::BURNING_BLOOD},
        {RelicId::BLACK_BLOOD},
        {RelicId::BURNING_BLOOD, RelicId::MEAT_ON_THE_BONE},
        {RelicId::MEAT_ON_THE_BONE, RelicId::BURNING_BLOOD},
        {RelicId::BLACK_BLOOD, RelicId::MEAT_ON_THE_BONE},
        {RelicId::MEAT_ON_THE_BONE, RelicId::BLACK_BLOOD},
    }};
    struct Case {
        int hp;
        int maxHp;
        std::array<int, 8> expected;
    };
    const Case cases[] = {
        {39, 80, {39, 51, 45, 51, 57, 57, 63, 63}},
        {40, 80, {40, 52, 46, 52, 58, 58, 64, 64}},
        {41, 80, {41, 41, 47, 53, 47, 47, 53, 53}},
        {40, 81, {40, 52, 46, 52, 58, 58, 64, 64}},
        {41, 81, {41, 41, 47, 53, 47, 47, 53, 53}},
        {10, 20, {10, 20, 16, 20, 20, 20, 20, 20}},
        {80, 80, {80, 80, 80, 80, 80, 80, 80, 80}},
        {1, 80, {1, 13, 7, 13, 19, 19, 25, 25}},
    };

    int checked = 0;
    for (const auto &test : cases) {
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            require(finishBattle(test.hp, test.maxHp, profiles[i], true) == test.expected[i],
                    "Meat on the Bone victory-healing order mismatch");
            ++checked;
        }
    }
    for (const auto &profile : profiles) {
        require(finishBattle(40, 80, profile, false) == 0,
                "victory relics must not heal a defeat");
        ++checked;
    }
    require(checked == 72, "expected 72 battle-exit cases");
}

void testRedSkull() {
    auto redSkull = start({RelicId::RED_SKULL});
    redSkull.player.maxHp = 80;
    redSkull.player.curHp = 40;
    redSkull.player.strength = 3;
    redSkull.player.heal(5);
    require(redSkull.player.curHp == 45 && redSkull.player.strength == 0,
            "Red Skull must remove three Strength after healing above half HP");

    auto noCrossing = start({RelicId::RED_SKULL});
    noCrossing.player.maxHp = 80;
    noCrossing.player.curHp = 40;
    noCrossing.player.strength = 3;
    noCrossing.player.heal(0);
    require(noCrossing.player.strength == 3,
            "Red Skull must remain active without a threshold crossing");
}

void testMagicFlower() {
    auto odd = start({RelicId::MAGIC_FLOWER});
    odd.player.maxHp = 80;
    odd.player.curHp = 60;
    odd.player.heal(5);
    require(odd.player.curHp == 68,
            "Magic Flower must round 7.5 healing to 8");

    auto even = start({RelicId::MAGIC_FLOWER});
    even.player.maxHp = 80;
    even.player.curHp = 60;
    even.player.heal(4);
    require(even.player.curHp == 66,
            "Magic Flower must preserve exact even healing");
}

void installBlockPotion(BattleContext &bc) {
    bc.potions[0] = Potion::BLOCK_POTION;
    bc.potionCount = 1;
}

void testToyOrnithopter() {
    auto used = start({RelicId::TOY_ORNITHOPTER});
    used.player.maxHp = 80;
    used.player.curHp = 70;
    installBlockPotion(used);
    search::Action(search::ActionType::POTION, 0, 0).execute(used);
    require(used.player.curHp == 75,
            "Toy Ornithopter must heal five HP when a potion is used");

    auto discarded = start({RelicId::TOY_ORNITHOPTER});
    discarded.player.maxHp = 80;
    discarded.player.curHp = 70;
    installBlockPotion(discarded);
    discarded.discardPotion(0);
    require(discarded.player.curHp == 70,
            "discarding a potion must not trigger Toy Ornithopter");
}

void testBufferHpLoss() {
    auto buffered = start({RelicId::FOSSILIZED_HELIX});
    const int before = buffered.player.curHp;
    buffered.player.loseHp(buffered, 5, false);
    require(buffered.player.curHp == before,
            "Buffer must intercept direct positive HP loss");
    require(!buffered.player.hasStatus<PS::BUFFER>(),
            "intercepted direct HP loss must consume Buffer");

    auto bufferedRod = start({RelicId::FOSSILIZED_HELIX, RelicId::TUNGSTEN_ROD});
    const int rodBefore = bufferedRod.player.curHp;
    bufferedRod.player.loseHp(bufferedRod, 5, false);
    require(bufferedRod.player.curHp == rodBefore,
            "Buffer must intercept before Tungsten Rod reduction");

    auto rodOnly = start({RelicId::TUNGSTEN_ROD});
    const int rodOnlyBefore = rodOnly.player.curHp;
    rodOnly.player.loseHp(rodOnly, 5, false);
    require(rodOnly.player.curHp == rodOnlyBefore - 4,
            "Tungsten Rod alone must reduce five direct HP loss to four");
}

BattleContext startGremlinLeaderWithLament(int remaining) {
    GameContext gc(CharacterClass::IRONCLAD, 100123, 20);
    gc.floorNum = 1;
    gc.curRoom = Room::MONSTER;
    gc.relics = {};
    gc.relics.add({RelicId::NEOWS_LAMENT, remaining});
    gc.deck = {};
    for (int i = 0; i < 10; ++i) {
        gc.deck.obtainRaw(Card(CardId::FLEX));
    }
    BattleContext bc;
    bc.init(gc, MonsterEncounter::GREMLIN_LEADER);
    return bc;
}

void testNeowsLament() {
    auto lament = startGremlinLeaderWithLament(1);
    require(lament.monsters.monsterCount == 4,
            "Gremlin Leader must reserve four combat slots");
    require(lament.monsters.arr[0].idx == -1 && lament.monsters.arr[0].curHp == 0,
            "Neow's Lament must not give an uninitialized monster slot one HP");
    for (int i = 1; i < lament.monsters.monsterCount; ++i) {
        require(lament.monsters.arr[i].idx >= 0 && lament.monsters.arr[i].curHp == 1,
                "Neow's Lament must set one HP on actual combat entities");
    }
}

}  // namespace

int main() {
    try {
        testRedMask();
        testVelvetChoker();
        testUnceasingTop();
        testMeatOnTheBone();
        testRedSkull();
        testMagicFlower();
        testToyOrnithopter();
        testBufferHpLoss();
        testNeowsLament();
        std::cout << "UPSTREAM_FIDELITY_FIXES_OK (9 mechanisms, 72 exit cases)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
