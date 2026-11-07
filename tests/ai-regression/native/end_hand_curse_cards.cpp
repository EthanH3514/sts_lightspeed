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
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
constexpr std::array<CardId, 4> ids {CardId::DECAY, CardId::DOUBT, CardId::SHAME, CardId::REGRET};
struct Fixture {
    GameContext game;
    BattleContext bc;
    Fixture(RelicId relic = RelicId::INVALID) : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (relic != RelicId::INVALID) game.obtainRelic(relic);
        game.floorNum = 1; game.enterBattle(MonsterEncounter::CULTIST);
        bc.init(game); bc.executeActions();
        bc.cards = CardManager{}; bc.cards.nextUniqueCardId = 100;
        for (int i = 0; i < 12; ++i) bc.cards.drawPile.push_back(card(CardId::DEFEND_RED));
    }
    CardInstance card(CardId id) { CardInstance c(id); c.setUniqueId(bc.cards.nextUniqueCardId++); return c; }
    void hand(CardId id) { bc.cards.moveToHand(card(id)); }
    void resolve() { bc.inputState = InputState::EXECUTING_ACTIONS; bc.executeActions(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(bc); }
};
void lifecycle() {
    for (auto id : ids) {
        Fixture f; f.hand(id); f.hand(CardId::DEFEND_RED);
        const auto c = f.bc.cards.hand[0];
        check(!c.canUse(f.bc, 0, false) && c.cost < -1, "unplayable curse");
        int hp = f.bc.player.curHp, enemyHp = f.bc.monsters.arr[0].curHp;
        f.bc.player.buff<PS::PANACHE>(10); const auto counter = f.bc.player.panacheCounter;
        f.end();
        check(hp - f.bc.player.curHp == (id == CardId::DECAY || id == CardId::REGRET ? 2 : 0), "end-hand effect");
        check(f.bc.cards.discardPile.size() == 2 && f.bc.cards.exhaustPile.empty(), "discard not exhaust");
        check(std::any_of(f.bc.cards.discardPile.begin(), f.bc.cards.discardPile.end(), [&](const auto &x) {
            return x.getUniqueId() == c.getUniqueId(); }), "identity conserved");
        check(f.bc.monsters.arr[0].curHp == enemyHp && f.bc.player.panacheCounter == counter,
              "passive callback is not a normal card play");
        check(f.bc.player.getStatus<PS::WEAK>() == (id == CardId::DOUBT ? 1 : 0), "Doubt fresh duration");
        check(f.bc.player.getStatus<PS::FRAIL>() == (id == CardId::SHAME ? 1 : 0), "Shame fresh duration");
    }
}
void damageAndLoss() {
    for (auto id : {CardId::DECAY, CardId::REGRET}) {
        for (int block : {0, 1, 9}) for (auto relic : {RelicId::INVALID, RelicId::TUNGSTEN_ROD}) {
            Fixture f(relic); f.hand(id); f.hand(CardId::DEFEND_RED); f.hand(CardId::DEFEND_RED);
            f.bc.player.block = block; f.bc.player.buff<PS::RUPTURE>(2);
            f.bc.player.buff<PS::STRENGTH>(20); f.bc.player.debuff<PS::WEAK>(2);
            const int hp = f.bc.player.curHp;
            int loss = id == CardId::DECAY ? std::max(0, 2 - block) : 3;
            loss = std::max(0, loss - (relic == RelicId::TUNGSTEN_ROD ? 1 : 0));
            f.end();
            check(hp - f.bc.player.curHp == loss, "blockable damage vs block-bypassing HP loss / Rod");
            check(f.bc.player.strength == 20 + (loss > 0 ? 2 : 0), "self source Rupture only on actual loss");
        }
        Fixture buffer; buffer.hand(id); buffer.bc.player.buff<PS::BUFFER>(1);
        const int hp = buffer.bc.player.curHp; buffer.end();
        check(buffer.bc.player.curHp == hp && !buffer.bc.player.hasStatus<PS::BUFFER>(), "Buffer prevents passive loss");
    }
}
template<PS power> void debuffCases(CardId id, RelicId immunity) {
    for (int existing : {0, 1, 3}) for (bool fresh : {false, true}) {
        Fixture f; f.hand(id);
        if (existing) f.bc.player.debuff<power>(existing, fresh);
        f.end();
        const int expected = existing == 0 ? 1 : existing + (fresh ? 1 : 0);
        check(f.bc.player.getStatus<power>() == expected, "stack preserves existing justApplied, not incoming flag");
    }
    Fixture artifact; artifact.hand(id); artifact.bc.player.buff<PS::ARTIFACT>(1); artifact.end();
    check(!artifact.bc.player.hasStatus<power>() && !artifact.bc.player.hasStatus<PS::ARTIFACT>(), "Artifact blocks debuff");
    Fixture immune(immunity); immune.hand(id); immune.bc.player.buff<PS::ARTIFACT>(1); immune.end();
    check(!immune.bc.player.hasStatus<power>() && immune.bc.player.getStatus<PS::ARTIFACT>() == 1,
          "relic immunity preserves Artifact");
}
void regretSnapshot() {
    for (bool reversed : {false, true}) {
        Fixture f;
        if (reversed) { f.hand(CardId::REGRET); f.hand(CardId::DECAY); }
        else { f.hand(CardId::DECAY); f.hand(CardId::REGRET); }
        f.hand(CardId::REGRET); f.hand(CardId::CLUMSY);
        f.bc.player.buff<PS::DARK_EMBRACE>(1);
        const int hp = f.bc.player.curHp; f.end();
        check(hp - f.bc.player.curHp == 10, "each Regret snapshots four cards including self before removals/exhaust draws");
        check(f.bc.cards.exhaustPile.size() == 1 && f.bc.cards.exhaustPile[0].id == CardId::CLUMSY,
              "ethereal exhaustion follows end-hand callbacks");
    }
}
void candleAndAutoplay() {
    for (auto id : ids) {
        Fixture candle(RelicId::BLUE_CANDLE); candle.hand(id); candle.bc.player.energy = 0;
        candle.bc.player.buff<PS::FEEL_NO_PAIN>(3);
        const int hp = candle.bc.player.curHp;
        check(candle.bc.cards.hand[0].canUse(candle.bc, 0, false), "Candle at zero energy");
        candle.bc.addToBotCard(CardQueueItem(candle.bc.cards.hand[0], 0, 0)); candle.resolve();
        check(hp - candle.bc.player.curHp == 1 && candle.bc.player.block == 3, "Candle cost only, no passive curse callback");
        check(!candle.bc.player.hasStatus<PS::WEAK>() && !candle.bc.player.hasStatus<PS::FRAIL>(), "Candle does not apply end-hand debuff");
        check(candle.bc.cards.exhaustPile.size() == 1, "Candle exhaust");
        for (bool exhaust : {false, true}) {
            Fixture automatic; automatic.bc.cards.drawPile.push_back(automatic.card(id));
            const int before = automatic.bc.player.curHp;
            automatic.bc.addToBot(Actions::PlayTopCard(0, exhaust)); automatic.resolve();
            check(automatic.bc.player.curHp == before && !automatic.bc.player.hasStatus<PS::WEAK>() &&
                  !automatic.bc.player.hasStatus<PS::FRAIL>(), "rejected autoplay has no end-hand callback");
            check((exhaust ? automatic.bc.cards.exhaustPile : automatic.bc.cards.discardPile).size() == 1,
                  "rejected autoplay settles card identity");
        }
    }
}
void resampling() {
    const std::array<std::uint64_t, 7> seeds {11,22,33,44,55,66,77};
    for (bool frozen : {false, true}) {
        Fixture f(frozen ? RelicId::FROZEN_EYE : RelicId::INVALID);
        for (auto id : ids) { f.hand(id); f.bc.cards.drawPile.push_back(f.card(id)); }
        auto a = f.bc, b = f.bc;
        if (!frozen) std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, a, seeds);
        public_sampling::resampleCombatContinuation(f.game, b, seeds);
        for (std::size_t i = 0; i < a.cards.drawPile.size(); ++i) {
            check(a.cards.drawPile[i].getUniqueId() == b.cards.drawPile[i].getUniqueId(), "hidden order invariant");
            if (frozen) check(a.cards.drawPile[i].getUniqueId() == f.bc.cards.drawPile[i].getUniqueId(), "Frozen Eye order retained");
        }
    }
}
}
int main() {
    try {
        lifecycle(); damageAndLoss(); debuffCases<PS::WEAK>(CardId::DOUBT, RelicId::GINGER);
        debuffCases<PS::FRAIL>(CardId::SHAME, RelicId::TURNIP); regretSnapshot(); candleAndAutoplay(); resampling();
        std::cout << "END_HAND_CURSE_CARDS_OK checks=" << checks << '\n';
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
