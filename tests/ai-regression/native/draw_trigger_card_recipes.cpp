#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"

using namespace sts;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    GameContext game;
    BattleContext battle;
    explicit Fixture(bool frozen = false, bool multiple = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (frozen) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1;
        game.enterBattle(multiple ? MonsterEncounter::THREE_SENTRIES : MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
        battle.player.energy = 10;
    }
    void hand(CardId id, bool up = false) {
        CardInstance c(id, up);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = c;
    }
    void draw(CardId id) {
        CardInstance c(id);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.drawPile.push_back(c);
    }
    void execute() {
        battle.inputState = InputState::EXECUTING_ACTIONS;
        battle.executeActions();
    }
    void play() {
        auto c = battle.cards.hand[0];
        require(c.canUse(battle, 0, false), "trigger card not playable");
        battle.addToBotCard(CardQueueItem(c, 0, battle.player.energy));
        execute();
    }
    void drawCards(int amount) {
        battle.addToBot(Actions::DrawCards(amount));
        execute();
    }
};

void verifySinglePowerVariantsAndTypes() {
    for (bool up : {false, true}) {
        for (CardId id : {CardId::EVOLVE, CardId::FIRE_BREATHING}) {
            for (CardId drawn : {CardId::WOUND, CardId::INJURY, CardId::DEFEND_RED}) {
                Fixture f;
                f.hand(id, up);
                f.draw(CardId::STRIKE_RED);
                f.draw(CardId::BASH);
                f.draw(drawn);
                f.play();
                require(f.battle.cards.cardsInHand == 0 && f.battle.cards.discardPile.empty() &&
                        f.battle.cards.exhaustPile.empty(), "power did not leave combat card piles");
                int hp = f.battle.monsters.arr[0].curHp;
                f.drawCards(1);
                int extra = id == CardId::EVOLVE && drawn == CardId::WOUND ? (up ? 2 : 1) : 0;
                int damage = id == CardId::FIRE_BREATHING && drawn != CardId::DEFEND_RED ? (up ? 10 : 6) : 0;
                require(f.battle.cards.cardsInHand == 1 + extra, "trigger type/upgrade draw amount");
                require(hp - f.battle.monsters.arr[0].curHp == damage, "trigger type/upgrade damage amount");
            }
        }
    }
}

void verifyChainAndBounds() {
    Fixture chain;
    chain.hand(CardId::EVOLVE);
    chain.hand(CardId::EVOLVE, true);
    chain.draw(CardId::STRIKE_RED);
    chain.draw(CardId::DAZED);
    chain.draw(CardId::WOUND);
    chain.play();
    chain.play();
    require(chain.battle.player.getStatus<PS::EVOLVE>() == 3, "Evolve stacking");
    chain.drawCards(1);
    if (chain.battle.cards.cardsInHand != 3 || !chain.battle.cards.drawPile.empty()) {
        throw std::runtime_error("chained draw/empty supply: hand=" +
            std::to_string(chain.battle.cards.cardsInHand) + " draw=" +
            std::to_string(chain.battle.cards.drawPile.size()) + " discard=" +
            std::to_string(chain.battle.cards.discardPile.size()) + " outcome=" +
            std::to_string(static_cast<int>(chain.battle.outcome)));
    }
    for (bool noDraw : {false, true}) {
        Fixture f;
        f.hand(CardId::EVOLVE, true);
        f.hand(CardId::FIRE_BREATHING);
        f.draw(CardId::STRIKE_RED);
        f.draw(CardId::WOUND);
        f.play();
        f.play();
        for (int i = 0; i < 9; ++i) f.hand(CardId::DEFEND_RED);
        if (noDraw) f.battle.player.debuff<PS::NO_DRAW>(1, false);
        f.drawCards(1);
        require(f.battle.cards.cardsInHand == (noDraw ? 9 : 10), "No Draw/hand cap");
        require(f.battle.cards.drawPile.size() == (noDraw ? 2 : 1), "blocked draw consumed cards");
    }
    Fixture generated;
    generated.hand(CardId::FIRE_BREATHING);
    generated.hand(CardId::EVOLVE);
    generated.draw(CardId::STRIKE_RED);
    generated.play();
    generated.play();
    int hp = generated.battle.monsters.arr[0].curHp;
    generated.battle.addToBot(Actions::MakeTempCardInHand(CardId::WOUND, false, 2));
    generated.execute();
    require(generated.battle.cards.cardsInHand == 2 && generated.battle.cards.drawPile.size() == 1 &&
            generated.battle.monsters.arr[0].curHp == hp, "generation was incorrectly treated as draw");
}

