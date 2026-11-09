#include "game/GameContext.h"
#include "sim/PublicRunSupplement.h"
#include <iostream>
#include <stdexcept>
using namespace sts;
int checks=0;
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
int main(){try{
    for(auto curse:{CardId::DOUBT,CardId::PARASITE}) for(int position:{0,2,4}) {
        GameContext g(CharacterClass::IRONCLAD,7,20);g.deck=Deck{};
        CardId cards[]={CardId::STRIKE_RED,CardId::DEFEND_RED,CardId::INFLAME,CardId::BASH};
        for(int i=0;i<5;++i)g.deck.obtainRaw(Card(i==position?curse:cards[i-(i>position)]));
        for(int i=0;i<g.deck.size();++i) {
            auto type=g.deck.cards[i].getType();
            if(type!=CardType::CURSE && g.deck.bottleIdxs[int(type)]==-1)g.deck.bottleCard(i,type);
        }
        auto original=g.deck;int oldMax=g.maxHp;
        g.deck.remove(g,position);
        check(g.deck.size()==4,"one removal");
        for(int i=0;i<3;++i){int before=original.bottleIdxs[i];int expected=before-(before>position);
            check(g.deck.bottleIdxs[i]==expected,"bottled card shifted with deck");
            check(g.deck.cards[expected].id==original.cards[before].id,"bottled identity preserved");}
        check(g.deck.transformableCount==original.transformableCount-1,"curse accounting preserved");
        check(g.deck.upgradeableCount==original.upgradeableCount,"curse not upgradeable");
        check(g.deck.cardTypeCounts[3]==original.cardTypeCounts[3]-1,"curse count");
        check(g.maxHp==oldMax-(curse==CardId::PARASITE?3:0),"parasite max HP loss exactly once");
        check(spire_run_public::capture(g.deck)["deck"].size()==4,"public supplement remains valid");
    }
    GameContext g(CharacterClass::IRONCLAD,7,0);g.deck=Deck{};
    g.deck.obtainRaw(Card(CardId::DOUBT));g.deck.obtainRaw(Card(CardId::DEFEND_RED));
    g.deck.remove(g,0);check(g.deck.bottleIdxs==std::array<int,3>{-1,-1,-1},"absent bottles unchanged");
    g.deck.bottleCard(0,CardType::SKILL);g.deck.remove(g,0);
    check(g.deck.size()==0 && !g.deck.anyCardBottled() && g.deck.transformableCount==0,"bottled removal lifecycle");
    std::cout<<"deck removal bottle checks: "<<checks<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
