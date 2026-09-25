// Standalone regression for Forethought selection and existing free-play semantics.
#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/BattleSimulator.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) { ++checks; if(!ok && ++failures<=16) std::cerr<<message<<'\n'; }
struct Fixture {
    BattleSimulator sim;
    BattleContext &b;
    Fixture():b(*sim.bc) {
        GameContext game(CharacterClass::IRONCLAD,123456789,0);
        game.floorNum=1; game.relics={}; game.enterBattle(MonsterEncounter::CULTIST);
        sim.initBattle(game); b.executeActions(); b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=10; b.player.curHp=b.player.maxHp=100;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(int i=0;i<3;++i) b.cards.createTempCardInDrawPile(i,CardInstance(CardId::STRIKE_RED));
    }
    int add(CardId id,int cost=99,int turnCost=99) {
        b.cards.createTempCardInHand(CardInstance(id));
        auto &c=b.cards.hand[b.cards.cardsInHand-1];
        if(cost!=99) c.cost=cost;
        if(turnCost!=99) c.costForTurn=turnCost;
        return c.uniqueId;
    }
    void play(bool upgraded) {
        b.cards.createTempCardInHand(CardInstance(CardId::FORETHOUGHT,upgraded));
        auto c=b.cards.hand[b.cards.cardsInHand-1];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy));
        drain();
    }
    void drain() {b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();}
};
void baseAndEmpty() {
    for(bool upgraded:{false,true}) {
        Fixture empty; empty.play(upgraded);
        check(empty.b.inputState==InputState::PLAYER_NORMAL,"empty hand should resolve without choice");
        check(empty.b.cards.discardPile.size()==1 && empty.b.cards.exhaustPile.empty(),"Forethought discards, not exhausts");
    }
    Fixture single; int id=single.add(CardId::BLUDGEON); single.play(false);
    check(single.b.inputState==InputState::PLAYER_NORMAL,"base one-card hand auto-selects");
    check(single.b.cards.drawPile[0].uniqueId==id && single.b.cards.drawPile[0].freeToPlayOnce,"base auto-selection moves/free flag");
    Fixture many; many.add(CardId::DEFEND_RED); int chosen=many.add(CardId::BLUDGEON); many.play(false);
    check(many.b.inputState==InputState::CARD_SELECT && !many.b.cardSelectInfo.canPickAnyNumber,"base requires one selection");
    many.sim.takeAction("1");
    check(many.b.cards.drawPile[0].uniqueId==chosen && many.b.cards.cardsInHand==1,"base manual choice");
}
void upgradedChoices() {
    for(int size:{1,2,4,9}) for(int mask=0;mask<(1<<size);++mask) {
        Fixture f; std::vector<int> ids;
        for(int i=0;i<size;++i) ids.push_back(f.add(i%2?CardId::BLUDGEON:CardId::DEFEND_RED));
        auto previous=f.b.cards.drawPile; f.play(true);
        bool ready=f.b.inputState==InputState::CARD_SELECT && f.b.cardSelectInfo.canPickAnyNumber;
        check(ready,"upgraded Forethought requires optional multi-choice, including one-card hand");
        if(!ready) continue; // Safe pre-fix reproducer, no invalid console command.
        std::ostringstream options; f.sim.printCardSelectActions(options);
        check(options.str().find("none")!=std::string::npos,"console documents zero choice");
        std::vector<int> selected; std::ostringstream command;
        for(int i=0;i<size;++i) if(mask&(1<<i)) selected.push_back(i);
        for(int i:selected) command<<i<<' ';
        f.sim.takeAction(selected.empty()?"none":command.str());
        check(f.b.inputState==InputState::PLAYER_NORMAL,"selection resumes pending queue");
        check(f.b.cards.cardsInHand==size-int(selected.size()),"only selected cards leave hand");
        check(f.b.cards.drawPile.size()==previous.size()+selected.size(),"pile sizes conserved");
        for(std::size_t i=0;i<selected.size();++i) {
            auto &c=f.b.cards.drawPile[i];
            check(c.uniqueId==ids[selected[selected.size()-1-i]],"selected order determines bottom insertion order");
            check(c.freeToPlayOnce,"positive-cost selection gets one free use");
        }
        for(std::size_t i=0;i<previous.size();++i)
            check(f.b.cards.drawPile[selected.size()+i].uniqueId==previous[i].uniqueId,"existing draw order preserved");
        std::map<int,int> counts;
        for(int i=0;i<f.b.cards.cardsInHand;++i) ++counts[f.b.cards.hand[i].uniqueId];
        for(auto &c:f.b.cards.drawPile) ++counts[c.uniqueId];
        for(int id:ids) check(counts[id]==1,"card identity neither lost nor duplicated");
        check(f.b.player.energy==10 && f.b.cards.discardPile.size()==1,"zero cost and normal source-card discard");
    }
    // Distinct orders of the same subset are not interchangeable.
    std::vector<int> order{0,1,2};
    do {
        Fixture f; std::vector<int> ids;
        for(int i=0;i<3;++i) ids.push_back(f.add(CardId::BLUDGEON));
        f.play(true);
        if(!f.b.cardSelectInfo.canPickAnyNumber) continue;
        std::ostringstream command; for(int i:order) command<<i<<' ';
        f.sim.takeAction(command.str());
        for(int i=0;i<3;++i) check(f.b.cards.drawPile[i].uniqueId==ids[order[2-i]],"console preserves arbitrary selection order");
    } while(std::next_permutation(order.begin(),order.end()));
    Fixture full;
    for(int i=0;i<10;++i) full.add(CardId::BLUDGEON);
    full.b.addToBot(Actions::ForethoughtAction(true)); full.drain();
    if(full.b.inputState==InputState::CARD_SELECT && full.b.cardSelectInfo.canPickAnyNumber) {
        // Autoplay can resolve the effect while all ten hand slots are occupied.
        full.sim.takeAction("9 8 7 6 5 4 3 2 1 0");
        check(full.b.cards.cardsInHand==0 && full.b.cards.drawPile.size()==13,"full hand can select all ten");
    } else check(false,"full-hand upgraded action must allow multi-choice");
    Fixture invalid; invalid.add(CardId::BLUDGEON); invalid.add(CardId::DEFEND_RED); invalid.play(true);
    if(invalid.b.cardSelectInfo.canPickAnyNumber) {
        bool rejected=false;
        try {invalid.sim.takeAction("0 0");} catch(const std::invalid_argument &) {rejected=true;}
        check(rejected && invalid.b.cards.cardsInHand==2 && invalid.b.cards.drawPile.size()==3,
              "duplicate indices are rejected before moving any card");
    }
}
void freeFlag() {
    for(int cost:{-2,-1,0,1,3}) for(int turnCost:{-2,-1,0,1,3}) {
        Fixture f; f.add(CardId::BLUDGEON,cost,turnCost); f.play(false);
        const auto &c=f.b.cards.drawPile[0];
        check(c.cost==cost && c.costForTurn==turnCost,"Forethought does not overwrite either cost field");
        check(c.freeToPlayOnce==(cost>0),"eligibility uses combat cost, not temporary cost");
        f.b.cards.resetAttributesAtEndOfTurn();
        check(f.b.cards.drawPile[0].freeToPlayOnce==(cost>0),"normal turn reset preserves one-use flag");
    }
    Fixture f; int id=f.add(CardId::BLUDGEON); f.play(false);
    f.b.cards.draw(f.b,4); f.drain();
    int index=-1; for(int i=0;i<f.b.cards.cardsInHand;++i) if(f.b.cards.hand[i].uniqueId==id) index=i;
    check(index>=0 && f.b.cards.hand[index].freeToPlayOnce,"flag survives drawing");
    f.b.player.energy=0;
    check(f.b.cards.hand[index].canUse(f.b,0,false),"flag makes positive-cost card playable at zero energy");
    f.b.addToBotCard(CardQueueItem(f.b.cards.hand[index],0,0)); f.drain();
    check(f.b.player.energy==0 && f.b.monsters.arr[0].curHp==968,"actual free use has normal damage and no energy charge");
    bool found=false; for(auto &c:f.b.cards.discardPile) if(c.uniqueId==id) {
        found=true; check(!c.freeToPlayOnce && c.cost==3 && c.costForTurn==3,"free flag consumed after use; printed cost unchanged");
    }
    check(found,"used card retains identity in discard");
    Fixture x; x.add(CardId::WHIRLWIND); x.play(false);
    check(x.b.cards.drawPile[0].cost==-1 && !x.b.cards.drawPile[0].freeToPlayOnce,"X-cost is not made free");
    x.b.cards.draw(x.b,4); x.drain(); x.b.player.energy=3;
    x.b.addToBotCard(CardQueueItem(x.b.cards.hand[3],0,3)); x.drain();
    check(x.b.player.energy==0 && x.b.monsters.arr[0].curHp==985,"redrawn X-cost still spends energy for normal hits");
    Fixture existing; existing.add(CardId::GOOD_INSTINCTS);
    existing.b.cards.hand[0].freeToPlayOnce=true; existing.play(false);
    check(existing.b.cards.drawPile[0].freeToPlayOnce,"nonpositive cost does not clear an existing free flag");
}
}
int main() {baseAndEmpty(); upgradedChoices(); freeFlag(); std::cout<<"FORETHOUGHT_FOUNDATION "<<failures<<" failures / "<<checks<<" checks\n"; return failures?1:0;}