void verifyNonAttackDamageAndStacking() {
    Fixture f(false, true);
    f.hand(CardId::FIRE_BREATHING);
    f.hand(CardId::FIRE_BREATHING, true);
    f.draw(CardId::STRIKE_RED);
    f.draw(CardId::WOUND);
    f.play();
    f.play();
    require(f.battle.player.getStatus<PS::FIRE_BREATHING>() == 16, "Fire Breathing stacking");
    f.battle.player.strength = 9;
    f.battle.player.debuff<PS::WEAK>(2, false);
    f.battle.monsters.arr[0].removeStatus<MS::ARTIFACT>();
    f.battle.monsters.arr[0].addDebuff<MS::VULNERABLE>(2, false);
    require(f.battle.monsters.arr[0].getStatus<MS::VULNERABLE>() == 2, "Vulnerable fixture setup");
    f.battle.monsters.arr[0].block = 3;
    int hp = f.battle.monsters.arr[0].curHp;
    f.drawCards(1);
    require(hp - f.battle.monsters.arr[0].curHp == 13, "trigger damage used attack modifiers or bypassed block");
    require(f.battle.monsters.arr[1].curHp == f.battle.monsters.arr[1].maxHp - 16 &&
            f.battle.monsters.arr[2].curHp == f.battle.monsters.arr[2].maxHp - 16,
            "Fire Breathing did not damage all live enemies");
}

void verifyAcquisitionOrderAtLethalBoundary() {
    for (bool fireFirst : {false, true}) {
      for (bool frozen : {false, true}) {
        Fixture f(frozen);
        f.hand(fireFirst ? CardId::FIRE_BREATHING : CardId::EVOLVE);
        f.hand(fireFirst ? CardId::EVOLVE : CardId::FIRE_BREATHING);
        f.draw(CardId::STRIKE_RED);
        f.draw(CardId::DEFEND_RED);
        f.draw(CardId::WOUND);
        f.play();
        f.play();
        f.battle.monsters.arr[0].curHp = 6;
        const int drawnBefore = f.battle.cardsDrawn;
        f.drawCards(1);
        require(f.battle.outcome == Outcome::PLAYER_VICTORY, "trigger did not finish combat");
        // Same-priority powers retain acquisition order in the original game.
        // Fire first kills before the queued Evolve draw; Evolve first draws Defend.
        if (fireFirst) {
            require(f.battle.cardsDrawn == drawnBefore + 1,
                    "Fire Breathing acquired first: Evolve drew before lethal damage");
        } else {
            require(f.battle.cardsDrawn == drawnBefore + 2,
                    "Evolve acquired first: expected extra draw before lethal damage");
        }
      }
    }
}

