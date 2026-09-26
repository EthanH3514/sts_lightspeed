#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "sim/PublicCombatResampling.h"

using namespace sts;

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
auto cardWords(const CardInstance &card) {
    return std::make_tuple(card.id, card.uniqueId, card.specialData, card.upgraded,
                          card.cost, card.costForTurn, card.freeToPlayOnce, card.retain);
}
auto pileWords(const BattleContext &battle) {
    std::vector<decltype(cardWords(CardInstance{}))> words;
    for (const auto &card : battle.cards.drawPile) words.push_back(cardWords(card));
    return words;
}
auto rngWords(const Random &rng) {
    return std::make_tuple(rng.counter, rng.seed0, rng.seed1);
}
void verify() {
    GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
    game.floorNum = 1;
    game.enterBattle(MonsterEncounter::TWO_LOUSE);
    BattleContext original;
    original.init(game);
    original.executeActions();
    require(original.inputState == InputState::PLAYER_NORMAL, "fixture is not at a decision");
    require(original.turn == 0, "first-turn indexing assumption changed");
    // This supported fixture publicly reveals each louse's random base damage.
    for (int i = 0; i < original.monsters.monsterCount; ++i) {
        auto &monster = original.monsters.arr[i];
        monster.moveHistory[0] = monster.id == MonsterId::RED_LOUSE ?
            MMID::RED_LOUSE_BITE : MMID::GREEN_LOUSE_BITE;
    }
    const auto beforePile = pileWords(original);
    const auto beforeRng = rngWords(original.shuffleRng);
    std::array<std::uint64_t, 7> seeds {11, 22, 33, 44, 55, 66, 77};
    BattleContext first(original);
    BattleContext reversed(original);
    std::reverse(reversed.cards.drawPile.begin(), reversed.cards.drawPile.end());
    public_sampling::resampleCombatContinuation(game, first, seeds);
    public_sampling::resampleCombatContinuation(game, reversed, seeds);
    require(pileWords(first) == pileWords(reversed), "sample depends on original hidden pile order");
    require(pileWords(original) == beforePile && rngWords(original.shuffleRng) == beforeRng,
            "sampling mutated original battle");
    for (int i = 0; i < original.cards.cardsInHand; ++i) {
        require(cardWords(first.cards.hand[i]) == cardWords(original.cards.hand[i]),
                "sampling changed current hand");
    }
    require(first.player.energy == original.player.energy && first.turn == original.turn,
            "sampling changed current decision");
    require(first.monsters.arr[0].moveHistory[0] == original.monsters.arr[0].moveHistory[0],
            "sampling changed current monster intent");
    BattleContext other(original);
    seeds[5] = 67;
    public_sampling::resampleCombatContinuation(game, other, seeds);
    require(rngWords(other.shuffleRng) != rngWords(first.shuffleRng), "different samples share RNG");

    game.obtainRelic(RelicId::FROZEN_EYE);
    BattleContext frozen(original);
    public_sampling::resampleCombatContinuation(game, frozen, seeds);
    require(pileWords(frozen) == beforePile, "Frozen Eye draw order was changed");

    // Use a separate game without Frozen Eye to test unsupported public knowledge.
    GameContext constrained(CharacterClass::IRONCLAD, 123456789, 0);
    constrained.enterBattle(MonsterEncounter::TWO_LOUSE);
    constrained.deck.bottleIdxs[0] = original.cards.drawPile.back().uniqueId;
    bool bottleRejected = false;
    try {
        BattleContext bottled(original);
        public_sampling::resampleCombatContinuation(constrained, bottled, seeds);
    } catch (const std::runtime_error &) { bottleRejected = true; }
    require(bottleRejected, "undrawn bottled knowledge was silently erased");
    BattleContext lateBottled(original);
    lateBottled.turn = 2;
    bool lateRejected = false;
    try { public_sampling::resampleCombatContinuation(constrained, lateBottled, seeds); }
    catch (const std::runtime_error &) { lateRejected = true; }
    require(lateRejected, "remaining bottled knowledge was erased on a later turn");

    BattleContext queued(original);
    queued.addToBot(Actions::GainEnergy(1));
    bool queueRejected = false;
    try { public_sampling::resampleCombatContinuation(game, queued, seeds); }
    catch (const std::runtime_error &) { queueRejected = true; }
    require(queueRejected, "sampling accepted an unresolved queue");
    BattleContext unaudited(original);
    unaudited.encounter = MonsterEncounter::GREMLIN_GANG;
    bool encounterRejected = false;
    try { public_sampling::resampleCombatContinuation(game, unaudited, seeds); }
    catch (const std::runtime_error &) { encounterRejected = true; }
    require(encounterRejected, "sampling accepted an unaudited encounter");
    BattleContext unseenDamage(original);
    unseenDamage.monsters.arr[0].moveHistory[0] =
        unseenDamage.monsters.arr[0].id == MonsterId::RED_LOUSE ?
        MMID::RED_LOUSE_GROW : MMID::GREEN_LOUSE_SPIT_WEB;
    bool damageRejected = false;
    try { public_sampling::resampleCombatContinuation(game, unseenDamage, seeds); }
    catch (const std::runtime_error &) { damageRejected = true; }
    require(damageRejected, "sampling used unrevealed exact Louse damage");
}
}
int main() {
    try {
        verify();
        std::cout << "PUBLIC_COMBAT_RESAMPLING_OK\n";
    } catch (const std::exception &error) {
        std::cerr << "PUBLIC_COMBAT_RESAMPLING_FAILED: " << error.what() << '\n';
        return 1;
    }
}
