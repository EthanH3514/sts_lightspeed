#define main forethoughtFoundationMain
#include "forethought_foundation.cpp"
#undef main
#include "sim/PublicCombatResampling.h"
#include <set>

int main() {
    Fixture f;
    int a=f.add(CardId::BLUDGEON), b=f.add(CardId::DEFEND_RED);
    f.play(true); f.sim.takeAction("1 0");
    check(f.b.cards.publicKnownBottomIds==std::vector<int>({a,b}), "public selection records bottom-first identities");
    GameContext game(CharacterClass::IRONCLAD,123456789,0);
    game.floorNum=1; game.relics={}; game.enterBattle(MonsterEncounter::CULTIST);
    const auto original=f.b;
    std::set<std::vector<int>> orders;
    for(unsigned int seed=0;seed<128;++seed) {
        auto sampled=original;
        std::array<std::uint64_t,7> seeds{seed+1,seed+2,seed+3,seed+4,seed+5,seed+6,seed+7};
        public_sampling::resampleCombatContinuation(game,sampled,seeds);
        check(sampled.cards.drawPile[0].uniqueId==a && sampled.cards.drawPile[1].uniqueId==b,
              "resampling preserves selected bottom order");
        check(sampled.cards.drawPile[0].freeToPlayOnce && sampled.cards.drawPile[0].cost==3,
              "resampling preserves free flag and combat cost");
        std::vector<int> ids; for(auto &c:sampled.cards.drawPile) ids.push_back(c.uniqueId);
        orders.insert(ids);
        // Paired hidden orders canonicalize to the same sampled world.
        auto paired=original;
        std::reverse(paired.cards.drawPile.begin()+2,paired.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(game,paired,seeds);
        for(int i=0;i<ids.size();++i) check(ids[i]==paired.cards.drawPile[i].uniqueId,"hidden order is not a sampling input");
        auto both=original;
        int top=both.cards.drawPile.back().uniqueId;
        public_sampling::resampleCombatContinuation(game,both,seeds,top);
        check(both.cards.drawPile.back().uniqueId==top && both.cards.drawPile[0].uniqueId==a,
              "known top and bottom coexist");
    }
    check(orders.size()>1,"unknown middle still varies across sampled worlds");
    auto frozen=original;
    game.relics.add({RelicId::FROZEN_EYE});
    std::array<std::uint64_t,7> frozenSeeds{8,9,10,11,12,13,14};
    public_sampling::resampleCombatContinuation(game,frozen,frozenSeeds);
    for(int i=0;i<frozen.cards.drawPile.size();++i)
        check(frozen.cards.drawPile[i].uniqueId==original.cards.drawPile[i].uniqueId,"Frozen Eye preserves the whole order");
    game.relics={};
    auto draw=original;
    while(draw.cards.drawPile.size()>2) draw.cards.popFromDrawPile();
    check(draw.cards.publicKnownBottomIds.size()==2,"unknown draws preserve known bottom");
    std::array<std::uint64_t,7> seeds{1,2,3,4,5,6,7};
    public_sampling::resampleCombatContinuation(game,draw,seeds,b);
    check(draw.cards.drawPile[1].uniqueId==b,"fully known bottom can overlap known top");
    draw.cards.popFromDrawPile();
    check(draw.cards.publicKnownBottomIds==std::vector<int>({a}),"drawing a known card removes just its identity");
    draw.cards.removeFromDrawPileAtIdx(0);
    check(draw.cards.publicKnownBottomIds.empty(),"retrieval removes known bottom identity");

    auto uncertain=original;
    uncertain.cards.createTempCardInDrawPile(2,CardInstance(CardId::STRIKE_RED));
    bool refused=false;
    try {public_sampling::resampleCombatContinuation(game,uncertain,seeds);} catch(const std::runtime_error &) {refused=true;}
    check(refused && uncertain.cards.publicBottomOrderUncertain,"random insertion refuses rather than silently discarding knowledge");
    uncertain.addToBot(Actions::ShuffleDrawPile());
    uncertain.inputState=InputState::EXECUTING_ACTIONS; uncertain.executeActions();
    check(!uncertain.cards.publicBottomOrderUncertain && uncertain.cards.publicKnownBottomIds.empty(),"public shuffle clears prior order constraints");
    auto malformed=original; malformed.cards.publicKnownBottomIds[0]=-1;
    refused=false;
    try {public_sampling::resampleCombatContinuation(game,malformed,seeds);} catch(const std::runtime_error &) {refused=true;}
    check(refused,"invalid public bottom constraints fail closed");
    std::cout<<"FORETHOUGHT_PUBLIC_CONSTRAINTS "<<failures<<" failures / "<<checks<<" checks\n";
    return failures?1:0;
}
