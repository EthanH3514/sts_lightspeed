#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "combat/CardManager.h"

// Test-only reachability watch. Compare both rules on PRE-Corruption values so
// the local production fix cannot hide a naturally reached difference. This is
// independent of the native implementation; never use it as a policy feature.
namespace corruption_cost_watch {
inline std::vector<std::string> differences(const sts::CardManager &cards) {
    std::vector<std::string> result;
    const auto inspect = [&](const sts::CardInstance &c, const char *pile) {
        if (c.getType() != sts::CardType::SKILL) return;
        const int base=c.cost, turn=c.costForTurn;
        const std::pair<int,int> legacy = base>0 ? std::make_pair(0,0) : std::make_pair(base,turn);
        auto original=std::make_pair(base,turn);
        if(turn>0) original={std::max(0,turn-9),std::max(0,turn-9)};
        else if(base>=0) original={std::max(0,base-9),0};
        if(legacy==original) return;
        std::ostringstream out;
        out<<"CORRUPTION_COST_BOUNDARY pile="<<pile<<" card="<<c.getName()
           <<" uid="<<c.uniqueId<<" before="<<base<<'/'<<turn
           <<" legacy="<<legacy.first<<'/'<<legacy.second
           <<" original="<<original.first<<'/'<<original.second;
        result.push_back(out.str());
    };
    for(int i=0;i<cards.cardsInHand;++i) inspect(cards.hand[i],"hand");
    for(const auto &c:cards.drawPile) inspect(c,"draw");
    for(const auto &c:cards.discardPile) inspect(c,"discard");
    for(const auto &c:cards.exhaustPile) inspect(c,"exhaust");
    return result;
}
}
