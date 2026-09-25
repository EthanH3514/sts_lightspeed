// Standalone regression: upstream master plus only the Impatience fix.
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
using namespace sts;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { ++failures; if (failures <= 20) std::cerr << "FAIL: " << message << '\n'; }
}
struct Fixture {
    BattleContext b;
    Fixture() {
        GameContext game(CharacterClass::IRONCLAD, 123456789, 0);
        game.relics = {}; game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game); b.executeActions();
        b.cards = CardManager{}; b.cards.nextUniqueCardId = 100;
        b.player.energy = 0; b.monsters.arr[0].curHp = b.monsters.arr[0].maxHp = 1000;
        for (int i = 0; i < 5; ++i) b.cards.createTempCardInDrawPile(i, CardInstance(CardId::DEFEND_RED));
    }
    void run() { b.inputState = InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void play(bool up) {
        b.cards.createTempCardInHand(CardInstance(CardId::IMPATIENCE, up));
        const auto c = b.cards.hand[b.cards.cardsInHand - 1];
        check(c.canUse(b, 0, false), "Impatience stays legal with an attack in hand");
        b.addToBotCard(CardQueueItem(c, 0, b.player.energy)); run();
    }
};
void ordinary() {
    for (bool up : {false, true}) for (bool blocked : {false, true})
    for (auto other : {CardId::DEFEND_RED, CardId::STRIKE_RED, CardId::BLUDGEON, CardId::CLASH})
    for (int occupied : {1, 9}) {
        Fixture f;
        f.b.cards.createTempCardInHand(CardInstance(other));
        for (int i = 1; i < occupied; ++i) f.b.cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        if (blocked) f.b.player.buff<PS::NO_DRAW>();
        const bool hasAttack = other != CardId::DEFEND_RED;
        const int draw = blocked || hasAttack ? 0 : std::min(up ? 3 : 2, 10 - occupied);
        f.play(up);
        check(f.b.cards.cardsInHand == occupied + draw, "only an attack-free hand permits drawing");
        check(f.b.cards.drawPile.size() == static_cast<unsigned>(5 - draw), "conditional draw consumes the expected supply");
        check(f.b.player.energy == 0, "Impatience spends no energy");
        check(f.b.cards.discardPile.size() == 1 && f.b.cards.exhaustPile.empty(), "ordinary play discards even when drawing is blocked");
    }
}
void resolutionTiming() {
    for (bool up : {false, true}) for (bool addAttack : {false, true}) {
        Fixture f;
        if (!addAttack) f.b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
        // Deliberately construct an earlier queued hand change: this isolates
        // resolution timing without claiming a naturally captured game sequence.
        f.b.addToBot({[=](BattleContext &b) {
            if (addAttack) b.cards.createTempCardInHand(CardInstance(CardId::STRIKE_RED));
            else {
                const auto c = b.cards.hand[0]; b.cards.removeFromHandAtIdx(0);
                b.cards.moveToDiscardPile(c);
            }
        }});
        f.b.curCardQueueItem = CardQueueItem(CardInstance(CardId::IMPATIENCE, up), 0, 0);
        f.b.useSkillCard();
        int observedByNextAction = -1;
        f.b.addToBot({[&observedByNextAction](BattleContext &b) {
            observedByNextAction = b.cards.cardsInHand;
        }});
        f.run();
        const int expected = addAttack ? 1 : up ? 3 : 2;
        check(f.b.cards.cardsInHand == expected, "condition uses the hand at queued resolution");
        check(observedByNextAction == expected, "successful conditional draw executes before later queued actions");
    }
}
void autoplay() {
    for (bool up : {false, true}) for (bool attack : {false, true}) {
        Fixture f;
        f.b.cards.createTempCardInHand(CardInstance(attack ? CardId::STRIKE_RED : CardId::DEFEND_RED));
        f.b.cards.createTempCardInDrawPile(5, CardInstance(CardId::IMPATIENCE, up));
        f.b.playTopCardInDrawPile(0, true); f.run();
        check(f.b.cards.cardsInHand == (attack ? 1 : up ? 4 : 3), "autoplay obeys the same no-attack condition");
        check(f.b.cards.exhaustPile.size() == 1 && f.b.cards.exhaustPile[0].id == CardId::IMPATIENCE, "successful autoplay still exhausts its source");
    }
}
}
int main() {
    ordinary(); resolutionTiming(); autoplay();
    std::cout << "IMPATIENCE_CONDITION_" << (failures ? "FAILED" : "OK")
              << " (" << failures << " failures / " << checks << " checks)\n";
    return failures ? 1 : 0;
}
