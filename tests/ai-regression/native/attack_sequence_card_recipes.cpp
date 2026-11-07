#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"

using namespace sts;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Spec { CardId id; int damage[2], hits[2], strength[2], cost; bool exhaust; };
constexpr Spec specs[] = {
    {CardId::TWIN_STRIKE, {5,7}, {2,2}, {1,1}, 1, false},
    {CardId::PUMMEL, {2,2}, {4,5}, {1,1}, 1, true},
    {CardId::HEAVY_BLADE, {14,14}, {1,1}, {3,5}, 2, false},
};
struct Fixture {
    GameContext game;
    BattleContext battle;
    Fixture(CardId id, bool up, bool frozen = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        if (frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.player.curHp = 60;
        battle.player.block = 0;
        battle.player.energy = 5;
        battle.monsters.arr[0].curHp = 200;
        battle.monsters.arr[0].maxHp = 200;
        CardInstance card(id, up);
        card.setUniqueId(100);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
        int uid = 101;
        for (auto next : {CardId::BASH, CardId::STRIKE_RED, CardId::DEFEND_RED}) {
            CardInstance drawn(next);
            drawn.setUniqueId(uid++);
            battle.cards.drawPile.push_back(drawn);
        }
        battle.cards.nextUniqueCardId = uid;
    }
};
void play(BattleContext &b) {
    auto card = b.cards.hand[0];
    require(card.canUse(b, 0, false), "card not playable");
    b.addToBotCard(CardQueueItem(card, 0, b.player.energy));
    b.inputState = InputState::EXECUTING_ACTIONS;
    b.executeActions();
}
std::vector<int> drawIds(const BattleContext &b) {
    std::vector<int> ids;
    for (const auto &c : b.cards.drawPile) ids.push_back(c.uniqueId);
    return ids;
}
void variantsAndModifiers() {
    for (auto spec : specs) for (int up=0; up<2; ++up)
    for (int strength : {-10,-2,0,3}) for (int modifiers=0; modifiers<4; ++modifiers) {
        Fixture f(spec.id, up);
        auto &b = f.battle;
        b.player.buff<PS::STRENGTH>(strength);
        if (modifiers & 1) b.player.debuff<PS::WEAK>(1);
        if (modifiers & 2) b.monsters.arr[0].addDebuff<MS::VULNERABLE>(1, false);
        b.monsters.arr[0].block = 7;
        double hit = std::max(0, spec.damage[up] + strength * spec.strength[up]);
        if (modifiers & 1) hit *= .75;
        if (modifiers & 2) hit *= 1.5;
        int damage = static_cast<int>(hit) * spec.hits[up];
        play(b);
        require(b.monsters.arr[0].curHp == 200-std::max(0,damage-7), "damage/rounding mismatch");
        require(b.monsters.arr[0].block == std::max(0,7-damage), "block consumed per sequence incorrectly");
        require(b.player.energy == 5-spec.cost, "energy mismatch");
        require(b.cards.cardsInHand == 0, "card stayed in hand");
        const auto &pile = spec.exhaust ? b.cards.exhaustPile : b.cards.discardPile;
        require(pile.size() == 1 && pile[0].uniqueId == 100, "played card lifecycle mismatch");
    }
}
void samplingAndInteractions() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for (auto spec : specs) for (int up=0; up<2; ++up) {
        Fixture f(spec.id, up);
        BattleContext twin(f.battle);
        std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, f.battle, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        require(drawIds(f.battle) == drawIds(twin), "hidden order leaked");
        play(f.battle);
        play(twin);
        require(f.battle.monsters.arr[0].curHp == twin.monsters.arr[0].curHp,
                "paired outcome mismatch");
        public_sampling::resampleCombatContinuation(f.game, f.battle, seeds);
        require(f.battle.cards.cardsInHand == 0, "post-action resampling changed hand");
        Fixture frozen(spec.id, up, true);
        auto order = drawIds(frozen.battle);
        public_sampling::resampleCombatContinuation(frozen.game, frozen.battle, seeds);
        require(drawIds(frozen.battle) == order, "Frozen Eye before play changed");
        play(frozen.battle);
        public_sampling::resampleCombatContinuation(frozen.game, frozen.battle, seeds);
        require(drawIds(frozen.battle) == order, "Frozen Eye after play changed");

        Fixture intangible(spec.id, up);
        intangible.battle.monsters.arr[0].buff<MS::INTANGIBLE>(1);
        play(intangible.battle);
        require(intangible.battle.monsters.arr[0].curHp == 200-spec.hits[up],
                "Intangible must cap each hit");
        Fixture thorns(spec.id, up);
        thorns.battle.monsters.arr[0].buff<MS::THORNS>(3);
        play(thorns.battle);
        require(thorns.battle.player.curHp == 60-3*spec.hits[up], "Thorns must trigger per hit");
        Fixture dying(spec.id, up);
        dying.battle.player.curHp = 2;
        dying.battle.monsters.arr[0].buff<MS::THORNS>(3);
        play(dying.battle);
        require(dying.battle.outcome == Outcome::PLAYER_LOSS &&
                dying.battle.monsters.arr[0].curHp == 200-spec.damage[up],
                "player death must interrupt subsequent hits");
        Fixture kill(spec.id, up);
        kill.battle.monsters.arr[0].curHp = 1;
        play(kill.battle);
        require(kill.battle.outcome == Outcome::PLAYER_VICTORY, "lethal sequence did not terminate");
    }
}
}
int main() {
    try {
        variantsAndModifiers();
        samplingAndInteractions();
        std::cout << "ATTACK_SEQUENCE_CARD_RECIPES_OK (3 cards, 6 variants, 96 modifier cases)\n";
    } catch (const std::exception &e) {
        std::cerr << "ATTACK_SEQUENCE_CARD_RECIPES_FAILED: " << e.what() << '\n';
        return 1;
    }
}
