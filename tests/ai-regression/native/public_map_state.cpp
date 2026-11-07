#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "sim/PublicMapState.h"
using namespace sts;
namespace {
int checks = 0;
void check(bool ok, const char *message) { ++checks; if (!ok) throw std::runtime_error(message); }
std::string signature(const public_map::State &state) {
    std::ostringstream out;
    out << state.available << ':' << state.act << ':' << state.position.x << ':' << state.position.y
        << ':' << static_cast<int>(state.boss) << ':' << state.bossPosition.x << ':' << state.bossPosition.y;
    for (auto p : state.starts) out << 'S' << p.x << ',' << p.y;
    for (const auto &node : state.nodes) {
        out << 'N' << node.position.x << ',' << node.position.y << ',' << static_cast<int>(node.room)
            << ',' << node.burningElite;
        for (auto p : node.next) out << 'E' << p.x << ',' << p.y;
    }
    return out.str();
}
void verify(const GameContext &game) {
    const auto state = public_map::capture(game);
    check(state.available && state.act == game.act, "current act available");
    check(state.boss == game.boss, "only currently displayed primary boss");
    std::set<std::pair<int,int>> points;
    int burning = 0, bosses = 0;
    for (const auto &node : state.nodes) {
        check(points.insert({node.position.x, node.position.y}).second, "unique public nodes");
        check(node.room != Room::NONE && node.room != Room::INVALID, "no invisible room placeholders");
        burning += node.burningElite;
        bosses += node.room == Room::BOSS;
        if (node.burningElite) check(node.room == Room::ELITE, "public flame is on an elite");
    }
    check(bosses == 1, "single public boss node");
    check(burning == (game.act == 4 || game.greenKey ? 0 : 1), "no hidden burning modifier");
    for (auto p : state.starts) check(p.y == 0 && points.count({p.x,p.y}), "start is a real first-row node");
    for (const auto &node : state.nodes) {
        std::set<std::pair<int,int>> next;
        for (auto p : node.next) {
            check(points.count({p.x,p.y}) && p.y == node.position.y + 1, "edge reaches real next-row node");
            check(next.insert({p.x,p.y}).second, "no duplicate edge");
        }
    }
}
}
int main() {
    try {
        for (int ascension : {0, 20}) for (int seed : {7, 42, 123456789}) {
            GameContext game(CharacterClass::IRONCLAD, seed, ascension);
            verify(game);
            check(game.curEvent == Event::NEOW && public_map::capture(game).position.y == -1,
                  "public map exists before choosing Neow");
            const auto before = signature(public_map::capture(game));
            game.seed = 0; game.neowRng.setCounter(99); game.monsterRng.setCounter(101);
            game.secondBoss = MonsterEncounter::THE_HEART;
            game.monsterList.clear(); game.eliteMonsterList.clear(); game.eventList.clear();
            game.commonRelicPool.clear(); game.map->burningEliteBuff = 99;
            check(before == signature(public_map::capture(game)), "hidden data cannot change projection");
            for (int act : {2, 3, 4}) { game.transitionToAct(act); verify(game); }
            check(public_map::capture(game).nodes.size() == 4, "Act 4 fixed graph");
        }
        GameContext game(CharacterClass::IRONCLAD, 42, 0);
        game.curMapNodeX = 0; game.curMapNodeY = 15;
        check(public_map::capture(game).position.x == 3, "boss command is not a column");
        game.greenKey = true; verify(game);
        game.map.reset(); check(!public_map::capture(game).available, "missing map is explicit");
        std::cout << "PUBLIC_MAP_STATE_PASS (" << checks << " checks)\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
