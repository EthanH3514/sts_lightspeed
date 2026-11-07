#include <array>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
using namespace sts;
namespace {
int checks=0, failures=0;
std::set<std::string> messages;
void check(bool ok,const char *why) {
    ++checks;
    if(!ok) { ++failures; messages.insert(why); }
}
std::string rng(const Random &r) {
    std::ostringstream s; s<<r.counter<<':'<<r.seed0<<':'<<r.seed1; return s.str();
}
struct Fixture {
    GameContext g; BattleContext b;
    explicit Fixture(int asc=0):g(CharacterClass::IRONCLAD,7,asc) {
        g.act=2; g.floorNum=20; g.enterBattle(MonsterEncounter::CENTURION_AND_HEALER);
        b.init(g); b.executeActions(); b.player.curHp=b.player.maxHp=10000;
    }
    void drain() { b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions(); }
};
void healerDecisions() {
    const std::array<std::array<int,2>,9> deficits{{
        {0,0},{15,0},{16,0},{8,8},{7,8},{10,11},{21,0},{0,21},{40,40}}};
    const MMID moves[]{MMID::INVALID,MMID::MYSTIC_HEAL,MMID::MYSTIC_BUFF,MMID::MYSTIC_ATTACK_DEBUFF};
    for(int asc:{0,17}) {
        Fixture f(asc); auto &ally=f.b.monsters.arr[0], &healer=f.b.monsters.arr[1];
        for(bool alive:{false,true}) for(const auto &lost:deficits) {
            ally.maxHp=healer.maxHp=100;
            ally.curHp=alive?100-lost[0]:0; healer.curHp=100-lost[1];
            f.b.monsters.monstersAlive=alive?2:1;
            for(auto current:moves) for(auto previous:moves) for(int roll=0;roll<100;++roll) {
                healer.moveHistory[0]=current; healer.moveHistory[1]=previous;
                const auto two=[&](MMID move){return current==move && previous==move;};
                const int missing=lost[1]+(alive?lost[0]:0);
                MMID expected;
                if(missing>(asc>=17?20:15) && !two(MMID::MYSTIC_HEAL)) expected=MMID::MYSTIC_HEAL;
                else if(roll>=40 && !(asc>=17?current==MMID::MYSTIC_ATTACK_DEBUFF:two(MMID::MYSTIC_ATTACK_DEBUFF)))
                    expected=MMID::MYSTIC_ATTACK_DEBUFF;
                else expected=two(MMID::MYSTIC_BUFF)?MMID::MYSTIC_ATTACK_DEBUFF:MMID::MYSTIC_BUFF;
                const auto before=rng(f.b.aiRng); int data=0;
                check(healer.getMoveForRoll(f.b,data,roll)==expected,"Mystic must sum living deficits and forbid a third consecutive Heal");
                check(rng(f.b.aiRng)==before,"Mystic deterministic decision consumed extra RNG");
            }
        }
    }
}
void centurionBlock() {
    for(int asc:{0,17}) for(bool alive:{false,true}) {
        Fixture f(asc); auto &knight=f.b.monsters.arr[0], &healer=f.b.monsters.arr[1];
        if(!alive) { healer.curHp=0; f.b.monsters.monstersAlive=1; }
        knight.block=healer.block=0; knight.moveHistory[0]=MMID::CENTURION_DEFEND;
        auto expectedRng=f.b.aiRng;
        if(alive) expectedRng.random(0); // Even a single eligible ally consumes a target roll.
        expectedRng.random(99); // RollMove follows the block action.
        knight.takeTurn(f.b);
        check(knight.block==0 && healer.block==0,"Centurion block must be queued");
        auto child=f.b; f.drain();
        child.inputState=InputState::EXECUTING_ACTIONS; child.executeActions();
        const int amount=asc>=17?20:15;
        check(knight.block==(alive?0:amount) && healer.block==(alive?amount:0),
              "Centurion Defend must target a living ally or itself if alone");
        check(rng(f.b.aiRng)==rng(expectedRng),"Centurion target/next-move RNG sequence");
        check(child.monsters.arr[0].block==knight.block && child.monsters.arr[1].block==healer.block,
              "queued Centurion action captured the source battle");
    }
    Fixture late;
    late.b.monsters.arr[0].moveHistory[0]=MMID::CENTURION_DEFEND;
    late.b.monsters.arr[0].takeTurn(late.b);
    late.b.monsters.arr[1].damage(late.b,9999);
    late.drain();
    check(late.b.monsters.arr[0].block==15,"Centurion target must resolve at action execution");
}
void healerEffects() {
    for(bool allyAlive:{false,true}) {
        Fixture f; auto &ally=f.b.monsters.arr[0], &healer=f.b.monsters.arr[1];
        ally.maxHp=healer.maxHp=100; ally.curHp=allyAlive?80:0; healer.curHp=92;
        f.b.monsters.monstersAlive=allyAlive?2:1;
        healer.moveHistory[0]=MMID::MYSTIC_HEAL; healer.takeTurn(f.b); f.drain();
        check(healer.curHp==100 && ally.curHp==(allyAlive?96:0),"Mystic heals living allies with max-HP cap");
        healer.moveHistory[0]=MMID::MYSTIC_BUFF; healer.takeTurn(f.b); f.drain();
        check(healer.strength==2 && ally.strength==(allyAlive?2:0),"Mystic buffs living allies");
    }
}
}
int main() {
    try {
        healerDecisions(); centurionBlock(); healerEffects();
        for(const auto &message:messages) std::cerr<<message<<'\n';
        std::cout<<"centurion-mystic-rules-v1: "<<checks<<" checks, "<<failures<<" failures\n";
        return failures?1:0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
