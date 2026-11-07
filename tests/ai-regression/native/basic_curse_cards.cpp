#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "game/SaveFile.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
constexpr std::array<CardId, 6> curses {CardId::INJURY, CardId::CLUMSY, CardId::WRITHE,
    CardId::ASCENDERS_BANE, CardId::CURSE_OF_THE_BELL, CardId::PARASITE};
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
bool ethereal(CardId id) { return id == CardId::CLUMSY || id == CardId::ASCENDERS_BANE; }
bool removable(CardId id) { return id != CardId::ASCENDERS_BANE && id != CardId::CURSE_OF_THE_BELL; }
struct Fixture {
    GameContext game;
    BattleContext bc;
    Fixture(bool candle = false, bool rod = false, bool frozen = false)
        : game(CharacterClass::IRONCLAD, 123456789, 0) {
        if (candle) game.obtainRelic(RelicId::BLUE_CANDLE);
        if (rod) game.obtainRelic(RelicId::TUNGSTEN_ROD);
        if (frozen) game.obtainRelic(RelicId::FROZEN_EYE);
        game.floorNum = 1; game.enterBattle(MonsterEncounter::CULTIST);
        bc.init(game); bc.executeActions();
        bc.cards = CardManager{}; bc.cards.nextUniqueCardId = 100;
    }
    CardInstance card(CardId id) {
        CardInstance c(id); c.setUniqueId(bc.cards.nextUniqueCardId++); return c;
    }
    void hand(CardId id) { bc.cards.moveToHand(card(id)); }
    void draw(CardId id) { bc.cards.drawPile.push_back(card(id)); }
    void resolve() { bc.inputState = InputState::EXECUTING_ACTIONS; bc.executeActions(); }
    void play() {
        auto c = bc.cards.hand[0]; check(c.canUse(bc, 0, false), "Blue Candle legal at zero energy");
        bc.addToBotCard(CardQueueItem(c, 0, bc.player.energy)); resolve();
    }
};
void traitsAndLifecycle() {
    for (auto id : curses) {
        Fixture f; f.hand(id);
        for (int i = 0; i < 8; ++i) f.draw(CardId::DEFEND_RED);
        auto c = f.bc.cards.hand[0];
        // The native catalogue uses -3 for unplayable curses; original game uses -2.
        // Both are distinct from X cost (-1); this check is behavioral, not wire parity.
        check(c.getType() == CardType::CURSE && c.cost < -1 && !c.canUse(f.bc, 0, false), "curse unplayable");
        check(!c.canUpgrade() && !Card(id).canUpgrade(), "curse cannot upgrade");
        check(c.isEthereal() == ethereal(id), "curse ethereal");
        check(Card(id).isInnate() == (id == CardId::WRITHE), "curse innate");
        check(Card(id).canTransform() == removable(id), "curse removal/transform eligibility");
        int hp = f.bc.player.curHp;
        f.bc.player.buff<PS::FEEL_NO_PAIN>(3); f.bc.player.buff<PS::DARK_EMBRACE>(1);
        f.bc.player.buff<PS::BARRICADE>(1);
        search::Action(search::ActionType::END_TURN).execute(f.bc);
        check(f.bc.player.curHp == hp, "basic curse has no end-turn HP effect");
        const auto &pile = ethereal(id) ? f.bc.cards.exhaustPile : f.bc.cards.discardPile;
        check(pile.size() == 1 && pile[0].getUniqueId() == c.getUniqueId(), "curse end-turn identity");
        check(f.bc.player.block == (ethereal(id) ? 3 : 0), "ethereal exhaust power");
    }
    for (int seed = 1; seed <= 16; ++seed) {
        GameContext gc(CharacterClass::IRONCLAD, seed, 0);
        gc.deck.obtainRaw(Card(CardId::WRITHE)); gc.floorNum = 1;
        gc.enterBattle(MonsterEncounter::CULTIST);
        BattleContext bc; bc.init(gc); bc.executeActions();
        bool found = false;
        for (int i = 0; i < bc.cards.cardsInHand; ++i) found |= bc.cards.hand[i].id == CardId::WRITHE;
        check(found, "Writhe starts in opening hand");
    }
}
void drawAndAutoplay() {
    for (auto id : curses) {
        Fixture f; f.draw(CardId::DEFEND_RED); f.draw(id);
        f.bc.player.buff<PS::EVOLVE>(2); f.bc.player.buff<PS::FIRE_BREATHING>(6);
        int hp = f.bc.monsters.arr[0].curHp;
        f.bc.addToBot(Actions::DrawCards(1)); f.resolve();
        check(f.bc.cards.cardsInHand == 1 && f.bc.cards.drawPile.size() == 1, "Evolve excludes curses");
        check(hp - f.bc.monsters.arr[0].curHp == 6, "Fire Breathing includes curses");
        for (bool candle : {false, true}) for (bool exhaust : {false, true}) {
            Fixture autoPlay(candle); autoPlay.draw(id); autoPlay.bc.player.energy = 0;
            int playerHp = autoPlay.bc.player.curHp;
            autoPlay.bc.player.buff<PS::FEEL_NO_PAIN>(3);
            autoPlay.bc.playTopCardInDrawPile(0, exhaust); autoPlay.resolve();
            check(playerHp - autoPlay.bc.player.curHp == int(candle), "autoplay only Candle causes HP loss");
            check(autoPlay.bc.cards.exhaustPile.size() == static_cast<unsigned>(exhaust || candle), "autoplay curse exhaust");
            check(autoPlay.bc.cards.discardPile.size() == static_cast<unsigned>(!exhaust && !candle), "rejected curse discard");
            check(autoPlay.bc.player.block == ((exhaust || candle) ? 3 : 0), "autoplay exhaust callbacks");
        }
    }
}
void candleInteractions() {
    for (auto id : curses) for (bool rod : {false, true}) for (bool buffer : {false, true}) {
        Fixture f(true, rod); f.hand(id); f.draw(CardId::DEFEND_RED);
        f.bc.player.energy = 0; f.bc.player.block = 10;
        f.bc.player.buff<PS::RUPTURE>(2); f.bc.player.buff<PS::FEEL_NO_PAIN>(3);
        f.bc.player.buff<PS::DARK_EMBRACE>(1);
        if (buffer) f.bc.player.buff<PS::BUFFER>(1);
        int hp = f.bc.player.curHp, maxHp = f.bc.player.maxHp;
        f.play();
        check(hp - f.bc.player.curHp == int(!rod && !buffer), "Candle loss bypasses block; Buffer/Rod prevent");
        check(f.bc.player.strength == ((!rod && !buffer) ? 2 : 0), "Candle self-loss Rupture");
        check(f.bc.player.block == 13 && f.bc.player.energy == 0, "Candle exhaust block and no energy payment");
        check(f.bc.cards.exhaustPile.size() == 1 && f.bc.cards.cardsInHand == 1, "Candle exhaust/draw exactly once");
        check(f.bc.player.maxHp == maxHp, "combat exhaust is not master-deck removal");
        if (buffer) check(!f.bc.player.hasStatus<PS::BUFFER>(), "Buffer consumed before Rod");
    }
    Fixture lethal(true); lethal.hand(CardId::INJURY); lethal.bc.player.curHp = 1;
    lethal.play(); check(lethal.bc.outcome == Outcome::PLAYER_LOSS, "Candle can be lethal");
}
void masterDeckRemoval() {
    for (auto id : curses) {
        GameContext gc(CharacterClass::IRONCLAD, 9, 0);
        gc.deck = Deck{}; gc.deck.obtainRaw(Card(id));
        check(gc.deck.getTransformableCount() == int(removable(id)), "deck eligibility count");
        if (!removable(id)) continue; // Low-level remove assumes a legal selected card.
        gc.maxHp = 80; gc.curHp = 79;
        gc.deck.remove(gc, 0);
        check(gc.deck.size() == 0, "master removal membership");
        check(gc.maxHp == (id == CardId::PARASITE ? 77 : 80), "Parasite master removal max HP");
        check(gc.curHp == (id == CardId::PARASITE ? 77 : 79), "master removal current HP clamp");
    }
    SaveFile saved{};
    saved.bottledCards = {CardId::STRIKE_RED, CardId::INVALID, CardId::INVALID};
    saved.cards = {Card(CardId::STRIKE_RED), Card(CardId::DEFEND_RED),
                   Card(CardId::INFLAME), Card(CardId::INJURY),
                   Card(CardId::ASCENDERS_BANE), Card(CardId::CURSE_OF_THE_BELL)};
    Deck imported;
    imported.initFromSaveFile(saved);
    check(imported.size() == 6, "save import keeps all identities");
    check(imported.getTransformableCount() == 3, "save import counts eligible cards once and excludes bottle");
    check(imported.getTransformableCount(-1, true) == 4, "save import bottle-inclusive count");
    check(imported.getUpgradeableCount() == 3, "save import only three upgradeable cards");
    for (int type = 0; type < 4; ++type)
        check(imported.cardTypeCounts[type] == 1, "save import selectable type count not doubled");
    GameContext ordinary(CharacterClass::IRONCLAD, 17, 0);
    ordinary.deck = Deck{};
    for (const auto &c : saved.cards) ordinary.deck.obtain(ordinary, c);
    ordinary.deck.bottleCard(0, CardType::ATTACK);
    check(ordinary.deck.transformableCount == imported.transformableCount &&
          ordinary.deck.cardTypeCounts == imported.cardTypeCounts &&
          ordinary.deck.upgradeableCount == imported.upgradeableCount, "raw import matches ordinary acquisition without relics");
}
void publicSampling() {
    const std::array<std::uint64_t, 7> seeds {11,22,33,44,55,66,77};
    for (auto id : curses) for (bool frozen : {false, true}) {
        Fixture f(true, false, frozen); f.hand(id);
        f.draw(CardId::BASH); f.draw(CardId::DEFEND_RED); f.draw(CardId::STRIKE_RED);
        auto a = f.bc, b = f.bc;
        if (!frozen) std::reverse(b.cards.drawPile.begin(), b.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game, a, seeds);
        public_sampling::resampleCombatContinuation(f.game, b, seeds);
        for (std::size_t i = 0; i < a.cards.drawPile.size(); ++i) {
            check(a.cards.drawPile[i].getUniqueId() == b.cards.drawPile[i].getUniqueId(), "curse hidden-order invariance");
            if (frozen) check(a.cards.drawPile[i].getUniqueId() == f.bc.cards.drawPile[i].getUniqueId(), "curse Frozen Eye order");
        }
        check(a.cards.hand[0].getUniqueId() == f.bc.cards.hand[0].getUniqueId(), "curse public identity");
    }
}
}
int main() {
    try {
        traitsAndLifecycle(); drawAndAutoplay(); candleInteractions(); masterDeckRemoval(); publicSampling();
        std::cout << "BASIC_CURSE_CARDS_OK (" << checks << " checks)\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