void verifyOrderSurvivesCopyResamplingAndReapplication() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    for (bool fireFirst : {false, true}) {
      for (bool frozen : {false, true}) {
        Fixture f(frozen);
        f.hand(fireFirst ? CardId::FIRE_BREATHING : CardId::EVOLVE);
        f.hand(fireFirst ? CardId::EVOLVE : CardId::FIRE_BREATHING);
        f.draw(CardId::STRIKE_RED);
        f.draw(CardId::BASH);
        f.draw(CardId::DEFEND_RED);
        f.draw(CardId::WOUND);
        f.draw(CardId::DAZED);
        f.play();
        f.play();
        auto copy = f.battle;
        auto reordered = f.battle;
        if (!frozen) std::reverse(reordered.cards.drawPile.begin(), reordered.cards.drawPile.end());
        const auto originalTop = copy.cards.drawPile.back().getUniqueId();
        public_sampling::resampleCombatContinuation(f.game, copy, seeds);
        public_sampling::resampleCombatContinuation(f.game, reordered, seeds);
        for (size_t i = 0; i < copy.cards.drawPile.size(); ++i)
            require(copy.cards.drawPile[i].getUniqueId() == reordered.cards.drawPile[i].getUniqueId(),
                    "trigger sample depends on hidden source order");
        if (frozen) require(copy.cards.drawPile.back().getUniqueId() == originalTop, "Frozen Eye order changed");
        require(copy.player.powerInstances.before(PS::FIRE_BREATHING, PS::EVOLVE) == fireFirst, "clone/sample lost public power order");
        copy.player.buff<PS::EVOLVE>(1);
        copy.player.buff<PS::FIRE_BREATHING>(6);
        require(copy.player.powerInstances.before(PS::FIRE_BREATHING, PS::EVOLVE) == fireFirst, "stacking changed acquisition order");
        copy.player.removeStatus<PS::EVOLVE>();
        copy.player.buff<PS::EVOLVE>(1);
        require(copy.player.powerInstances.before(PS::FIRE_BREATHING, PS::EVOLVE), "reacquired Evolve must be last");
        copy.player.removeStatus<PS::FIRE_BREATHING>();
        copy.player.buff<PS::FIRE_BREATHING>(6);
        require(!copy.player.powerInstances.before(PS::FIRE_BREATHING, PS::EVOLVE), "reacquired Fire Breathing must be last");
        f.drawCards(1);
        auto afterDraw = f.battle;
        require(afterDraw.cards.cardsInHand == 3 && afterDraw.cards.drawPile.size() == 2,
                "post-trigger sample fixture setup");
        public_sampling::resampleCombatContinuation(f.game, afterDraw, seeds);
        require(afterDraw.player.powerInstances.before(PS::FIRE_BREATHING, PS::EVOLVE) == fireFirst,
                "post-trigger sampling lost acquisition order");
        require(afterDraw.cards.cardsInHand == f.battle.cards.cardsInHand,
                "post-trigger sampling changed public hand size");
        for (int i = 0; i < afterDraw.cards.cardsInHand; ++i)
            require(afterDraw.cards.hand[i].getUniqueId() == f.battle.cards.hand[i].getUniqueId(),
                    "post-trigger sampling changed observed drawn identity");
        if (frozen)
            for (size_t i = 0; i < afterDraw.cards.drawPile.size(); ++i)
                require(afterDraw.cards.drawPile[i].getUniqueId() == f.battle.cards.drawPile[i].getUniqueId(),
                        "post-trigger Frozen Eye order changed");
      }
    }
    Fixture shuffled;
    shuffled.hand(CardId::EVOLVE);
    shuffled.draw(CardId::STRIKE_RED);
    shuffled.play();
    shuffled.battle.cards.discardPile = shuffled.battle.cards.drawPile;
    shuffled.battle.cards.drawPile.clear();
    shuffled.battle.cards.createTempCardInDiscard(CardInstance(CardId::WOUND));
    shuffled.drawCards(2);
    require(shuffled.battle.cards.cardsInHand == 2, "shuffle/insufficient trigger draw");
    search::Action end(search::ActionType::END_TURN);
    end.execute(shuffled.battle);
    require(shuffled.battle.player.getStatus<PS::EVOLVE>() == 1, "draw power did not persist across turns");
}
}

int main() {
    try {
        verifySinglePowerVariantsAndTypes();
        verifyChainAndBounds();
        verifyNonAttackDamageAndStacking();
        verifyAcquisitionOrderAtLethalBoundary();
        verifyOrderSurvivesCopyResamplingAndReapplication();
        std::cout << "DRAW_TRIGGER_CARD_RECIPES_OK (2 cards, 4 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "DRAW_TRIGGER_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
