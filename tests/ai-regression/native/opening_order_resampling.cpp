#include <algorithm>
#include <iostream>
#include <set>
#include <tuple>
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *why){++checks;if(!ok){++failures;if(failures<20)std::cerr<<why<<'\n';}}
auto words(const BattleContext &b){std::vector<int> out;for(const auto &c:b.cards.drawPile)out.push_back(c.uniqueId);return out;}
std::array<std::uint64_t,7> seeds(unsigned i){return {i+1u,2,3,4,5,6,Random::murmurHash3(i+17u)};}
struct Fixture {
    GameContext game;BattleContext b;
    Fixture(int innate,int bottled,bool frozen=false,bool bag=false):game(CharacterClass::IRONCLAD,147,0) {
        game.relics={};game.deck=Deck{};
        if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        if(bag)game.relics.add({RelicId::BAG_OF_PREPARATION});
        for(int i=0;i<12;++i)game.deck.obtainRaw(Card(CardId::DEFEND_RED));
        for(int i=0;i<innate;++i)game.deck.obtainRaw(Card(i%2?CardId::MIND_BLAST:CardId::DRAMATIC_ENTRANCE));
        for(int i=0;i<bottled;++i)game.deck.bottleIdxs[i]=i;
        game.enterBattle(MonsterEncounter::CULTIST);b.init(game);b.executeActions();
        b.player.curHp=b.player.maxHp=999;b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=999;
    }
    void run(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void sample(unsigned i=1){public_sampling::resampleCombatContinuation(game,b,seeds(i));}
};
template<class F>bool rejected(F call){try{call();return false;}catch(const std::runtime_error&){return true;}}
void openingMatrix() {
    for(int n:{0,1,3,8,10,12,15})for(int bottle:{0,1,3})for(bool frozen:{false,true})for(bool bag:{false,true}) {
        Fixture f(n,bottle,frozen,bag);const auto original=words(f.b);
        const int pending=std::max(0,n+bottle-10);
        check(int(f.b.cards.publicOpeningPriorityIds.size())==pending,"actual opening membership after hand cap");
        check(std::is_sorted(f.b.cards.publicOpeningPriorityIds.begin(),f.b.cards.publicOpeningPriorityIds.end()),"membership canonical not private order");
        for(unsigned i=0;i<8;++i) {
            auto one=f.b,two=f.b;
            auto split=two.cards.drawPile.end()-pending;
            if(!frozen){std::reverse(two.cards.drawPile.begin(),split);std::reverse(split,two.cards.drawPile.end());}
            two.shuffleRng=Random(984);two.cardRandomRng=Random(571);
            public_sampling::resampleCombatContinuation(f.game,one,seeds(i));
            public_sampling::resampleCombatContinuation(f.game,two,seeds(i));
            check(words(one)==words(two),"same public world independent of original hidden order/RNG");
            check(words(f.b)==original,"sampling branches do not mutate source");
            check(one.cards.cardsInHand==f.b.cards.cardsInHand&&one.player.energy==f.b.player.energy,"current hand/energy preserved");
            if(frozen)check(words(one)==original,"Frozen Eye preserves full public order");
            for(int j=0;j<pending;++j)check(std::binary_search(one.cards.publicOpeningPriorityIds.begin(),
                one.cards.publicOpeningPriorityIds.end(),int(one.cards.drawPile[one.cards.drawPile.size()-1-j].uniqueId)),"priority suffix preserved");
        }
    }
}
void lifecycle() {
    Fixture f(12,1);check(f.b.cards.publicOpeningPriorityIds.size()==3,"three undrawn original priority cards");
    f.b.cards.createTempCardOnDrawTop(CardInstance(CardId::STRIKE_RED));
    const int top=f.b.cards.drawPile.back().uniqueId;
    f.b.chooseForethoughtCard(0);
    const int bottom=f.b.cards.drawPile.front().uniqueId;
    check(!f.b.cards.publicOpeningOrderUncertain,"selected bottom does not scramble opening band");
    f.sample();check(f.b.cards.drawPile.front().uniqueId==bottom&&f.b.cards.drawPile.back().uniqueId==top,"known edges around opening band preserved");
    f.b.cards.popFromDrawPile();
    const auto removed=f.b.cards.popFromDrawPile();
    check(f.b.cards.publicOpeningPriorityIds.size()==2,"draw or autoplay removes membership once");
    f.b.cards.moveToDrawPileTop(removed);
    check(f.b.cards.publicOpeningPriorityIds.size()==2,"return to top does not regain opening status");
    f.sample();
    f.b.cards.createTempCardInDrawPile(1,CardInstance(CardId::WOUND));
    check(f.b.cards.publicOpeningOrderUncertain,"random insertion is not a shuffle");
    check(rejected([&]{f.sample();}),"unrepresented interleaving still refuses");
    f.b.addToBot(Actions::ShuffleDrawPile());f.run();
    check(f.b.cards.publicOpeningPriorityIds.empty()&&!f.b.cards.publicOpeningOrderUncertain,"real shuffle expires opening constraints");
    f.sample();

    Fixture cleared(12,0);
    while(!cleared.b.cards.publicOpeningPriorityIds.empty())cleared.b.cards.popFromDrawPile();
    cleared.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::MIND_BLAST));
    check(cleared.b.cards.publicOpeningPriorityIds.empty(),"generated innate card never receives opening priority");cleared.sample();
    Fixture random(12,0);random.b.cards.createTempCardInDrawPile(0,CardInstance(CardId::WOUND));
    while(!random.b.cards.publicOpeningPriorityIds.empty())random.b.cards.popFromDrawPile();
    check(!random.b.cards.publicOpeningOrderUncertain,"empty opening group has no remaining constraint");random.sample();

    Fixture cycled(0,3);std::set<int> seen;bool returned=false;
    for(int turn=0;turn<8&&!returned;++turn) {
        for(int i=0;i<cycled.b.cards.cardsInHand;++i)if(cycled.b.cards.hand[i].uniqueId<3)seen.insert(cycled.b.cards.hand[i].uniqueId);
        for(const auto &c:cycled.b.cards.drawPile)if(seen.count(c.uniqueId)) {
            returned=true;check(cycled.b.cards.publicOpeningPriorityIds.empty(),"previously drawn bottled card is ordinary after reshuffle");
        }
        if(returned)cycled.sample();
        else {cycled.b.endTurn();cycled.run();}
    }
    check(seen.size()==3&&returned,"natural end-turn/empty-deck shuffle reproduces old bottled refusal");
}
void validationAndDiversity() {
    Fixture f(12,0);
    auto valid=f.b;
    f.b.cards.publicOpeningPriorityIds.push_back(f.b.cards.publicOpeningPriorityIds.back());
    check(rejected([&]{f.sample();}),"duplicate membership is invalid");f.b=valid;
    f.b.cards.publicOpeningPriorityIds.push_back(30000);
    check(rejected([&]{f.sample();}),"absent membership is invalid");f.b=valid;
    std::swap(f.b.cards.drawPile.front(),f.b.cards.drawPile.back());
    check(rejected([&]{f.sample();}),"inconsistent group cannot be silently shuffled into validity");f.b=valid;
    // Keep two ordinary cards and two priority cards to enumerate joint outcomes.
    while(f.b.cards.drawPile.size()>4)f.b.cards.removeFromDrawPileAtIdx(0);
    const auto base=f.b;std::set<std::vector<int>> permutations;
    for(unsigned seed=0;seed<128;++seed){f.b=base;f.sample(seed);permutations.insert(words(f.b));}
    check(permutations.size()==4,"two bands must not share identical reset RNG permutations");
    f.b=valid;f.b.addToBot(Actions::GainEnergy(1));
    check(rejected([&]{f.sample();}),"unresolved queue remains refused");
}
}
int main(){try{openingMatrix();lifecycle();validationAndDiversity();}catch(const std::exception &e){check(false,e.what());}
    std::cout<<"OPENING_ORDER_RESAMPLING_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
