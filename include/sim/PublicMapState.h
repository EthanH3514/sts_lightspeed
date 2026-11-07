#ifndef STS_PUBLIC_MAP_STATE_H
#define STS_PUBLIC_MAP_STATE_H

#include <vector>
#include "game/GameContext.h"

namespace sts::public_map {

// Only the CURRENT act's displayed map. In particular, burningEliteBuff,
// secondBoss, encounter/event lists, reward pools and RNG are never projected.
struct Point { int x; int y; };
struct Node {
    Point position;
    Room room;
    bool burningElite;
    std::vector<Point> next;
};
struct State {
    bool available = false;
    int act = 0;
    Point position{-1, -1};
    Point bossPosition{3, 15};
    MonsterEncounter boss = MonsterEncounter::INVALID;
    std::vector<Point> starts;
    std::vector<Node> nodes;
};

inline State capture(const GameContext &game) {
    State result;
    result.act = game.act;
    result.boss = game.boss;
    result.position = {game.curMapNodeX, game.curMapNodeY};
    if (!game.map) return result;
    result.available = true;
    result.bossPosition = {3, game.act == 4 ? 3 : 15};
    if (game.act != 4 && game.curMapNodeY == 15) {
        // Console input uses x=0 to enter the single Boss, not a map column.
        result.position = result.bossPosition;
    }
    const auto &map = *game.map;
    for (int y = 0; y < 15; ++y) {
        for (int x = 0; x < 7; ++x) {
            const auto &source = map.getNode(x, y);
            bool visible = source.edgeCount > 0 || source.room == Room::BOSS;
            if (y == 14) {
                visible = false;
                for (const auto &parent : map.nodes[13]) {
                    for (int i = 0; i < parent.edgeCount; ++i) {
                        visible |= parent.edges[i] == x;
                    }
                }
            }
            if (!visible) continue;
            Node node{{x, y}, source.room,
                      !game.greenKey && x == map.burningEliteX && y == map.burningEliteY, {}};
            if (y == 14 && game.act != 4) {
                node.next.push_back(result.bossPosition);
            } else {
                for (int i = 0; i < source.edgeCount; ++i) {
                    node.next.push_back({source.edges[i], y + 1});
                }
            }
            if (y == 0) result.starts.push_back({x, y});
            result.nodes.push_back(node);
        }
    }
    if (game.act != 4) {
        result.nodes.push_back({result.bossPosition, Room::BOSS, false, {}});
    }
    return result;
}

} // namespace sts::public_map
#endif
