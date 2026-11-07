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
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    GameContext game;
    BattleContext battle;
    explicit Fixture(bool frozen = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (frozen) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        battle.init(game);
        battle.executeActions();
        require(battle.inputState == InputState::PLAYER_NORMAL, "fixture not ready");
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
    }
    void hand(CardId id) {
        CardInstance c(id);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = c;
    }
    void draw(CardId id) {
        CardInstance c(id);
        c.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.drawPile.push_back(c);
    }
};

void playFirst(BattleContext &bc) {
    auto c = bc.cards.hand[0];
    require(c.canUse(bc, 0, false), "Slimed not playable");
    bc.addToBotCard(CardQueueItem(c, 0, bc.player.energy));
    bc.inputState = InputState::EXECUTING_ACTIONS;
    bc.executeActions();
    require(bc.inputState == InputState::PLAYER_NORMAL, "self-exhaust did not resolve");
}

std::vector<int> uniqueIds(const std::vector<CardInstance> &pile) {
    std::vector<int> result;
    for (auto c : pile) result.push_back(c.getUniqueId());
    return result;
}

void verifyCostLegalityAndIdentity() {
    Fixture f;
    f.hand(CardId::SLIMED);
    f.hand(CardId::SLIMED);
    f.hand(CardId::WOUND);
    f.hand(CardId::BURN);
    f.hand(CardId::DAZED);
    auto &bc = f.battle;
    const auto slimed = bc.cards.hand[0];
    require(slimed.getType() == CardType::STATUS && slimed.costForTurn == 1,
            "Slimed is a one-energy Status");
    bc.player.energy = 0;
    require(!slimed.canUse(bc, 0, false), "Slimed played without energy");
    bc.player.energy = 2;
    for (int i = 2; i < 5; ++i)
        require(!bc.cards.hand[i].canUse(bc, 0, false), "ordinary Status became playable");
    int hp = bc.player.curHp;
    int monsterHp = bc.monsters.arr[0].curHp;
    int block = bc.player.block;
    playFirst(bc);
    require(bc.player.energy == 1, "Slimed energy cost");
    require(bc.player.curHp == hp && bc.player.block == block &&
            bc.monsters.arr[0].curHp == monsterHp, "self-exhaust has direct effects");
    require(bc.cards.exhaustPile.size() == 1 &&
            bc.cards.exhaustPile[0].getUniqueId() == slimed.getUniqueId(),
            "self-exhaust moved wrong identity");
    require(bc.cards.cardsInHand == 4 && bc.cards.hand[0].id == CardId::SLIMED &&
            bc.cards.hand[0].getUniqueId() != slimed.getUniqueId(), "another Slimed was removed");
    require(bc.cards.discardPile.empty(), "played Slimed entered discard");
    playFirst(bc);
    require(bc.player.energy == 0 && bc.cards.exhaustPile.size() == 2,
            "second Slimed did not independently consume energy/exhaust");
}

void verifyUnplayedSlimedIsNotEthereal() {
    Fixture f;
    f.hand(CardId::SLIMED);
    // Avoid immediately redrawing Slimed after the turn boundary.
    for (int i = 0; i < 6; ++i) f.draw(CardId::DEFEND_RED);
    search::Action action(search::ActionType::END_TURN);
    action.execute(f.battle);
    require(f.battle.inputState == InputState::PLAYER_NORMAL, "end turn did not finish");
    require(f.battle.cards.exhaustPile.empty(), "unplayed Slimed exhausted");
    require(f.battle.cards.discardPile.size() == 1 &&
            f.battle.cards.discardPile[0].id == CardId::SLIMED, "unplayed Slimed not discarded");
}

void verifyExhaustCallbacks() {
    // Test an already-visible power combination, without admitting the power cards.
    Fixture f;
    f.hand(CardId::SLIMED);
    f.draw(CardId::STRIKE_RED);
    f.battle.player.buff<PS::FEEL_NO_PAIN>(3);
    f.battle.player.buff<PS::DARK_EMBRACE>(1);
    playFirst(f.battle);
    require(f.battle.player.block == 3, "self-exhaust omitted Feel No Pain callback");
    require(f.battle.cards.cardsInHand == 1 && f.battle.cards.hand[0].id == CardId::STRIKE_RED,
            "self-exhaust omitted Dark Embrace draw");
    require(f.battle.cards.exhaustPile.size() == 1, "callback repeated self-exhaust");
}

void verifyPublicResampling() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    for (bool frozen : {false, true}) {
        Fixture f(frozen);
        f.hand(CardId::SLIMED);
        f.hand(CardId::DEFEND_RED);
        f.draw(CardId::STRIKE_RED);
        f.draw(CardId::BASH);
        f.draw(CardId::DEFEND_RED);
        public_sampling::resampleCombatContinuation(f.game, f.battle, seeds);
        playFirst(f.battle);
        auto first = f.battle;
        auto reversed = f.battle;
        const auto order = uniqueIds(first.cards.drawPile);
        if (!frozen) std::reverse(reversed.cards.drawPile.begin(), reversed.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, first, seeds);
        public_sampling::resampleCombatContinuation(f.game, reversed, seeds);
        require(uniqueIds(first.cards.drawPile) == uniqueIds(reversed.cards.drawPile),
                "post-exhaust sample retained hidden order");
        if (frozen) require(uniqueIds(first.cards.drawPile) == order, "Frozen Eye order changed");
        require(uniqueIds(first.cards.exhaustPile) == uniqueIds(f.battle.cards.exhaustPile) &&
                first.cards.hand[0].getUniqueId() == f.battle.cards.hand[0].getUniqueId() &&
                first.player.energy == f.battle.player.energy, "sample changed public self-exhaust outcome");
    }
}
}

int main() {
    try {
        verifyCostLegalityAndIdentity();
        verifyUnplayedSlimedIsNotEthereal();
        verifyExhaustCallbacks();
        verifyPublicResampling();
        std::cout << "EXHAUST_ONLY_CARD_RECIPES_OK (Slimed, non-upgradable)\n";
    } catch (const std::exception &error) {
        std::cerr << "EXHAUST_ONLY_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
