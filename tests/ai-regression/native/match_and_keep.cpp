#include "game/GameContext.h"
#include "sim/ConsoleSimulator.h"
#include "sim/PublicMatchAndKeep.h"
#include "sim/PrintHelpers.h"
#include "sim/search/GameAction.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <map>
using namespace sts;
using J = nlohmann::json;
int checks = 0;
void check(bool ok, const char* what) { ++checks; if (!ok) throw std::runtime_error(what); }
GameContext root(int asc = 20, int seed = 1234) {
    GameContext g(CharacterClass::IRONCLAD, seed, asc);
    g.curRoom = Room::EVENT; g.curEvent = Event::MATCH_AND_KEEP; g.floorNum = 8;
    g.regainControlAction = [](GameContext& x) { x.screenState=ScreenState::MAP_SCREEN; };
    g.setupEvent(); return g;
}
std::string state(const GameContext& g) { std::ostringstream s; s << g; return s.str(); }
std::string menu(GameContext& g) { ConsoleSimulator c; c.gc = &g; std::ostringstream s; c.printActions(s); return s.str(); }
void pair(GameContext& g, int i, int j, bool console) {
    if (console) { ConsoleSimulator c; c.gc = &g; c.takeEventAction(std::to_string(i) + " " + std::to_string(j)); }
    else search::GameAction(i, j).execute(g);
}
std::pair<int,int> find(const GameContext& g, bool same) {
    for (int i = 0; i < 12; ++i) for (int j = i + 1; j < 12; ++j)
        if (g.canChooseMatchAndKeepCards(i,j) && (g.info.toSelectCards[i].card.id == g.info.toSelectCards[j].card.id) == same)
            return {i,j};
    throw std::runtime_error("fixture lacks required pair");
}
void fixed(GameContext& g, Card card) {
    g.info.toSelectCards[0] = {card,-1}; g.info.toSelectCards[1] = {card,-1};
}
int main() { try {
    check(!GameContext::disableMatchAndKeep, "event enabled");
    for (int asc : {0,10,20}) for (int seed : {7,1234,2047}) {
        auto g = root(asc,seed); auto before = state(g);
        auto p = spire_match_public::capture(g);
        check(g.info.eventData == 5 && g.info.toSelectCards.size() == 12, "five attempts/twelve tiles");
        check(p["available"] == true && p["tiles"].size() == 12, "public board");
        std::map<CardId,int> counts;
        for (const auto& c : g.info.toSelectCards) ++counts[c.card.id];
        for (const auto& c : counts) check(c.second % 2 == 0, "paired generation including duplicate curses");
        for (const auto& t : p["tiles"]) check(t["known"] == false && !t.contains("card"), "unknown identities absent");
        auto altered = g;
        for (auto& t : altered.info.toSelectCards) t.card = Card(CardId::WHIRLWIND);
        check(spire_match_public::capture(altered) == p && menu(g) == menu(altered), "hidden board twins expose identical public state");
        check(search::GameAction::getAllActionsInState(g).size() == 66, "all unordered legal pairs");
        check(!search::GameAction(0,0).isValidAction(g) && !search::GameAction(0,12).isValidAction(g), "invalid pair checks");
        for (auto text : {"0", "0 0", "-1 2", "0 12", "0 1 junk", "none"}) {
            bool rejected = false;
            try { ConsoleSimulator c; c.gc=&g; c.takeEventAction(text); }
            catch (const std::exception&) { rejected=true; }
            check(rejected && state(g) == before, "invalid console action rejects without mutation");
        }
        auto a = find(g,false); auto left=g, right=g;
        pair(left,a.first,a.second,true); pair(right,a.first,a.second,false);
        check(state(left) == state(right), "console/GameAction equality");
        check(left.info.eventData == 4 && left.deck.size() == g.deck.size(), "mismatch costs one attempt only");
        auto visible=spire_match_public::capture(left);
        check(visible["tiles"][a.first]["known"] == true && visible["tiles"][a.second]["known"] == true, "mismatch reveals both");
        auto l=left.miscRng,r=g.miscRng,lc=left.cardRng,rc=g.cardRng;
        check(l.counter==r.counter && lc.counter==rc.counter && l.randomLong()==r.randomLong() && lc.randomLong()==rc.randomLong(), "flips do not consume RNG");
        check(state(g) == before, "branch independence");
        auto b=find(g,true); pair(g,b.first,b.second,true);
        check(g.info.eventData==4 && g.deck.size()==left.deck.size()+1, "match obtains one card and costs one attempt");
        check(!g.canChooseMatchAndKeepCards(b.first,b.second), "removed tiles unselectable");
        check(spire_match_public::capture(g)["tiles"][b.first]["card"].contains("id"), "matched tile memory retained");
        check(search::GameAction::getAllActionsInState(g).size()==45, "ten remaining tiles");
        a=find(g,false);
        for(int n=0;n<4;++n) pair(g,a.first,a.second,false);
        check(g.screenState==ScreenState::MAP_SCREEN && g.info.eventData==0 && g.info.toSelectCards.empty(), "exactly fifth attempt exits");
        check(spire_match_public::capture(g)==J{{"version",spire_match_public::version},{"available",false}}, "exit clears public board");
    }
    for(bool console:{false,true}) {
        auto g=root(); g.obtainRelic(RelicId::MOLTEN_EGG);
        Card preview(CardId::SEARING_BLOW); preview.upgrade(); fixed(g,preview);
        pair(g,0,1,console);
        check(g.deck.cards[g.deck.size()-1].getUpgraded()==1, "Egg preview does not double upgrade Searing Blow");
        g=root(); g.obtainRelic(RelicId::OMAMORI); fixed(g,Card(CardId::REGRET));
        const int n=g.deck.size(), charges=g.relics.getRelicValueRef(RelicId::OMAMORI);
        pair(g,0,1,console);
        check(g.deck.size()==n && g.relics.getRelicValueRef(RelicId::OMAMORI)==charges-1, "matched curse obeys Omamori");
        check(!g.canChooseMatchAndKeepCards(0,1), "prevented curse still removes pair");
        g=root(); g.obtainRelic(RelicId::CERAMIC_FISH); fixed(g,Card(CardId::BASH));
        const int gold=g.gold; pair(g,0,1,console); check(g.gold==gold+9, "obtain callback Ceramic Fish");
    }
    std::cout << J{{"checks",checks},{"failures",J::array()}}.dump() << '\n'; return 0;
} catch(const std::exception& e) { std::cout << J{{"checks",checks},{"error",e.what()}}.dump() << '\n'; return 1; } }
