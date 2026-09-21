#include <iostream>

#include "combat/Actions.h"
#include "combat/BattleContext.h"

using namespace sts;

int main() {
    BattleContext emptyHand;
    emptyHand.inputState = InputState::EXECUTING_ACTIONS;

    Actions::ExhaustMany(3).actFunc(emptyHand);

    if (emptyHand.inputState != InputState::EXECUTING_ACTIONS) {
        std::cerr << "empty hand opened an impossible ExhaustMany selection\n";
        return 1;
    }

    BattleContext nonemptyHand;
    nonemptyHand.inputState = InputState::EXECUTING_ACTIONS;
    nonemptyHand.cards.hand[0] = CardInstance(CardId::STRIKE_RED);
    nonemptyHand.cards.cardsInHand = 1;

    Actions::ExhaustMany(3).actFunc(nonemptyHand);

    if (nonemptyHand.inputState != InputState::CARD_SELECT) {
        std::cerr << "nonempty hand did not open ExhaustMany selection\n";
        return 1;
    }
    if (nonemptyHand.cardSelectInfo.cardSelectTask != CardSelectTask::EXHAUST_MANY) {
        std::cerr << "nonempty hand opened the wrong selection task\n";
        return 1;
    }
    if (nonemptyHand.cardSelectInfo.pickCount != 3) {
        std::cerr << "nonempty hand changed the ExhaustMany pick count\n";
        return 1;
    }

    std::cout << "EXHAUST_MANY_EMPTY_HAND_OK\n";
    return 0;
}
