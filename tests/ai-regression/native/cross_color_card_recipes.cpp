#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;
namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Spec {
    CardId id;
    int damage[2], block[2], draw[2], hpLoss, strength[2], cost;
    bool exhaust;
};
constexpr Spec specs[] = {
    {CardId::SWIFT_STRIKE, {7,10}, {0,0}, {0,0}, 0, {0,0}, 0, false},
    {CardId::GOOD_INSTINCTS, {0,0}, {6,9}, {0,0}, 0, {0,0}, 0, false},
    {CardId::FLASH_OF_STEEL, {3,6}, {0,0}, {1,1}, 0, {0,0}, 0, false},
    {CardId::FINESSE, {0,0}, {2,4}, {1,1}, 0, {0,0}, 0, false},
    {CardId::MASTER_OF_STRATEGY, {0,0}, {0,0}, {3,4}, 0, {0,0}, 0, true},
    {CardId::CARNAGE, {20,28}, {0,0}, {0,0}, 0, {0,0}, 2, false},
    {CardId::HEMOKINESIS, {15,20}, {0,0}, {0,0}, 2, {0,0}, 1, false},
    {CardId::JAX, {0,0}, {0,0}, {0,0}, 3, {2,3}, 0, false},
};
struct Fixture {
    GameContext game;
    BattleContext battle;
    Fixture(CardId id, bool up = false, bool frozen = false)
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
        CardInstance played(id, up);
        played.setUniqueId(100);
        battle.cards.hand[battle.cards.cardsInHand++] = played;
        int unique = 101;
        for (const auto next : {CardId::STRIKE_RED, CardId::DEFEND_RED, CardId::BASH,
                               CardId::CLEAVE, CardId::SHRUG_IT_OFF}) {
            CardInstance card(next);
            card.setUniqueId(unique++);
            battle.cards.drawPile.push_back(card);
        }
        battle.cards.nextUniqueCardId = unique;
    }
};
void play(BattleContext &b) {
    const auto card = b.cards.hand[0];
    require(card.canUse(b, 0, false), "fixture card not playable");
    b.addToBotCard(CardQueueItem(card, 0, b.player.energy));
    b.inputState = InputState::EXECUTING_ACTIONS;
    b.executeActions();
}
std::vector<int> drawIds(const BattleContext &b) {
    std::vector<int> result;
    for (const auto &card : b.cards.drawPile) result.push_back(card.uniqueId);
    return result;
}
std::vector<int> handIds(const BattleContext &b) {
    std::vector<int> result;
    for (int i = 0; i < b.cards.cardsInHand; ++i) result.push_back(b.cards.hand[i].uniqueId);
    return result;
}
void variantsAndSampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for (const auto &spec : specs) for (int up = 0; up < 2; ++up) {
        Fixture f(spec.id, up);
        BattleContext twin(f.battle);
        std::reverse(twin.cards.drawPile.begin(), twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, f.battle, seeds);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds);
        require(drawIds(f.battle) == drawIds(twin), "source hidden order leaked");
        play(f.battle);
        play(twin);
        require(handIds(f.battle) == handIds(twin), "paired draws disagree");
        require(f.battle.player.curHp == 60-spec.hpLoss, "HP loss mismatch");
        require(f.battle.monsters.arr[0].curHp == 200-spec.damage[up], "damage mismatch");
        require(f.battle.player.block == spec.block[up], "block mismatch");
        require(f.battle.player.getStatus<PS::STRENGTH>() == spec.strength[up], "Strength mismatch");
        require(f.battle.player.energy == 5-spec.cost, "cost mismatch");
        require(f.battle.cards.cardsInHand == spec.draw[up], "draw mismatch");
        require(f.battle.cards.exhaustPile.size() == (spec.exhaust ? 1U : 0U), "exhaust mismatch");
        require(f.battle.cards.discardPile.size() == (spec.exhaust ? 0U : 1U), "discard mismatch");
        const auto observed = handIds(f.battle);
        public_sampling::resampleCombatContinuation(f.game, f.battle, seeds);
        require(handIds(f.battle) == observed, "post-action hand changed on resampling");

        Fixture frozen(spec.id, up, true);
        const auto visible = drawIds(frozen.battle);
        public_sampling::resampleCombatContinuation(frozen.game, frozen.battle, seeds);
        require(drawIds(frozen.battle) == visible, "Frozen Eye order changed");
        play(frozen.battle);
        const auto remaining = drawIds(frozen.battle);
        public_sampling::resampleCombatContinuation(frozen.game, frozen.battle, seeds);
        require(drawIds(frozen.battle) == remaining, "post-action Frozen Eye order changed");
    }
}
void drawLimitsAndLifecycle() {
    for (const auto &spec : specs) if (spec.draw[0] > 0) {
        for (int up = 0; up < 2; ++up) {
            Fixture noDraw(spec.id, up);
            noDraw.battle.player.buff<PS::NO_DRAW>(1);
            play(noDraw.battle);
            require(noDraw.battle.cards.cardsInHand == 0, "No Draw ignored");
            require(noDraw.battle.player.block == spec.block[up], "No Draw suppressed block");
            require(noDraw.battle.monsters.arr[0].curHp == 200-spec.damage[up], "No Draw suppressed damage");
            Fixture shuffle(spec.id, up);
            shuffle.battle.cards.discardPile = shuffle.battle.cards.drawPile;
            shuffle.battle.cards.drawPile.clear();
            play(shuffle.battle);
            require(shuffle.battle.cards.cardsInHand == spec.draw[up], "shuffle draw failed");
            Fixture full(spec.id, up);
            for (int i = 0; i < 9; ++i) {
                CardInstance filler(CardId::DEFEND_RED);
                filler.setUniqueId(full.battle.cards.nextUniqueCardId++);
                full.battle.cards.hand[full.battle.cards.cardsInHand++] = filler;
            }
            play(full.battle);
            require(full.battle.cards.cardsInHand == 10 && full.battle.cards.drawPile.size() == 4,
                    "hand limit / freed slot wrong");
        }
    }
    for (const bool up : {false,true}) {
        Fixture ethereal(CardId::CARNAGE, up);
        search::Action end(search::ActionType::END_TURN);
        require(end.isValidAction(ethereal.battle), "end turn is illegal");
        end.execute(ethereal.battle);
        require(ethereal.battle.cards.exhaustPile.size() == 1 &&
                ethereal.battle.cards.exhaustPile[0].id == CardId::CARNAGE,
                "unplayed Carnage did not exhaust");
    }
    Fixture lethalDraw(CardId::FLASH_OF_STEEL);
    lethalDraw.battle.monsters.arr[0].curHp = 1;
    play(lethalDraw.battle);
    require(lethalDraw.battle.outcome == Outcome::PLAYER_VICTORY &&
            lethalDraw.battle.cards.cardsInHand == 0, "terminal damage did not suppress queued draw");
}
void modifiersAndOrderedHpLoss() {
    for (const bool up : {false,true}) {
        for (const auto id : {CardId::GOOD_INSTINCTS, CardId::FINESSE}) {
            Fixture block(id, up);
            block.battle.player.buff<PS::DEXTERITY>(3);
            block.battle.player.debuff<PS::FRAIL>(1);
            play(block.battle);
            const int base = id == CardId::FINESSE ? (up?4:2) : (up?9:6);
            require(block.battle.player.block == (base+3)*3/4, "Dexterity/Frail order wrong");
        }
        for (const auto id : {CardId::HEMOKINESIS, CardId::JAX}) {
            Fixture loss(id, up);
            loss.battle.player.block = 99;
            loss.battle.player.buff<PS::RUPTURE>(2);
            play(loss.battle);
            require(loss.battle.player.block == 99, "HP loss used block");
            require(loss.battle.player.getStatus<PS::STRENGTH>() ==
                    2+(id == CardId::JAX ? (up?3:2) : 0), "Rupture/Strength order wrong");
            if (id == CardId::HEMOKINESIS)
                require(loss.battle.monsters.arr[0].curHp == 200-(up?20:15),
                        "Hemokinesis recalculated queued damage after Rupture");
            Fixture lethal(id, up);
            lethal.battle.player.curHp = id == CardId::JAX ? 3 : 2;
            play(lethal.battle);
            require(lethal.battle.outcome == Outcome::PLAYER_LOSS &&
                    lethal.battle.monsters.arr[0].curHp == 200 &&
                    lethal.battle.player.getStatus<PS::STRENGTH>() == 0,
                    "lethal HP loss did not interrupt later effect");
            Fixture buffer(id, up);
            buffer.battle.player.buff<PS::BUFFER>(1);
            buffer.battle.player.buff<PS::RUPTURE>(2);
            play(buffer.battle);
            require(buffer.battle.player.curHp == 60 && !buffer.battle.player.hasStatus<PS::BUFFER>(),
                    "Buffer did not prevent HP loss");
            require(buffer.battle.player.getStatus<PS::STRENGTH>() ==
                    (id == CardId::JAX ? (up?3:2) : 0), "prevented loss triggered Rupture");
        }
    }
}
}
int main() {
    try {
        variantsAndSampling();
        drawLimitsAndLifecycle();
        modifiersAndOrderedHpLoss();
        std::cout << "CROSS_COLOR_CARD_RECIPES_OK (8 cards, 16 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "CROSS_COLOR_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
