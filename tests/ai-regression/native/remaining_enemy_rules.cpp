#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const std::string &why) {
    ++checks;if(!ok) {if(failures<25) std::cerr<<why<<'\n';++failures;}
}
struct Fixture {
    GameContext g; BattleContext b;
    Fixture(ME e,int seed=7):g(CharacterClass::IRONCLAD,seed,0) {
        g.act=3;g.floorNum=40;g.enterBattle(e);b.init(g);b.executeActions();
        g.regainControlAction=[](GameContext &) {};
        b.player.curHp=b.player.maxHp=10000;
        for(int i=0;i<b.monsters.monsterCount;++i) if(b.monsters.arr[i].id!=MonsterId::INVALID)
            b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
    }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions(); }
    void end() { search::Action(search::ActionType::END_TURN).execute(b); }
    void kill(int i) { b.monsters.arr[i].damage(b,99999);drain(); }
    void play(CardId id,int target=0) {
        b.cards.createTempCardInHand(CardInstance(id));b.player.energy=100;
        search::Action(search::ActionType::CARD,b.cards.cardsInHand-1,target).execute(b);
    }
};
int count(const BattleContext &b,CardId id) {
    int n=0;for(int i=0;i<b.cards.cardsInHand;++i)n+=b.cards.hand[i].id==id;
    for(const auto *p:{&b.cards.drawPile,&b.cards.discardPile,&b.cards.exhaustPile})
        for(const auto &c:*p)n+=c.id==id;
    return n;
}
void attacks() {
    struct Row {ME e;int slot;MMID move;int damage,hits;};
    // A0 original-game DamageAction packets, independently specified. In
    // particular Chomp has TWO packets, not the simulator's old single packet.
    const Row rows[] {
        {ME::AWAKENED_ONE,2,MMID::AWAKENED_ONE_SLASH,20,1},
        {ME::AWAKENED_ONE,2,MMID::AWAKENED_ONE_SOUL_STRIKE,6,4},
        {ME::AWAKENED_ONE,2,MMID::AWAKENED_ONE_DARK_ECHO,40,1},
        {ME::AWAKENED_ONE,2,MMID::AWAKENED_ONE_SLUDGE,18,1},
        {ME::AWAKENED_ONE,2,MMID::AWAKENED_ONE_TACKLE,10,3},
        {ME::MASKED_BANDITS_EVENT,0,MMID::POINTY_ATTACK,5,2},
        {ME::MASKED_BANDITS_EVENT,1,MMID::ROMEO_AGONIZING_SLASH,10,1},
        {ME::MASKED_BANDITS_EVENT,1,MMID::ROMEO_CROSS_SLASH,15,1},
        {ME::MASKED_BANDITS_EVENT,2,MMID::BEAR_LUNGE,9,1},
        {ME::MASKED_BANDITS_EVENT,2,MMID::BEAR_MAUL,18,1},
        {ME::AUTOMATON,1,MMID::BRONZE_AUTOMATON_FLAIL,7,2},
        {ME::AUTOMATON,1,MMID::BRONZE_AUTOMATON_HYPER_BEAM,45,1},
        {ME::THE_HEART,0,MMID::CORRUPT_HEART_BLOOD_SHOTS,2,12},
        {ME::THE_HEART,0,MMID::CORRUPT_HEART_ECHO,40,1},
        {ME::REPTOMANCER,1,MMID::DAGGER_STAB,9,1},
        {ME::REPTOMANCER,1,MMID::DAGGER_EXPLODE,25,1},
        {ME::REPTOMANCER,2,MMID::REPTOMANCER_BIG_BITE,30,1},
        {ME::REPTOMANCER,2,MMID::REPTOMANCER_SNAKE_STRIKE,13,2},
        {ME::THREE_DARKLINGS,0,MMID::DARKLING_CHOMP,8,2},
        {ME::DONU_AND_DECA,0,MMID::DECA_BEAM,10,2},
        {ME::DONU_AND_DECA,1,MMID::DONU_BEAM,10,2},
        {ME::GIANT_HEAD,0,MMID::GIANT_HEAD_COUNT,13,1},
        {ME::GREMLIN_LEADER,3,MMID::GREMLIN_LEADER_STAB,6,3},
        {ME::NEMESIS,0,MMID::NEMESIS_ATTACK,6,3},
        {ME::NEMESIS,0,MMID::NEMESIS_SCYTHE,45,1},
        {ME::ORB_WALKER,0,MMID::ORB_WALKER_LASER,10,1},
        {ME::ORB_WALKER,0,MMID::ORB_WALKER_CLAW,15,1},
        {ME::SPIRE_GROWTH,0,MMID::SPIRE_GROWTH_QUICK_TACKLE,16,1},
        {ME::SPIRE_GROWTH,0,MMID::SPIRE_GROWTH_SMASH,22,1},
        {ME::SHIELD_AND_SPEAR,0,MMID::SPIRE_SHIELD_BASH,12,1},
        {ME::SHIELD_AND_SPEAR,0,MMID::SPIRE_SHIELD_SMASH,34,1},
        {ME::SHIELD_AND_SPEAR,1,MMID::SPIRE_SPEAR_BURN_STRIKE,5,2},
        {ME::SHIELD_AND_SPEAR,1,MMID::SPIRE_SPEAR_SKEWER,10,3},
        {ME::CHAMP,0,MMID::THE_CHAMP_HEAVY_SLASH,16,1},
        {ME::CHAMP,0,MMID::THE_CHAMP_FACE_SLAP,12,1},
        {ME::CHAMP,0,MMID::THE_CHAMP_EXECUTE,10,2},
        {ME::COLLECTOR,2,MMID::THE_COLLECTOR_FIREBALL,18,1},
        {ME::MAW,0,MMID::THE_MAW_SLAM,25,1},
        {ME::TIME_EATER,0,MMID::TIME_EATER_REVERBERATE,7,3},
        {ME::TIME_EATER,0,MMID::TIME_EATER_HEAD_SLAM,26,1},
        {ME::WRITHING_MASS,0,MMID::WRITHING_MASS_FLAIL,15,1},
        {ME::WRITHING_MASS,0,MMID::WRITHING_MASS_MULTI_STRIKE,7,3},
        {ME::WRITHING_MASS,0,MMID::WRITHING_MASS_STRONG_STRIKE,32,1},
        {ME::WRITHING_MASS,0,MMID::WRITHING_MASS_WITHER,10,1},
    };
    for(const auto &r:rows) for(int block:{0,1,7,15,30,99}) for(bool buffer:{false,true}) {
        Fixture f(r.e);auto &m=f.b.monsters.arr[r.slot];m.moveHistory[0]=r.move;m.strength=0;
        f.b.player.removeStatus<PS::SURROUNDED>();f.b.player.block=block;
        if(buffer)f.b.player.buff<PS::BUFFER>(1);
        auto info=m.getMoveBaseDamage(f.b);
        const auto label=std::string(monsterMoveStrings[static_cast<int>(r.move)]);
        check(info.damage==r.damage && info.attackCount==r.hits,label+" public packets");
        int remaining=block,shield=buffer,loss=0;
        for(int i=0;i<r.hits;++i) {
            int hit=r.damage,blocked=std::min(hit,remaining);remaining-=blocked;hit-=blocked;
            if(hit>0 && shield){--shield;hit=0;}loss+=hit;
        }
        m.takeTurn(f.b);f.drain();
        check(10000-f.b.player.curHp==loss,label+" actual block/Buffer packets");
    }
}
void summonedPowers() {
    int mad=0;
    for(int seed=0;seed<96;++seed) {
        Fixture f(ME::GREMLIN_LEADER,seed);
        f.kill(1);f.kill(2);
        Actions::SummonGremlins().actFunc(f.b);f.drain();
        for(int i:{1,2}) {
            const auto &m=f.b.monsters.arr[i];
            check(m.hasStatus<MS::MINION>(),"summoned gremlin must have Minion");
            if(m.id==MonsterId::MAD_GREMLIN) {
                ++mad;check(m.getStatus<MS::ANGRY>()==1,"summoned Mad Gremlin lacks original Angry1");
                const int strength=m.strength;
                f.b.monsters.arr[i].attacked(f.b,1);f.drain();
                check(m.strength==strength+1,"summoned Mad Gremlin missed attack-trigger Strength");
            }
        }
    }
    check(mad>0,"Mad Gremlin test did not cover spawn");
}
void stasis() {
    for(bool discarded:{false,true}) for(bool fullHand:{false,true}) {
        Fixture f(ME::AUTOMATON);f.end();
        f.b.cards=CardManager{};f.b.cards.nextUniqueCardId=100;
        auto &cards=f.b.cards;
        cards.createTempCardInDrawPile(0,CardInstance(CardId::OFFERING));
        cards.createTempCardInDrawPile(0,CardInstance(CardId::STRIKE_RED));
        if(discarded) {auto pile=cards.drawPile;cards.drawPile.clear();cards.discardPile=pile;}
        auto &orb=f.b.monsters.arr[0];orb.moveHistory[0]=MMID::BRONZE_ORB_STASIS;
        orb.takeTurn(f.b);f.drain();
        check(orb.hasStatus<MS::STASIS>() && cards.stasisCards[0].id==CardId::OFFERING,"Stasis rarity/draw-discard selection");
        check(count(f.b,CardId::OFFERING)==0 && count(f.b,CardId::STRIKE_RED)==1,"Stasis removes exactly one card");
        const int uid=cards.stasisCards[0].uniqueId;
        if(fullHand)for(int i=0;i<10;++i)cards.createTempCardInHand(CardInstance(CardId::DEFEND_RED));
        f.kill(0);
        check(cards.stasisCards[0].id==CardId::INVALID && count(f.b,CardId::OFFERING)==1,"Stasis return lost/duplicated card");
        check((fullHand?cards.discardPile.back().uniqueId:cards.hand[0].uniqueId)==uid,"Stasis return must preserve identity");
    }
}
void reactivePowers() {
    for(int facing:{0,1}) {
        Fixture f(ME::SHIELD_AND_SPEAR);
        f.play(CardId::STRIKE_RED,facing);
        check(f.b.player.lastTargetedMonster==facing,"targeted attack did not turn player");
        f.play(CardId::DEFEND_RED);
        check(f.b.player.lastTargetedMonster==facing,"untargeted card must not turn player");
        check(f.b.monsters.arr[0].calculateDamageToPlayer(f.b,12)==(facing==0?12:18),"Shield back attack multiplier");
        check(f.b.monsters.arr[1].calculateDamageToPlayer(f.b,10)==(facing==1?10:15),"Spear back attack multiplier");
        f.kill(facing);
        check(!f.b.player.hasStatus<PS::SURROUNDED>(),"surviving pair must remove Surrounded on death");
        check(f.b.monsters.arr[1-facing].calculateDamageToPlayer(f.b,12)==12,"single survivor must have no back attack");
    }
    Fixture timer(ME::TIME_EATER);
    timer.b.cards=CardManager{};timer.b.cards.nextUniqueCardId=100;
    for(int i=0;i<11;++i)timer.play(CardId::ANGER);
    check(timer.b.turn==0 && timer.b.monsters.arr[0].getStatus<MS::TIME_WARP>()==11,"Time Warp first eleven plays");
    timer.play(CardId::ANGER);
    check(timer.b.turn==1 && timer.b.monsters.arr[0].getStatus<MS::TIME_WARP>()==0 &&
          timer.b.monsters.arr[0].strength==2,"twelfth card must force turn and add Strength");

    Fixture heart(ME::THE_HEART);auto &m=heart.b.monsters.arr[0];
    heart.b.cards=CardManager{};heart.b.cards.nextUniqueCardId=100;
    heart.play(CardId::ANGER);
    check(heart.b.player.curHp==9999,"Beat of Death must trigger from play");
    m.damage(heart.b,400);heart.drain();
    check(m.curHp==700 && m.getStatus<MS::INVINCIBLE>()==0,"Heart per-turn 300 damage cap");
    m.damage(heart.b,10);heart.drain();check(m.curHp==700,"exhausted cap must block further damage");
    heart.end();check(m.getStatus<MS::INVINCIBLE>()==300,"Heart cap must reset next turn");

    Fixture nem(ME::NEMESIS);nem.end();
    check(nem.b.monsters.arr[0].getStatus<MS::INTANGIBLE>()==1,"Nemesis intangible on alternating turn");
    int hp=nem.b.monsters.arr[0].curHp;nem.play(CardId::STRIKE_RED);
    check(nem.b.monsters.arr[0].curHp==hp-1,"Intangible must cap a hit to one");
    nem.end();check(!nem.b.monsters.arr[0].hasStatus<MS::INTANGIBLE>(),"Nemesis intangible must expire");

    Fixture head(ME::GIANT_HEAD);head.play(CardId::DEFEND_RED);
    check(head.b.monsters.arr[0].getStatus<MS::SLOW>()==1,"Slow must count played cards");
    int oldHp=head.b.monsters.arr[0].curHp;head.play(CardId::BLUDGEON);
    check(head.b.monsters.arr[0].curHp==oldHp-35,"Slow must modify actual attack damage");
    for(int turn=0;turn<12;++turn) {
        const int t=head.b.turn;
        if(t>=4)check(head.b.monsters.arr[0].getMoveBaseDamage(head.b).damage==30+5*std::min(t-4,6),"Giant Head public escalating damage");
        head.end();
    }
}
}
int main() {
    try {attacks();summonedPowers();stasis();reactivePowers();}
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
    std::cout<<"remaining-enemy-rules-v1: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
