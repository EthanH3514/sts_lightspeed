#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

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
        require(battle.inputState == InputState::PLAYER_NORMAL, "fixture root not ready");
        battle.cards = CardManager{};
        battle.cards.nextUniqueCardId = 100;
    }
    void hand(CardId id, bool up = false) {
        CardInstance card(id, up);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
    }
    void draw(CardId id) {
        CardInstance card(id);
        card.setUniqueId(battle.cards.nextUniqueCardId++);
        battle.cards.drawPile.push_back(card);
    }
};

void play(BattleContext &bc, int index = 0) {
    auto card = bc.cards.hand[index];
    require(card.canUse(bc, 0, false), "fixture card not playable");
    bc.addToBotCard(CardQueueItem(card, 0, bc.player.energy));
    bc.inputState = InputState::EXECUTING_ACTIONS;
    bc.executeActions();
}

int count(const std::vector<CardInstance> &pile, CardId id) {
    return std::count_if(pile.begin(), pile.end(), [=](const CardInstance &c) { return c.id == id; });
}

std::vector<CardId> ids(const std::vector<CardInstance> &pile) {
    std::vector<CardId> result;
    for (auto c : pile) result.push_back(c.id);
    return result;
}

void endTurn(BattleContext &bc) {
    search::Action action(search::ActionType::END_TURN);
    require(action.isValidAction(bc), "end turn not legal");
    action.execute(bc);
    require(bc.inputState == InputState::PLAYER_NORMAL, "enemy turn did not finish");
}

void verifyPowerThrough() {
    for (bool up : {false, true}) {
        Fixture plain;
        plain.hand(CardId::POWER_THROUGH, up);
        play(plain.battle);
        require(plain.battle.player.block == (up ? 20 : 15), "Power Through unmodified block");
        for (int handSize : {1, 9, 10}) {
            Fixture f;
            f.hand(CardId::POWER_THROUGH, up);
            for (int i = 1; i < handSize; ++i) f.hand(CardId::STRIKE_RED);
            f.battle.player.dexterity = 2;
            f.battle.player.debuff<PS::FRAIL>(1, false);
            play(f.battle);
            require(f.battle.player.block == (up ? 16 : 12), "Power Through Dexterity/Frail");
            require(f.battle.cards.cardsInHand == std::min(10, handSize + 1), "generated hand cap");
            int wounds = 0;
            std::set<int> unique;
            for (int i = 0; i < f.battle.cards.cardsInHand; ++i) {
                auto c = f.battle.cards.hand[i];
                unique.insert(c.getUniqueId());
                if (c.id == CardId::WOUND) {
                    ++wounds;
                    require(!c.isUpgraded() && !c.canUse(f.battle, 0, false), "generated Wound variant/playability");
                }
            }
            for (auto c : f.battle.cards.discardPile) unique.insert(c.getUniqueId());
            require(wounds + count(f.battle.cards.discardPile, CardId::WOUND) == 2, "two Wounds conserved");
            require(count(f.battle.cards.discardPile, CardId::WOUND) == (handSize == 10 ? 1 : 0), "overflow must enter discard");
            require(unique.size() == static_cast<size_t>(handSize + 2), "generated identities collided");
        }
    }
}

void verifyAttacks() {
    for (bool up : {false, true}) {
        for (bool modifiers : {false, true}) {
            Fixture reckless;
            reckless.hand(CardId::RECKLESS_CHARGE, up);
            reckless.draw(CardId::DEFEND_RED);
            auto &bc = reckless.battle;
            if (modifiers) {
                bc.player.strength = 2;
                bc.player.debuff<PS::WEAK>(1, false);
                bc.monsters.arr[0].addDebuff<MS::VULNERABLE>(1, false);
            }
            int hp = bc.monsters.arr[0].curHp;
            play(bc);
            int expected = modifiers ? (up ? 13 : 10) : (up ? 10 : 7);
            require(hp - bc.monsters.arr[0].curHp == expected, "Reckless Charge damage modifiers");
            require(count(bc.cards.drawPile, CardId::DAZED) == 1, "Reckless Charge Dazed destination");
            for (auto c : bc.cards.drawPile) if (c.id == CardId::DAZED)
                require(!c.isUpgraded() && c.isEthereal(), "generated Dazed traits");

            Fixture immolate(false, true);
            immolate.hand(CardId::IMMOLATE, up);
            auto &aoe = immolate.battle;
            std::array<int, 3> hpBefore;
            if (modifiers) {
                aoe.player.strength = 2;
                aoe.player.debuff<PS::WEAK>(1, false);
            }
            for (int i = 0; i < 3; ++i) {
                aoe.monsters.arr[i].curHp = aoe.monsters.arr[i].maxHp = 100;
                hpBefore[i] = 100;
                if (modifiers) {
                    aoe.monsters.arr[i].removeStatus<MS::ARTIFACT>();
                    aoe.monsters.arr[i].addDebuff<MS::VULNERABLE>(1, false);
                }
            }
            play(aoe);
            expected = modifiers ? (up ? 33 : 25) : (up ? 28 : 21);
            for (int i = 0; i < 3; ++i)
                require(hpBefore[i] - aoe.monsters.arr[i].curHp == expected, "Immolate all-target damage modifiers");
            require(count(aoe.cards.discardPile, CardId::BURN) == 1, "Immolate Burn destination");
            for (auto c : aoe.cards.discardPile) if (c.id == CardId::BURN)
                require(!c.isUpgraded(), "Immolate must generate base Burn");
        }
    }
}

