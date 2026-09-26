#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct Fixture {
    GameContext game;
    BattleContext bc;
    Fixture(RelicId relic = RelicId::INVALID) : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (relic != RelicId::INVALID) game.obtainRelic(relic);
        game.floorNum = 1;
        game.enterBattle(MonsterEncounter::CULTIST);
        bc.init(game); bc.executeActions();
        bc.cards = CardManager{}; bc.cards.nextUniqueCardId = 100;
    }
    CardInstance card(CardId id, bool up = false) {
        CardInstance c(id, up); c.setUniqueId(bc.cards.nextUniqueCardId++); return c;
    }
    void hand(CardId id, bool up = false) { bc.cards.moveToHand(card(id, up)); }
    void draw(CardId id) { bc.cards.drawPile.push_back(card(id)); }
    void resolve() { bc.inputState = InputState::EXECUTING_ACTIONS; bc.executeActions(); }
    void play() {
        auto c = bc.cards.hand[0]; check(c.canUse(bc, 0, false), "play legality");
        bc.addToBotCard(CardQueueItem(c, 0, bc.player.energy)); resolve();
    }
};
void passiveLifecycle() {
    for (auto id : {CardId::WOUND, CardId::DAZED, CardId::BURN, CardId::VOID}) {
        for (bool up : {false, true}) {
            Fixture f; f.hand(id, up);
            for (int i = 0; i < 6; ++i) f.draw(CardId::DEFEND_RED);
            auto c = f.bc.cards.hand[0];
            check(c.cost == -2 && !c.canUse(f.bc, 0, false), "unplayable status");
            const bool ethereal = id == CardId::DAZED || id == CardId::VOID;
            check(c.isEthereal() == ethereal, "ethereal status trait");
            int hp = f.bc.player.curHp;
            search::Action(search::ActionType::END_TURN).execute(f.bc);
            check(hp - f.bc.player.curHp == (id == CardId::BURN ? (up ? 4 : 2) : 0), "passive end-turn damage");
            const auto &pile = ethereal ? f.bc.cards.exhaustPile : f.bc.cards.discardPile;
            check(pile.size() == 1 && pile[0].getUniqueId() == c.getUniqueId(), "passive lifecycle identity");
        }
    }
}
void drawAndMove() {
    for (int energy : {0, 1, 3}) {
        Fixture f; f.draw(CardId::VOID); f.bc.player.energy = energy;
        f.bc.addToBot(Actions::DrawCards(1)); f.resolve();
        check(f.bc.player.energy == std::max(0, energy - 1), "Void draw loss floor");
        check(f.bc.cards.cardsInHand == 1, "Void draw destination");
        Fixture moved; moved.bc.player.energy = energy;
        moved.bc.addToBot(Actions::MakeTempCardInHand(CardId::VOID, false, 1)); moved.resolve();
        check(moved.bc.player.energy == energy, "generation is not draw");
    }
    Fixture blocked; blocked.draw(CardId::VOID); blocked.bc.player.energy = 3;
    blocked.bc.player.buff<PS::NO_DRAW>(1);
    blocked.bc.addToBot(Actions::DrawCards(1)); blocked.resolve();
    check(blocked.bc.player.energy == 3 && blocked.bc.cards.drawPile.size() == 1, "No Draw prevents callback");
    Fixture full; for (int i = 0; i < 10; ++i) full.hand(CardId::WOUND);
    full.draw(CardId::VOID); full.bc.player.energy = 3;
    full.bc.addToBot(Actions::DrawCards(1)); full.resolve();
    check(full.bc.player.energy == 3 && full.bc.cards.drawPile.size() == 1, "full hand prevents callback");
}
void interactions() {
    for (bool upgraded : {false, true}) {
        Fixture f; f.hand(CardId::BURN, upgraded); f.bc.player.block = 3;
        for (int i = 0; i < 6; ++i) f.draw(CardId::DEFEND_RED);
        int hp = f.bc.player.curHp;
        search::Action(search::ActionType::END_TURN).execute(f.bc);
        check(hp - f.bc.player.curHp == (upgraded ? 1 : 0), "Burn is blockable");
    }
    Fixture exhaust; exhaust.hand(CardId::DAZED); exhaust.hand(CardId::VOID);
    for (int i = 0; i < 8; ++i) exhaust.draw(CardId::DEFEND_RED);
    exhaust.bc.player.buff<PS::FEEL_NO_PAIN>(3);
    exhaust.bc.player.buff<PS::DARK_EMBRACE>(1);
    exhaust.bc.player.buff<PS::BARRICADE>(1);
    search::Action(search::ActionType::END_TURN).execute(exhaust.bc);
    check(exhaust.bc.player.block == 6 && exhaust.bc.cards.exhaustPile.size() == 2, "ethereal exhaust callbacks");
    for (auto id : {CardId::WOUND, CardId::DAZED, CardId::BURN, CardId::VOID}) {
        Fixture draw; draw.draw(CardId::DEFEND_RED); draw.draw(id);
        draw.bc.player.buff<PS::EVOLVE>(1); draw.bc.player.buff<PS::FIRE_BREATHING>(6);
        int hp = draw.bc.monsters.arr[0].curHp;
        draw.bc.addToBot(Actions::DrawCards(1)); draw.resolve();
        check(draw.bc.cards.cardsInHand == 2 && hp - draw.bc.monsters.arr[0].curHp == 6, "status draw powers");
        Fixture kit(RelicId::MEDICAL_KIT); kit.hand(id); kit.bc.player.energy = 0;
        kit.bc.player.buff<PS::FEEL_NO_PAIN>(3); int playerHp = kit.bc.player.curHp;
        kit.play();
        check(kit.bc.cards.exhaustPile.size() == 1 && kit.bc.player.block == 3 &&
              kit.bc.player.energy == 0 && kit.bc.player.curHp == playerHp, "Medical Kit play is not draw/end turn");
    }
}
void voidQueuedLoss() {
    Fixture shuffle(RelicId::SUNDIAL);
    shuffle.hand(CardId::BATTLE_TRANCE);
    shuffle.bc.cards.discardPile.push_back(shuffle.card(CardId::VOID));
    shuffle.bc.player.sundialCounter = 2; shuffle.bc.player.energy = 0;
    shuffle.play();
    check(shuffle.bc.player.energy == 1, "Void draw during Sundial shuffle must resolve after energy gain");
    Fixture f(RelicId::INK_BOTTLE);
    f.hand(CardId::SEEING_RED, true); f.draw(CardId::VOID);
    f.bc.player.inkBottleCounter = 9; f.bc.player.energy = 0;
    f.play();
    check(f.bc.cards.cardsInHand == 1 && f.bc.cards.hand[0].id == CardId::VOID, "Ink Bottle draws Void");
    check(f.bc.player.energy == 1, "Ink Bottle Void loss after Seeing Red gain");
}
void publicSampling() {
    const std::array<std::uint64_t, 7> seeds {11,22,33,44,55,66,77};
    for (bool frozen : {false, true}) {
        Fixture f(frozen ? RelicId::FROZEN_EYE : RelicId::INVALID);
        f.hand(CardId::BURN, true); f.hand(CardId::VOID);
        f.draw(CardId::WOUND); f.draw(CardId::DAZED); f.draw(CardId::DEFEND_RED);
        auto a = f.bc, b = f.bc;
        if (!frozen) std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, a, seeds);
        public_sampling::resampleCombatContinuation(f.game, b, seeds);
        for (std::size_t i = 0; i < a.cards.drawPile.size(); ++i) {
            check(a.cards.drawPile[i].getUniqueId() == b.cards.drawPile[i].getUniqueId(), "hidden order invariance");
            if (frozen) check(a.cards.drawPile[i].getUniqueId() == f.bc.cards.drawPile[i].getUniqueId(), "Frozen Eye order");
        }
        check(a.cards.hand[0].isUpgraded() && a.cards.hand[1].id == CardId::VOID, "public variants preserved");
    }
}
}
int main() {
    try {
        passiveLifecycle(); drawAndMove(); interactions(); publicSampling(); voidQueuedLoss();
        std::cout << "PASSIVE_STATUS_CARDS_OK\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
