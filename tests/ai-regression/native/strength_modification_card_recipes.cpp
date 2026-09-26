#include <algorithm>
#include <array>
#include <iostream>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *message) {
    ++checks; if(!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game; BattleContext b;
    explicit Fixture(bool frozen=false) : game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100;
        b.player.energy=20; b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        b.monsters.arr[0].setMove(MMID::CULTIST_DARK_STRIKE);
        for(int i=0;i<10;++i) b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
    }
    void drain() {b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();}
    void play(CardId id,bool up=false) {
        b.cards.createTempCardInHand(CardInstance(id,up)); auto c=b.cards.hand[b.cards.cardsInHand-1];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); drain();
    }
    void end() {search::Action(search::ActionType::END_TURN).execute(b);}
};
void variants() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::DISARM,CardId::LIMIT_BREAK,CardId::DARK_SHACKLES})
    for(bool up:{false,true}) for(bool frozen:{false,true}) {
        Fixture f(frozen); f.b.player.strength=4; f.b.monsters.arr[0].strength=3;
        auto twin=f.b; twin.cardRandomRng=Random(98765);
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden-order invariance");
        const auto before=f.b.cards.drawPile;
        f.play(id,up);
        const bool exhaust=id!=CardId::LIMIT_BREAK||!up;
        const int loss=id==CardId::DISARM?(up?3:2):id==CardId::DARK_SHACKLES?(up?15:9):0;
        check(f.b.player.energy==20-(id==CardId::DARK_SHACKLES?0:1),"variant cost");
        check(f.b.cards.exhaustPile.size()==(exhaust?1:0)&&f.b.cards.discardPile.size()==(exhaust?0:1),"variant exhaust lifecycle");
        check(f.b.player.strength==(id==CardId::LIMIT_BREAK?8:4),"player Strength amount");
        check(f.b.monsters.arr[0].strength==3-loss,"enemy Strength amount");
        check(f.b.monsters.arr[0].getStatus<MS::SHACKLED>()==(id==CardId::DARK_SHACKLES?loss:0),"conditional restore amount");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.monsters.arr[0].strength==3-loss,"sample preserves negative Strength");
        check(f.b.monsters.arr[0].getStatus<MS::SHACKLED>()==(id==CardId::DARK_SHACKLES?loss:0),"sample preserves pending restoration");
        if(frozen) for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"Frozen Eye order");
        f.end();
        check(f.b.player.curHp==70-std::max(0,9-loss),"enemy phase uses reduced Strength");
        check(f.b.monsters.arr[0].strength==(id==CardId::DARK_SHACKLES?3:3-loss),"restore only after enemy action");
        check(f.b.monsters.arr[0].getStatus<MS::SHACKLED>()==0,"restore power removed");
    }
}
void artifactsAndStacking() {
    for(auto id:{CardId::DISARM,CardId::DARK_SHACKLES}) for(bool up:{false,true}) for(int artifacts:{1,2}) {
        Fixture f; auto &m=f.b.monsters.arr[0]; m.strength=3; m.buff<MS::ARTIFACT>(artifacts);
        f.play(id,up);
        check(m.strength==3&&m.getStatus<MS::ARTIFACT>()==artifacts-1,"Artifact blocks one Strength debuff");
        check(m.getStatus<MS::SHACKLED>()==0,"Artifact at play prevents restore registration");
        f.end(); check(m.strength==3,"blocked Shackles must not grant free Strength later");
    }
    Fixture stacked; auto &m=stacked.b.monsters.arr[0]; m.strength=3;
    stacked.play(CardId::DARK_SHACKLES); stacked.play(CardId::DARK_SHACKLES,true); stacked.play(CardId::DISARM,true);
    check(m.strength==-24&&m.getStatus<MS::SHACKLED>()==24,"stacked temporary and permanent reductions");
    stacked.end(); check(m.strength==0&&stacked.b.player.curHp==70,"only temporary loss restores");
    stacked.end(); check(stacked.b.player.curHp==64,"restored next attack and persistent Disarm");
}
void limitBreak() {
    for(bool up:{false,true}) for(int strength:{-600,-3,0,4,600,999}) for(int artifact:{0,1}) {
        Fixture f; f.b.player.strength=strength; f.b.player.artifact=artifact;
        f.play(CardId::LIMIT_BREAK,up);
        const bool blocked=strength<0&&artifact>0;
        check(f.b.player.strength==(blocked?strength:std::clamp(strength*2,-999,999)),"signed doubling, Artifact and cap");
        check(f.b.player.artifact==artifact-int(blocked),"only negative Strength consumes Artifact");
    }
    for(bool up:{false,true}) {
        Fixture f; f.play(CardId::FLEX,up); f.play(CardId::LIMIT_BREAK,up); f.end();
        check(f.b.player.strength==(up?4:2),"doubling Flex does not double scheduled Strength loss");
        Fixture g; g.play(CardId::INFLAME,up); g.play(CardId::RUPTURE,up);
        g.b.player.loseHp(g.b,2,true); g.drain(); g.play(CardId::LIMIT_BREAK,up);
        check(g.b.player.strength==2*((up?3:2)+(up?2:1)),"Inflame and Rupture compose before doubling");
    }
    Fixture exhaust; exhaust.play(CardId::FEEL_NO_PAIN); exhaust.play(CardId::LIMIT_BREAK);
    check(exhaust.b.player.block==3,"base Limit Break exhaustion triggers Feel No Pain");
    Fixture retained; retained.play(CardId::FEEL_NO_PAIN); retained.play(CardId::LIMIT_BREAK,true);
    check(retained.b.player.block==0,"upgraded Limit Break does not exhaust");
}
void strengthBounds() {
    Fixture f; auto &p=f.b.player; auto &m=f.b.monsters.arr[0];
    p.strength=998; p.buff<PS::STRENGTH>(4); check(p.strength==999,"player buff cap");
    p.strength=-998; p.debuff<PS::STRENGTH>(-4,false); check(p.strength==-999,"player debuff floor");
    m.strength=998; m.buff<MS::STRENGTH>(4); check(m.strength==999,"monster buff cap");
    m.strength=-998; f.play(CardId::DISARM,true); check(m.strength==-999,"monster debuff floor");
    m.strength=-995; f.play(CardId::DARK_SHACKLES);
    check(m.strength==-999,"temporary reduction clamps");
    f.end(); check(m.strength==-990,"restore full scheduled amount after clamped reduction");
    m.setStatus<MS::SHACKLED>(995); m.buff<MS::SHACKLED>(15);
    check(m.getStatus<MS::SHACKLED>()==999,"stacked restoration amount also caps at999");
    m.strength=-999; m.applyEndOfTurnTriggers(f.b);
    check(m.strength==0,"restore uses capped accumulated amount");
    m.buff<MS::SHACKLED>(126); m.buff<MS::SHACKLED>(15);
    check(m.getStatus<MS::SHACKLED>()==141,"restore storage must represent values beyond signed8bit");
}
void multiHitModifiers() {
    // Synthetic move probe of shared damage arithmetic, NOT Byrd encounter admission.
    for(auto id:{CardId::DISARM,CardId::DARK_SHACKLES}) for(bool up:{false,true})
    for(bool weak:{false,true}) for(bool vulnerable:{false,true}) {
        Fixture f; auto &m=f.b.monsters.arr[0]; m.strength=5; m.setMove(MMID::BYRD_PECK);
        if(weak) m.addDebuff<MS::WEAK>(2,false);
        if(vulnerable) f.b.player.debuff<PS::VULNERABLE>(2,false);
        f.play(id,up);
        const int loss=id==CardId::DISARM?(up?3:2):(up?15:9);
        int damage=std::max(0,6-loss);
        if(weak) damage=int(damage*.75);
        if(vulnerable) damage=int(damage*1.5);
        f.end();
        check(f.b.player.curHp==70-5*damage,"Strength reduction applies to every hit before Weak/Vulnerable rounding");
    }
}
}
int main() {
    variants(); artifactsAndStacking(); limitBreak(); strengthBounds(); multiHitModifiers();
    std::cout<<"STRENGTH_MODIFICATION_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