void verifyGeneratedStatusLifecycle() {
    Fixture dazed;
    dazed.hand(CardId::RECKLESS_CHARGE);
    play(dazed.battle); // Empty draw pile receives Dazed.
    dazed.battle.addToBot(Actions::DrawCards(1));
    dazed.battle.inputState = InputState::EXECUTING_ACTIONS;
    dazed.battle.executeActions();
    require(dazed.battle.cards.cardsInHand == 1 && dazed.battle.cards.hand[0].id == CardId::DAZED, "draw generated Dazed");
    endTurn(dazed.battle);
    require(count(dazed.battle.cards.exhaustPile, CardId::DAZED) == 1, "Dazed ethereal end-turn exhaust");

    for (int block : {0, 1, 2, 5}) {
        Fixture burn;
        burn.hand(CardId::IMMOLATE);
        play(burn.battle);
        // Drawing the entire two-card discard forces shuffle and includes the generated Burn.
        burn.battle.addToBot(Actions::DrawCards(2));
        burn.battle.inputState = InputState::EXECUTING_ACTIONS;
        burn.battle.executeActions();
        require(burn.battle.cards.cardsInHand == 2, "shuffle generated Burn into hand");
        burn.battle.player.block = block;
        int hp = burn.battle.player.curHp;
        endTurn(burn.battle); // Cultist's first move is non-attacking Incantation.
        require(hp - burn.battle.player.curHp == std::max(0, 2 - block), "Burn must use end-turn block before reset");
        require(count(burn.battle.cards.exhaustPile, CardId::BURN) == 0, "Burn incorrectly exhausted");
    }
}

void verifyExhaustInteractions() {
    for (CardId exhaust : {CardId::TRUE_GRIT, CardId::BURNING_PACT}) {
        Fixture f;
        f.hand(CardId::POWER_THROUGH);
        f.hand(exhaust, true);
        f.draw(CardId::DEFEND_RED);
        f.draw(CardId::BASH);
        f.draw(CardId::STRIKE_RED);
        play(f.battle);
        require(f.battle.cards.hand[1].id == CardId::WOUND, "generated Wound hand ordering");
        int chosenId = f.battle.cards.hand[1].getUniqueId();
        play(f.battle);
        require(f.battle.inputState == InputState::CARD_SELECT, "exhaust selection absent");
        f.battle.chooseExhaustOneCard(0);
        f.battle.inputState = InputState::EXECUTING_ACTIONS;
        f.battle.executeActions();
        require(f.battle.cards.exhaustPile.size() == 1 && f.battle.cards.exhaustPile[0].getUniqueId() == chosenId,
                "exhaust must select exact generated identity");
        require(f.battle.cards.cardsInHand == (exhaust == CardId::BURNING_PACT ? 4 : 1), "exhaust/draw composition count");
    }
}

void verifyResamplingAndTerminal() {
    const std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    for (CardId id : {CardId::POWER_THROUGH, CardId::RECKLESS_CHARGE, CardId::IMMOLATE}) {
        for (bool up : {false, true}) {
            for (bool frozen : {false, true}) {
                Fixture f(frozen);
                f.hand(id, up);
                f.draw(CardId::BASH);
                f.draw(CardId::DEFEND_RED);
                f.draw(CardId::STRIKE_RED);
                public_sampling::resampleCombatContinuation(f.game, f.battle, seeds);
                play(f.battle);
                auto a = f.battle;
                auto b = f.battle;
                if (!frozen) std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
                auto visible = ids(a.cards.drawPile);
                public_sampling::resampleCombatContinuation(f.game, a, seeds);
                public_sampling::resampleCombatContinuation(f.game, b, seeds);
                require(ids(a.cards.drawPile) == ids(b.cards.drawPile), "sample retained hidden insertion order");
                if (frozen) require(ids(a.cards.drawPile) == visible, "Frozen Eye order changed");
                require(a.cards.cardsInHand == f.battle.cards.cardsInHand && ids(a.cards.discardPile) == ids(f.battle.cards.discardPile),
                        "resampling changed public generated zones");
                for (int i = 0; i < a.cards.cardsInHand; ++i)
                    require(a.cards.hand[i].getUniqueId() == f.battle.cards.hand[i].getUniqueId(), "sample changed hand identity");
            }
            if (id == CardId::POWER_THROUGH) continue;
            Fixture terminal;
            terminal.hand(id, up);
            terminal.battle.monsters.arr[0].curHp = 1;
            play(terminal.battle);
            require(terminal.battle.outcome == Outcome::PLAYER_VICTORY, "lethal attack did not finish combat");
            require(terminal.battle.cards.drawPile.empty() && count(terminal.battle.cards.discardPile, CardId::BURN) == 0,
                    "terminal cleanup must interrupt queued status generation");
        }
    }
}
}

int main() {
    try {
        verifyPowerThrough();
        verifyAttacks();
        verifyGeneratedStatusLifecycle();
        verifyExhaustInteractions();
        verifyResamplingAndTerminal();
        std::cout << "STATUS_INSERTION_CARD_RECIPES_OK (3 cards, 6 variants)\n";
    } catch (const std::exception &error) {
        std::cerr << "STATUS_INSERTION_CARD_RECIPES_FAILED: " << error.what() << '\n';
        return 1;
    }
}
