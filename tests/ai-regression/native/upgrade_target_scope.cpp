#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/BattleSimulator.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 20) std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture() : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1; game.relics = {};
        game.enterBattle(MonsterEncounter::SMALL_SLIMES); b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 0; b.player.curHp = b.player.maxHp = 80;
        for (int i = 0; i < 2; ++i) b.monsters.arr[i].curHp = b.monsters.arr[i].maxHp = 100;
    }
    bool play(int target = 0) {
        const search::Action action(search::ActionType::CARD, 0, target);
        check(action.isValidAction(b), "zero-energy card remains legal");
        if (!action.isValidAction(b)) return false;
        action.execute(b);
        return true;
    }
};
int status(const Monster &m, CardId id) {
    return id == CardId::BLIND ? m.getStatus<MS::WEAK>() : m.getStatus<MS::VULNERABLE>();
}
void matrix() {
    for (CardId id : {CardId::BLIND, CardId::TRIP}) for (bool up : {false, true})
    for (int target : {0, 1}) for (int a0 : {0, 1, 2}) for (int a1 : {0, 1, 2})
    for (int existing : {0, 3}) {
        Fixture f;
        f.b.cards.createTempCardInHand(CardInstance(id, up));
        check(f.b.cards.hand[0].requiresTarget() == !up, "upgrade changes target requirement");
        check(f.b.cards.hand[0].cost == 0 && f.b.cards.hand[0].costForTurn == 0, "both variants cost zero");
        for (int i = 0; i < 2; ++i) {
            auto &m = f.b.monsters.arr[i]; const int artifact = i ? a1 : a0;
            if (artifact) m.buff<MS::ARTIFACT>(artifact);
            if (existing) {
                if (id == CardId::BLIND) m.addDebuff<MS::WEAK>(existing, false);
                else m.addDebuff<MS::VULNERABLE>(existing, false);
            }
        }
        if (!f.play(target)) continue;
        check(f.b.player.energy == 0, "no energy consumed");
        check(f.b.cards.discardPile.size() == 1 && f.b.cards.exhaustPile.empty(), "non-exhaust skill discards once");
        for (int i = 0; i < 2; ++i) {
            auto &m = f.b.monsters.arr[i]; const int artifact = i ? a1 : a0;
            const bool affected = up || target == i;
            check(status(m, id) == existing + (affected && !artifact ? 2 : 0), "target-specific stacks and Artifact");
            check(m.getStatus<MS::ARTIFACT>() == artifact - (affected && artifact ? 1 : 0), "each enemy consumes only its own Artifact");
            check(!m.wasJustApplied<MS::WEAK>() && !m.wasJustApplied<MS::VULNERABLE>(), "player-source duration flag");
            check(m.curHp == 100, "debuff skill causes no direct damage");
        }
    }
}
void lifecycle() {
    for (CardId id : {CardId::BLIND, CardId::TRIP}) for (bool up : {false, true}) {
        Fixture f; f.b.cards.createTempCardInHand(CardInstance(id, up));
        f.b.player.buff<PS::CORRUPTION>(); f.b.player.buff<PS::FEEL_NO_PAIN>(3);
        f.play(1);
        check(f.b.cards.exhaustPile.size() == 1 && f.b.cards.discardPile.empty() && f.b.player.block == 3,
              "Corruption exhausts once and triggers Feel No Pain");
        Fixture duration; duration.b.cards.createTempCardInHand(CardInstance(id, up));
        for (int i = 0; i < 6; ++i) duration.b.cards.createTempCardInDrawPile(i, CardInstance(CardId::DEFEND_RED));
        if (!duration.play(1)) continue;
        search::Action(search::ActionType::END_TURN).execute(duration.b);
        for (int i = 0; i < 2; ++i) check(status(duration.b.monsters.arr[i], id) == (up || i == 1 ? 1 : 0), "two stacks become one after enemy turn");
        Fixture upgrade; upgrade.b.cards.createTempCardInHand(CardInstance(id));
        upgrade.b.cards.hand[0].upgrade();
        check(!upgrade.b.cards.hand[0].requiresTarget(), "in-combat upgrade changes targeting immediately");
        upgrade.play();
        check(status(upgrade.b.monsters.arr[0], id) == 2 && status(upgrade.b.monsters.arr[1], id) == 2, "in-combat upgraded effect is area");
        Fixture dead; dead.b.cards.createTempCardInHand(CardInstance(id, up));
        dead.b.monsters.arr[0].curHp = 0; dead.b.monsters.monstersAlive = 1;
        dead.b.monsters.arr[0].buff<MS::ARTIFACT>();
        check(dead.b.cards.hand[0].canUse(dead.b, 0, false) == up, "base rejects dead target, upgraded needs no live selected target");
        dead.play(up ? 0 : 1);
        check(status(dead.b.monsters.arr[0], id) == 0 && dead.b.monsters.arr[0].getStatus<MS::ARTIFACT>() == 1, "dead slot untouched");
        check(status(dead.b.monsters.arr[1], id) == 2, "living target receives effect");
    }
    for (bool up : {false, true}) {
        Fixture belt; belt.b.player.setHasRelic<RelicId::CHAMPION_BELT>(true);
        belt.b.cards.createTempCardInHand(CardInstance(CardId::TRIP, up));
        belt.b.monsters.arr[0].buff<MS::ARTIFACT>(); belt.play(1);
        check(belt.b.monsters.arr[1].getStatus<MS::VULNERABLE>() == 2 && belt.b.monsters.arr[1].getStatus<MS::WEAK>() == 1, "Champion Belt follows successful Vulnerable");
        check(belt.b.monsters.arr[0].getStatus<MS::WEAK>() == 0, "blocked/untargeted Vulnerable does not trigger Belt");
    }
}
void legalActions() {
    for (CardId id : {CardId::BLIND, CardId::TRIP}) for (bool up : {false, true}) {
        Fixture f;
        BattleSimulator sim; sim.initBattle(f.game); *sim.bc = f.b;
        sim.bc->cards.createTempCardInHand(CardInstance(id, up));
        std::ostringstream out; sim.printNormalActions(out);
        check(out.str().find("0:") != std::string::npos, "zero-energy action exported");
        if (!sim.bc->cards.hand[0].canUseOnAnyTarget(*sim.bc)) continue;
        check((out.str().find(",  0 1") != std::string::npos) == !up, "native action export lists targets only for base");
        sim.takeNormalAction(up ? "0" : "0 1");
        sim.bc->inputState = InputState::EXECUTING_ACTIONS; sim.bc->executeActions();
        check(status(sim.bc->monsters.arr[0], id) == (up ? 2 : 0) && status(sim.bc->monsters.arr[1], id) == 2, "console action without target executes area effect");
    }
}
void roots() {
    for (CardId id : {CardId::BLIND, CardId::TRIP}) for (bool up : {false, true})
    for (bool frozen : {false, true}) for (int seed = 1; seed <= 16; ++seed) {
        Fixture f; f.b.cards.createTempCardInHand(CardInstance(id, up));
        if (frozen) f.b.player.setHasRelic<RelicId::FROZEN_EYE>(true);
        for (int i = 0; i < 8; ++i) f.b.cards.createTempCardInDrawPile(i, CardInstance(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED));
        auto twin = f.b;
        if (!frozen) std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        twin.cardRandomRng = Random(98765); twin.aiRng = Random(4321);
        const std::array<std::uint64_t, 7> seeds{11, static_cast<std::uint64_t>(seed), 33, 44, 55, 66, 77};
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        check(f.b.cards.hand[0].uniqueId == 100 && f.b.cards.hand[0].requiresTarget() == !up, "sample preserves visible identity and target scope");
        check(f.b.cards.hand[0].canUse(f.b, 1, false), "sampled zero-energy action remains legal");
        if (!f.b.cards.hand[0].canUse(f.b, 1, false)) continue;
        search::Action(search::ActionType::CARD, 0, 1).execute(f.b);
        search::Action(search::ActionType::CARD, 0, 1).execute(twin);
        for (int i = 0; i < 2; ++i) check(status(f.b.monsters.arr[i], id) == status(twin.monsters.arr[i], id), "paired hidden worlds produce equal debuffs");
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        check(f.b.cards.discardPile[0].uniqueId == 100 && status(f.b.monsters.arr[1], id) == 2, "resolved root retains played identity and status");
        search::Action(search::ActionType::END_TURN).execute(f.b);
        search::Action(search::ActionType::END_TURN).execute(twin);
        check(f.b.player.curHp == twin.player.curHp && f.b.cards.cardsInHand == twin.cards.cardsInHand, "paired continuation ignores original hidden RNG");
        for (int i = 0; i < f.b.cards.cardsInHand; ++i) check(f.b.cards.hand[i].id == twin.cards.hand[i].id, "paired draw order agrees");
    }
}
}
int main() {
    matrix(); lifecycle(); legalActions(); roots();
    std::cout << "UPGRADE_TARGET_SCOPE_" << (failures ? "FAILED" : "OK") << " (" << checks << " checks, " << failures << " failures)\n";
    return failures ? 1 : 0;
}
