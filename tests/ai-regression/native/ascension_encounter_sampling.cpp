#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
using namespace sts::public_sampling;
namespace {
int checks=0, roots=0, hidden=0, phases=0;
std::string context;
void check(bool ok,const char *why) {++checks;if(!ok)throw std::runtime_error(context+": "+why);}
template<class F> void rejects(F f,const char *why) {
    ++checks;try {f();}catch(const std::runtime_error &){return;}
    throw std::runtime_error(context+": "+why);
}
std::string dump(const BattleContext &b) {std::ostringstream s;s<<b;return s.str();}
const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
struct Fixture {
    GameContext g;BattleContext b;PublicEnemyHistory history;
    Fixture(ME e,int asc,int seed=7):g(CharacterClass::IRONCLAD,seed,asc) {
        g.act=3;g.floorNum=40;g.enterBattle(e);b.init(g);b.executeActions();
        g.regainControlAction=[](GameContext &){};
        b.player.curHp=b.player.maxHp=10000;
        for(int i=0;i<b.monsters.monsterCount;++i)
            if(b.monsters.arr[i].id!=MonsterId::INVALID)b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
        observe();
    }
    void observe(){history.observe(publicEnemyFrame(b));}
    void end(){search::Action(search::ActionType::END_TURN).execute(b);observe();}
    void hit(int i,int amount){b.monsters.arr[i].block=0;b.monsters.arr[i].damage(b,amount);
        b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();observe();}
};
void sample(Fixture &f) {
    if(f.b.outcome!=Outcome::UNDECIDED)return;
    context=std::string(monsterEncounterStrings[static_cast<int>(f.b.encounter)])+
        " A"+std::to_string(f.b.ascension)+" turn="+std::to_string(f.b.turn);
    const auto source=dump(f.b);const auto frame=publicEnemyFrame(f.b);auto a=f.b,b=f.b;
    b.aiRng=Random(901);b.cardRandomRng=Random(902);b.miscRng=Random(903);
    b.monsterHpRng=Random(904);b.potionRng=Random(905);b.shuffleRng=Random(906);
    std::reverse(b.cards.drawPile.begin(),b.cards.drawPile.end());
    const bool tracked=tracksPublicEnemyHistory(b.encounter);
    if(tracked)for(int i=0;i<b.monsters.monsterCount;++i){
        auto &m=b.monsters.arr[i];const int original=m.miscInfo;m.miscInfo=999;
        if(!(publicEnemyFrame(b)==frame))m.miscInfo=original;else ++hidden;
        m.moveHistory[1]=MMID::INVALID;m.escapeNext=true;
        if(m.id==MonsterId::HEXAGHOST)m.uniquePower0=99;
    }
    check(publicEnemyFrame(b)==frame,"poison changed public evidence");
    resampleCombatContinuation(f.g,a,seeds,-1,&f.history);
    resampleCombatContinuation(f.g,b,seeds,-1,&f.history);
    check(publicEnemyFrame(a)==frame,"public intent/damage changed");
    check(dump(a)==dump(b),"same-public samples depend on hidden state");
    check(dump(f.b)==source,"source mutated");
    for(int i=0;i<a.monsters.monsterCount;++i){
        const auto &m=a.monsters.arr[i],&o=f.b.monsters.arr[i];
        if(m.id==MonsterId::THE_GUARDIAN)check(m.miscInfo==o.miscInfo,"Guardian threshold differs");
        if(m.id==MonsterId::DARKLING){
            const int lo=f.b.ascension>=2?9:7;
            check(m.miscInfo>=lo&&m.miscInfo<=lo+4,"Darkling damage outside ascension prior");
            if(o.moveHistory[0]==MMID::DARKLING_NIP)check(m.miscInfo==o.miscInfo,"lost revealed Nip");
        }
    }
    search::Action(search::ActionType::END_TURN).execute(a);
    search::Action(search::ActionType::END_TURN).execute(b);
    check(dump(a)==dump(b),"same-public continuation differs");
    if(a.outcome==Outcome::UNDECIDED){auto history=f.history;history.observe(publicEnemyFrame(a));
        resampleCombatContinuation(f.g,a,seeds,-1,&history);}
    ++roots;
}
void guardian(int asc){
    Fixture f(ME::THE_GUARDIAN,asc);const int initial=asc>=9?35:30;
    check(f.b.monsters.arr[0].miscInfo==initial,"native initial threshold");
    for(int i=0;i<2;++i){sample(f);f.end();}
    for(int cycle=0;cycle<3;++cycle){
        const int remaining=f.b.monsters.arr[0].getStatus<MS::MODE_SHIFT>();
        f.hit(0,1);sample(f);f.hit(0,remaining-1);sample(f);
        for(int i=0;i<3;++i){f.end();sample(f);}
        check(f.b.monsters.arr[0].miscInfo==initial+10*(cycle+1),"repeated Guardian phase");++phases;
    }
}
void darkling(int asc){
    std::set<int> realized;
    for(int seed=0;seed<64;++seed){
        Fixture f(ME::THREE_DARKLINGS,asc,seed);
        const int base=f.b.monsters.arr[0].miscInfo;realized.insert(base);
        f.b.monsters.arr[0].setMove(MMID::DARKLING_NIP);
        f.b.monsters.arr[1].setMove(MMID::DARKLING_HARDEN);
        f.b.monsters.arr[2].setMove(MMID::DARKLING_HARDEN);
        check(f.b.monsters.arr[0].getMoveBaseDamage(f.b).damage==base,"Nip intent adds ascension twice");
        const int hp=f.b.player.curHp;f.b.player.block=0;f.end();
        check(hp-f.b.player.curHp==base,"Nip executed damage adds ascension twice");
        Fixture revive(ME::THREE_DARKLINGS,asc,seed);revive.hit(0,99999);sample(revive);
        for(int i=0;i<3;++i){revive.end();sample(revive);}
        check(!revive.b.monsters.arr[0].halfDead,"revival lifecycle not exercised");++phases;
    }
    const int lo=asc>=2?9:7;
    check(realized==std::set<int>{lo,lo+1,lo+2,lo+3,lo+4},"birth damage support incomplete");
}
void special(int asc){
    guardian(asc);darkling(asc);
    for(auto e:{ME::GREMLIN_LEADER,ME::AUTOMATON,ME::COLLECTOR,ME::REPTOMANCER}){
        Fixture f(e,asc);const int leader=e==ME::GREMLIN_LEADER?3:e==ME::AUTOMATON?1:2;
        for(int turn=0;turn<16;++turn){
            if(turn%3==2)for(int i=0;i<f.b.monsters.monsterCount;++i)
                if(i!=leader&&f.b.monsters.arr[i].isAlive())f.hit(i,99999);
            sample(f);f.end();++phases;
        }
    }
    Fixture awake(ME::AWAKENED_ONE,asc);awake.hit(2,99999);sample(awake);awake.end();sample(awake);
    check(!awake.b.monsters.arr[2].halfDead,"Awakened revival");
    for(auto e:{ME::CHAMP,ME::TIME_EATER}){
        Fixture f(e,asc);f.end();f.b.monsters.arr[0].curHp=300;f.observe();
        for(int i=0;i<12;++i){sample(f);f.end();}++phases;
    }
    for(auto e:{ME::LARGE_SLIME,ME::SLIME_BOSS}){
        Fixture f(e,asc);f.hit(0,510);sample(f);f.end();sample(f);
        for(int i=0;i<6&&f.b.outcome==Outcome::UNDECIDED;++i){f.end();sample(f);}++phases;
    }
}
}
int main(){try{
    std::set<ME> encounters;for(const auto &r:encounterCompositionRecipes)encounters.insert(r.encounter);
    for(int asc:{0,1,2,3,4,7,8,9,10}){
        for(auto e:encounters)for(int seed=0;seed<8;++seed){Fixture f(e,asc,seed);
            for(int turn=0;turn<14&&f.b.outcome==Outcome::UNDECIDED;++turn){sample(f);f.end();}}
        special(asc);
    }
    for(auto e:encounters){Fixture f(e,10);sample(f);
        const bool originalWide=e==ME::CULTIST||e==ME::JAW_WORM||e==ME::TWO_LOUSE;
        if(!originalWide)for(int asc:{11,17,18,19,20}){auto b=f.b;b.ascension=asc;
            rejects([&]{validateEncounterRecipe(b,&f.history);},"unreviewed ascension admitted");}
        auto b=f.b;b.ascension=-1;rejects([&]{validateEncounterRecipe(b,&f.history);},"negative ascension");
        b=f.b;b.ascension=21;rejects([&]{validateEncounterRecipe(b,&f.history);},"out-of-range ascension");
        if(needsPublicEnemyHistory(e))rejects([&]{auto c=f.b;resampleCombatContinuation(f.g,c,seeds);},"missing history");
    }
    check(hidden>0&&phases>0,"coverage counters empty");
    std::cout<<"{\"status\":\"passed\",\"encounters\":"<<encounters.size()<<",\"ascensions\":9,\"roots\":"<<roots
        <<",\"checks\":"<<checks<<",\"hidden_cache_probes\":"<<hidden<<",\"phase_probes\":"<<phases<<"}\n";
    return 0;
}catch(const std::exception &e){std::cerr<<context<<": "<<e.what()<<'\n';return 1;}}
