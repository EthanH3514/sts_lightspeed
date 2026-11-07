#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(bool frozen=false, RelicId relic=RelicId::INVALID)
        : game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game); b.executeActions(); b.cards=CardManager{};
        b.cards.nextUniqueCardId=100; b.player.energy=20;
        b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=200;
        for(auto id:{CardId::STRIKE_RED,CardId::DEFEND_RED,CardId::BASH}) {
            CardInstance c(id); c.setUniqueId(b.cards.nextUniqueCardId++);
            b.cards.drawPile.push_back(c);
        }
    }
    void play(CardId id,bool up=false) {
        CardInstance c(id,up); c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.hand[b.cards.cardsInHand++]=c;
        require(c.canUse(b,0,false),"fixture card not playable");
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); execute();
    }
    void execute() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); }
};
void variantsAndSampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::BARRICADE,CardId::ENTRENCH,CardId::METALLICIZE,CardId::FLAME_BARRIER})
    for(bool up:{false,true}) for(bool frozen:{false,true}) {
        Fixture f(frozen); f.b.player.block=10;
        auto twin=f.b;
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<3;++i) require(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden order leak");
        auto before=f.b.cards.drawPile;
        f.play(id,up);
        const int cost=id==CardId::BARRICADE?(up?2:3):id==CardId::ENTRENCH?(up?1:2):id==CardId::METALLICIZE?1:2;
        require(f.b.player.energy==20-cost,"cost mismatch");
        const bool power=id==CardId::BARRICADE||id==CardId::METALLICIZE;
        require(f.b.cards.cardsInHand==0&&f.b.cards.exhaustPile.empty()&&f.b.cards.discardPile.size()==(power?0:1),"card lifecycle mismatch");
        int expected=id==CardId::ENTRENCH?20:id==CardId::FLAME_BARRIER?10+(up?16:12):10;
        require(f.b.player.block==expected,"immediate block mismatch");
        if(id==CardId::BARRICADE) require(f.b.player.hasStatus<PS::BARRICADE>(),"retention absent");
        if(id==CardId::METALLICIZE) require(f.b.player.getStatus<PS::METALLICIZE>()==(up?4:3),"end-turn power amount");
        if(id==CardId::FLAME_BARRIER) require(f.b.player.getStatus<PS::FLAME_BARRIER>()==(up?6:4),"retaliation amount");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        require(f.b.player.block==expected,"resampling changed block");
        if(frozen) for(int i=0;i<3;++i) require(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"Frozen Eye changed");
    }
}
void blockInteractions() {
    for(bool up:{false,true}) for(bool noBlock:{false,true}) {
        Fixture f; f.b.player.block=17; f.b.player.dexterity=7;
        f.b.player.debuff<PS::FRAIL>(2,false);
        if(noBlock) f.b.player.debuff<PS::NO_BLOCK>(2,false);
        f.play(CardId::ENTRENCH,up);
        require(f.b.player.block==34,"Entrench reapplied card modifiers");
        f.play(CardId::BODY_SLAM,true);
        require(f.b.monsters.arr[0].curHp==166,"Body Slam missed doubled block");
        Fixture flame; flame.b.player.dexterity=7; flame.b.player.debuff<PS::FRAIL>(2,false);
        if(noBlock) flame.b.player.debuff<PS::NO_BLOCK>(2,false);
        flame.play(CardId::FLAME_BARRIER,up);
        require(flame.b.player.block==(noBlock?0:(up?23:19)*3/4),"Flame Barrier card modifiers");
        require(flame.b.player.getStatus<PS::FLAME_BARRIER>()==(up?6:4),"No Block suppressed retaliation");
        Fixture metal; metal.b.player.dexterity=7; metal.b.player.debuff<PS::FRAIL>(2,false);
        if(noBlock) metal.b.player.debuff<PS::NO_BLOCK>(2,false);
        metal.play(CardId::BARRICADE); metal.play(CardId::METALLICIZE,up);
        metal.play(CardId::METALLICIZE,up); metal.end();
        require(metal.b.player.block==2*(up?4:3),"Metallicize timing/stack/modifiers");
    }
    for(bool retain:{false,true}) for(auto relic:{RelicId::INVALID,RelicId::CALIPERS}) {
        Fixture f(false,relic); f.b.player.block=40;
        if(retain) { f.play(CardId::BARRICADE); f.play(CardId::BARRICADE,true); }
        f.end();
        require(f.b.player.block==(retain?40:relic==RelicId::CALIPERS?25:0),"retention/Calipers precedence");
    }
    Fixture timing; timing.play(CardId::METALLICIZE);
    CardInstance burn(CardId::BURN); burn.setUniqueId(timing.b.cards.nextUniqueCardId++);
    timing.b.cards.hand[timing.b.cards.cardsInHand++]=burn; timing.end();
    require(timing.b.player.curHp==70&&timing.b.player.block==0,"Metallicize must precede Burn and expire normally");
    for(int block:{0,499,500,998,999}) {
        Fixture cap; cap.b.player.block=block; cap.b.player.buff<PS::JUGGERNAUT>(5);
        cap.play(CardId::ENTRENCH);
        require(cap.b.player.block==std::min(999,2*block),"player block exceeded 999");
        require(cap.b.monsters.arr[0].curHp==200-(block>0?5:0),"Juggernaut trigger at zero/cap");
    }
    Fixture cardCap; cardCap.b.player.block=995; cardCap.play(CardId::FLAME_BARRIER);
    require(cardCap.b.player.block==999,"card block cap");
    Fixture turnCap; turnCap.b.player.block=998; turnCap.play(CardId::BARRICADE);
    turnCap.play(CardId::METALLICIZE); turnCap.end();
    require(turnCap.b.player.block==999,"end-turn block cap");
}
void retaliation() {
    for(bool up:{false,true}) for(int damage:{0,2,20}) {
        Fixture f; f.play(CardId::FLAME_BARRIER,up); f.play(CardId::FLAME_BARRIER,up);
        f.b.player.block=100;
        for(int i=0;i<3;++i) { f.b.player.attacked(f.b,0,damage); f.execute(); }
        require(f.b.monsters.arr[0].curHp==200-6*(up?6:4),"blocked/zero/multi-hit retaliation or stacking");
        require(f.b.player.curHp==70,"blocked attack lost HP");
        auto hp=f.b.monsters.arr[0].curHp;
        f.b.addToBot(Actions::PlayerLoseHp(1,true)); f.execute();
        require(f.b.monsters.arr[0].curHp==hp,"self HP loss retaliated");
        f.end(); require(!f.b.player.hasStatus<PS::FLAME_BARRIER>(),"retaliation did not expire");
        f.b.player.attacked(f.b,0,0); f.execute();
        require(f.b.monsters.arr[0].curHp==hp,"expired retaliation triggered");
    }
}
}
int main() {
    try {variantsAndSampling(); blockInteractions(); retaliation();
        std::cout<<"PERSISTENT_DEFENSE_CARD_RECIPES_OK (4 cards, 8 variants)\n";
    } catch(const std::exception &e) {std::cerr<<"PERSISTENT_DEFENSE_CARD_RECIPES_FAILED: "<<e.what()<<'\n'; return 1;}
}
