#include <cstdlib>
#include <iostream>
#include <vector>
#include "combat/BattleContext.h"

using namespace sts;
static int checks = 0;
static void check(bool condition, const char *message) {
    ++checks;
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    BattleContext battle;
    std::vector<int> seen;
    ActionQueue<3> q;
    auto add = [&](int id, bool keep = false) {
        return Action([&, id](BattleContext &) { seen.push_back(id); }, !keep);
    };
    q.pushBack(add(0)); q.pushBack(add(1)); q.pushBack(add(2));
    q.popFront()(battle);
    q.pushBack(add(3, true)); // wrapped, full
    q.pushFront(add(-1, true)); // grow a wrapped queue
    for (int i = 4; i < 200; ++i) q.pushBack(add(i, i % 2 == 0));
    auto copy = q;
    q.popFront()(battle);
    check(copy.size == 200 && q.size == 199, "action queue copies are isolated");
    check(!copy.bits[copy.front], "front victory flag survives growth");
    seen.clear();
    while (!copy.isEmpty()) copy.popFront()(battle);
    check(seen.size() == 200 && seen[0] == -1, "front insertion survives growth");
    for (int i = 1; i < 200; ++i) check(seen[i] == i, "action FIFO order");
    battle.actionQueue.clear(); seen.clear();
    for (int i = 0; i < 140; ++i) battle.actionQueue.pushBack(add(i, i % 3 == 0));
    battle.clearPostCombatActions();
    battle.addToBot(add(999, true));
    while (!battle.actionQueue.isEmpty()) battle.actionQueue.popFront()(battle);
    check(seen.size() == 48 && seen.back() == 999, "cleanup repairs tail for later enqueue");
    for (int i = 0; i < 47; ++i) check(seen[i] == i * 3, "cleanup keeps flags and order");
    CardQueue cards;
    auto item = [](int id) { CardQueueItem x; x.card.uniqueId = id; return x; };
    for (int i = 0; i < 10; ++i) cards.pushBack(item(i));
    for (int i = 0; i < 7; ++i) check(cards.popFront().card.uniqueId == i, "card initial FIFO");
    for (int i = 10; i < 100; ++i) cards.pushBack(item(i));
    cards.pushFront(item(6));
    check(cards.containsCardWithId(80) && !cards.containsCardWithId(5), "membership scans entire live queue");
    auto other = cards;
    check(cards.popBack().card.uniqueId == 99, "back pop after growth");
    check(other.size == 94 && cards.size == 93, "card queue copies are isolated");
    for (int i = 6; i < 100; ++i) check(other.popFront().card.uniqueId == i, "grown card FIFO");
    cards.clear(); cards.pushFront(item(7)); cards.pushBack(item(8));
    check(cards.popFront().card.uniqueId == 7 && cards.popBack().card.uniqueId == 8,
          "clear and reuse grown queue");
    std::cout << "COMBAT_QUEUE_GROWTH_OK " << checks << " checks\n";
}
