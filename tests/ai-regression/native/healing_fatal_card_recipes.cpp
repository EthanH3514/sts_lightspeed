#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
using namespace sts;
namespace {
void require(bool v,const char *m) { if(!v) throw std::runtime_error(m); }
struct Spec { CardId id; int damage[2], heal[2], cost; bool area,exhaust; };
constexpr Spec specs[]={
    {CardId::BANDAGE_UP,{0,0},{4,6},0,false,true},
    {CardId::BITE,{7,8},{2,3},1,false,false},
    {CardId::REAPER,{4,5},{0,0},2,true,true},
    {CardId::FEED,{10,12},{0,0},1,false,true},
    {CardId::HAND_OF_GREED,{20,25},{0,0},2,false,false},
};
struct Fixture {
    GameContext game; BattleContext b;
    Fixture(CardId id,bool up,RelicId relic=RelicId::INVALID,bool frozen=false)
        :game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::SMALL_SLIMES);
        b.init(game); b.executeActions(); b.cards=CardManager{};
        b.player.maxHp=80; b.player.curHp=50; b.player.energy=5; b.player.gold=100;
        for(int i=0;i<2;++i) { b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=200; b.monsters.arr[i].block=0; }
        CardInstance c(id,up); c.setUniqueId(100); b.cards.hand[b.cards.cardsInHand++]=c;
        int uid=101; for(auto id:{CardId::STRIKE_RED,CardId::BASH,CardId::DEFEND_RED}) {
            CardInstance next(id); next.setUniqueId(uid++); b.cards.drawPile.push_back(next);
        } b.cards.nextUniqueCardId=uid;
    }
};
void play(BattleContext &b) {
    auto c=b.cards.hand[0]; require(c.canUse(b,0,false),"not playable");
    b.addToBotCard(CardQueueItem(c,0,b.player.energy));
    b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();
}
std::vector<int> pile(const BattleContext &b) {
    std::vector<int> ids; for(auto c:b.cards.drawPile) ids.push_back(c.uniqueId); return ids;
}
void baseAndSampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto s:specs) for(int up=0;up<2;++up) for(bool frozen:{false,true}) {
        Fixture f(s.id,up,RelicId::INVALID,frozen); auto twin=f.b;
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        require(pile(f.b)==pile(twin),"hidden order leaked");
        const auto order=pile(f.b); play(f.b); play(twin);
        int heal=s.area?s.damage[up]*2:s.heal[up];
        require(f.b.player.curHp==50+heal && f.b.player.curHp==twin.player.curHp,"healing mismatch");
        require(f.b.monsters.arr[0].curHp==200-s.damage[up],"damage mismatch");
        require(f.b.monsters.arr[1].curHp==200-(s.area?s.damage[up]:0),"area scope mismatch");
        require(f.b.player.energy==5-s.cost && f.b.player.maxHp==80 && f.b.player.gold==100,"cost/nonfatal reward mismatch");
        const auto &dest=s.exhaust?f.b.cards.exhaustPile:f.b.cards.discardPile;
        require(dest.size()==1 && dest[0].uniqueId==100,"lifecycle mismatch");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        require(f.b.cards.cardsInHand==0,"hand mutated");
        if(frozen) require(pile(f.b)==order,"Frozen Eye changed");
    }
}
void healing() {
    for(int up=0;up<2;++up) for(auto id:{CardId::BANDAGE_UP,CardId::BITE,CardId::REAPER})
    for(auto relic:{RelicId::INVALID,RelicId::MAGIC_FLOWER,RelicId::MARK_OF_THE_BLOOM}) {
        Fixture f(id,up,relic); f.b.monsters.arr[0].block=999;
        f.b.monsters.arr[1].curHp=2;
        const int raw=id==CardId::REAPER?2:id==CardId::BITE?(up?3:2):(up?6:4);
        const int heal=relic==RelicId::MARK_OF_THE_BLOOM?0:relic==RelicId::MAGIC_FLOWER?(raw*3+1)/2:raw;
        play(f.b); require(f.b.player.curHp==50+heal,"block/overkill/healing relic mismatch");
        Fixture cap(id,up,relic); cap.b.player.curHp=79; play(cap.b);
        require(cap.b.player.curHp==(relic==RelicId::MARK_OF_THE_BLOOM?79:80),"heal cap mismatch");
    }
    for(bool up:{false,true}) for(auto id:{CardId::BITE,CardId::REAPER}) {
        Fixture kill(id,up); kill.b.monsters.arr[0].curHp=1;
        kill.b.monsters.arr[1].curHp=0; kill.b.monsters.monstersAlive=1;
        play(kill.b);
        require(kill.b.outcome==Outcome::PLAYER_VICTORY,"healing attack did not terminate");
        require(kill.b.player.curHp==50+(id==CardId::REAPER?1:(up?3:2)),"victory lost heal");
    }
}
void fatalRewards() {
    for(bool up:{false,true}) for(auto id:{CardId::FEED,CardId::HAND_OF_GREED})
    for(bool minion:{false,true}) for(bool blocked:{false,true}) {
        Fixture f(id,up); f.b.monsters.arr[0].curHp=1;
        if(minion) f.b.monsters.arr[0].buff<MS::MINION>();
        if(blocked) f.b.monsters.arr[0].block=999;
        play(f.b);
        bool reward=!minion&&!blocked;
        require(f.b.player.maxHp==80+(id==CardId::FEED&&reward?(up?4:3):0),"Feed eligibility mismatch");
        require(f.b.player.gold==100+(id==CardId::HAND_OF_GREED&&reward?(up?25:20):0),"Greed eligibility mismatch");
    }
    for(bool up:{false,true}) {
        for(auto id:{CardId::FEED,CardId::HAND_OF_GREED}) {
            Fixture last(id,up); last.b.monsters.arr[0].curHp=1;
            last.b.monsters.arr[1].curHp=0; last.b.monsters.monstersAlive=1;
            play(last.b); require(last.b.outcome==Outcome::PLAYER_VICTORY,"fatal reward did not end combat");
            require(last.b.player.maxHp==80+(id==CardId::FEED?(up?4:3):0),"terminal Feed reward lost");
            require(last.b.player.gold==100+(id==CardId::HAND_OF_GREED?(up?25:20):0),"terminal Greed reward lost");
        }
        Fixture bloom(CardId::FEED,up,RelicId::MARK_OF_THE_BLOOM); bloom.b.monsters.arr[0].curHp=1;
        play(bloom.b); require(bloom.b.player.maxHp==80+(up?4:3)&&bloom.b.player.curHp==50,"Bloom must not block max HP gain");
        Fixture ecto(CardId::HAND_OF_GREED,up,RelicId::ECTOPLASM); ecto.b.monsters.arr[0].curHp=1;
        play(ecto.b); require(ecto.b.player.gold==100,"Ectoplasm ignored");
        Fixture thorn(CardId::HAND_OF_GREED,up); thorn.b.monsters.arr[0].buff<MS::THORNS>(3);
        play(thorn.b); require(thorn.b.player.curHp==47,"Hand of Greed missed attack-triggered Thorns");
        Fixture angry(CardId::HAND_OF_GREED,up); angry.b.monsters.arr[0].buff<MS::ANGRY>(2);
        play(angry.b); require(angry.b.monsters.arr[0].getStatus<MS::STRENGTH>()==2,"Hand of Greed missed Angry");
    }
}
}
int main() {
    try {baseAndSampling(); healing(); fatalRewards();
        std::cout<<"HEALING_FATAL_CARD_RECIPES_OK (5 cards, 10 variants)\n";
    } catch(const std::exception &e) {std::cerr<<"HEALING_FATAL_CARD_RECIPES_FAILED: "<<e.what()<<'\n';return 1;}
}
