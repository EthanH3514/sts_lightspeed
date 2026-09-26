// Reuse setup only; the foundation remains a separate regression executable.
#define main mayhem_foundation_entry
#include "mayhem_foundation.cpp"
#undef main
int main() {
    for (bool frozen : {false, true}) for (bool known : {false, true})
    for (int stacks = 1; stacks <= 8; ++stacks) for (int seed = 1; seed <= 4; ++seed) {
        Fixture f(frozen ? RelicId::FROZEN_EYE : RelicId::INVALID);
        for (int i = 0; i < 20; ++i) f.top(i % 2 ? CardId::STRIKE_RED : CardId::DEFEND_RED);
        for (int i = 0; i < 3; ++i) f.b.cards.publicKnownBottomIds.push_back(f.b.cards.drawPile[i].uniqueId);
        const auto bottom = f.b.cards.publicKnownBottomIds;
        const int top = f.b.cards.drawPile.back().uniqueId;
        f.b.player.buff<PS::MAYHEM>(stacks);
        auto twin = f.b;
        if (!frozen) std::reverse(twin.cards.drawPile.begin() + 3,
                                 known ? twin.cards.drawPile.end() - 1 : twin.cards.drawPile.end());
        twin.cardRandomRng = Random(999999); twin.shuffleRng = Random(555555);
        std::array<std::uint64_t, 7> seeds = {11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds, known ? top : -1);
        public_sampling::resampleCombatContinuation(f.game, twin, seeds, known ? top : -1);
        f.turn(); twin.skipMonsterTurn = true; twin.afterMonsterTurns();
        twin.inputState = InputState::EXECUTING_ACTIONS; twin.executeActions();
        check(f.b.player.cardsPlayedThisTurn == stacks, "reviewed 1..8 stacks each resolve a play");
        check(f.b.cards.publicKnownBottomIds == bottom && twin.cards.publicKnownBottomIds == bottom,
              "normal draw plus autoplay preserve untouched known bottom order");
        check(f.b.monsters.arr[0].curHp == twin.monsters.arr[0].curHp && f.b.player.block == twin.player.block,
              "paired roots ignore old hidden RNG and unknown middle order");
        check(f.b.cards.cardsInHand == 5 && twin.cards.cardsInHand == 5, "five normal draws precede the plays");
        for (int i = 0; i < 5; ++i)
            check(f.b.cards.hand[i].uniqueId == twin.cards.hand[i].uniqueId, "paired public draw identities match");
        if (known || frozen) check(f.b.cards.hand[0].uniqueId == top, "known top is drawn, not treated as future autoplay target");
        public_sampling::resampleCombatContinuation(f.game, f.b, seeds);
        check(f.b.cards.publicKnownBottomIds == bottom, "resolved post-turn root keeps bottom history");
    }
    Fixture consumed; for (int i = 0; i < 7; ++i) consumed.top(CardId::STRIKE_RED);
    for (const auto &c : consumed.b.cards.drawPile) consumed.b.cards.publicKnownBottomIds.push_back(c.uniqueId);
    consumed.b.player.buff<PS::MAYHEM>(2); consumed.turn();
    check(consumed.b.cards.drawPile.empty() && consumed.b.cards.publicKnownBottomIds.empty(),
          "normal draw and autoplay both remove consumed bottom identities");
    std::cout << "MAYHEM_PUBLIC_CONSTRAINTS " << failures << " failures / " << checks << " checks\n";
    return failures ? 1 : 0;
}
