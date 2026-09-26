#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int checks=0, failures=0;
void check(bool ok,const char *msg) {++checks;if(!ok){++failures;if(failures<=20)std::cerr<<"FAIL: "<<msg<<'\n';}}
struct Fixture {
    GameContext game; BattleContext b;
    Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1;game.relics={};if(frozen)game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::SMALL_SLIMES);b.init(game);b.executeActions();
        b.cards=CardManager{};b.cards.nextUniqueCardId=100;b.player.energy=20;
        b.player.curHp=70;b.player.maxHp=80;b.player.buff<PS::THORNS>(1);
        for(int i=0;i<2;++i)b.monsters.arr[i].curHp=b.monsters.arr[i].maxHp=1000;
    }
    void supply(int n) {for(int i=0;i<n;++i)b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));}
    void add(CardId id,bool up=false) {b.cards.createTempCardInHand(CardInstance(id,up));}
    void run() {b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void use(int i=0,int target=0) {
        search::Action a(search::ActionType::CARD,i,target);
        check(a.isValidAction(b),"tested attack is a legal native action");
        if(a.isValidAction(b))a.execute(b);
    }
};
int damage(int base,int strength,bool weak,bool vulnerable,int block) {
    float x=base+strength;if(weak)x*=.75f;if(vulnerable)x*=1.5f;
    return std::max(0,int(x)-block);
}
void matrix() {
    for(auto id:{CardId::DRAMATIC_ENTRANCE,CardId::MIND_BLAST})for(bool up:{false,true})
    for(int count:{0,1,5,20})for(int strength:{-3,0,4})for(bool weak:{false,true})for(bool vuln:{false,true}) {
        Fixture f;f.supply(count);f.add(id,up);
        f.b.player.buff<PS::STRENGTH>(strength);if(weak)f.b.player.debuff<PS::WEAK>(1);
        if(vuln)f.b.monsters.arr[0].addDebuff<MS::VULNERABLE>(1,false);
        f.b.monsters.arr[0].block=2;f.b.monsters.arr[1].block=1;
        const bool area=id==CardId::DRAMATIC_ENTRANCE;
        check(f.b.cards.hand[0].cost==(area?0:up?1:2),"variant energy costs");
        check(f.b.cards.hand[0].requiresTarget()==!area,"area vs single target");
        f.use();const int base=area?(up?12:8):count;
        check(1000-f.b.monsters.arr[0].curHp==damage(base,strength,weak,vuln,2),"target damage uses count before modifiers");
        check(1000-f.b.monsters.arr[1].curHp==(area?damage(base,strength,weak,false,1):0),"area hits each target independently; Mind Blast does not");
        check(f.b.player.energy==20-(area?0:up?1:2),"exact energy payment");
        check(f.b.cards.drawPile.size()==static_cast<unsigned>(count),"attack does not consume the counted draw pile");
        check(f.b.cards.exhaustPile.size()==(area?1u:0u)&&f.b.cards.discardPile.size()==(area?0u:1u),"correct post-play lifecycle");
    }
}
void orderingAndCallbacks() {
    for(bool up:{false,true})for(bool drawFirst:{false,true}) {
        Fixture f;f.supply(8);f.add(CardId::MIND_BLAST,up);f.add(CardId::BATTLE_TRANCE);
        if(drawFirst)f.use(1);
        const int hp=f.b.monsters.arr[0].curHp;f.use();
        check(hp-f.b.monsters.arr[0].curHp==(drawFirst?5:8),"drawing first reduces Mind Blast damage");
        if(!drawFirst)f.use();
        check(f.b.cards.drawPile.size()==5,"same final pile count for both sequences");
        Fixture shuffle;shuffle.supply(2);shuffle.add(CardId::MIND_BLAST,up);shuffle.add(CardId::DEEP_BREATH);
        for(int i=0;i<4;++i)shuffle.b.cards.createTempCardInDiscard(CardInstance(CardId::DEFEND_RED));
        shuffle.use(1);shuffle.use();
        check(shuffle.b.monsters.arr[0].curHp==995,"shuffle and draw changes counted supply before attack");
    }
    for(auto id:{CardId::DRAMATIC_ENTRANCE,CardId::MIND_BLAST})for(bool up:{false,true}) {
        Fixture f;f.supply(8);f.add(id,up);
        f.b.monsters.arr[0].buff<MS::THORNS>(3);
        f.b.player.buff<PS::FEEL_NO_PAIN>(3);f.b.player.buff<PS::DARK_EMBRACE>(1);
        f.use();const bool area=id==CardId::DRAMATIC_ENTRANCE;
        check(f.b.player.curHp==67,"normal attack triggers Thorns");
        // These powers share native uniquePower0; test separately, not on a synthetic hybrid.
        Fixture angry;angry.supply(8);angry.add(id,up);angry.b.monsters.arr[0].buff<MS::ANGRY>(2);angry.use();
        check(angry.b.monsters.arr[0].getStatus<MS::STRENGTH>()==2,"normal attack triggers Angry");
        check(f.b.player.block==(area?3:0),"only exhausted entrance triggers Feel No Pain");
        check(f.b.cards.drawPile.size()==(area?7u:8u),"exhaust draw follows attack");
        Fixture repeated;repeated.supply(8);repeated.add(CardId::DOUBLE_TAP);repeated.add(id,up);
        repeated.use();repeated.use();
        check(repeated.b.monsters.arr[0].curHp==1000-2*(area?(up?12:8):8),"Double Tap repeats both attack variants");
        check(repeated.b.cards.exhaustPile.size()==(area?1u:0u),"repeat does not duplicate intrinsic exhaustion");
        Fixture automatic;automatic.supply(8);
        automatic.b.cards.createTempCardInDrawPile(8,CardInstance(id,up));
        automatic.b.playTopCardInDrawPile(0,true);automatic.run();
        check(automatic.b.monsters.arr[0].curHp==1000-(area?(up?12:8):8),"autoplay excludes the played card from the draw-pile count");
        check(automatic.b.player.energy==20&&automatic.b.cards.exhaustPile.size()==1,"free autoplay exhausts the source exactly once");
    }
    Fixture dead;dead.supply(1);dead.add(CardId::DRAMATIC_ENTRANCE);
    dead.b.monsters.arr[1].curHp=0;dead.b.monsters.monstersAlive=1;dead.use();
    check(dead.b.monsters.arr[1].curHp==0,"area attack skips dead targets");
}
void openings() {
    for(int seed=1;seed<=16;++seed)for(bool up:{false,true}) {
        GameContext game(CharacterClass::IRONCLAD,seed,0);game.relics={};game.deck=Deck{};
        for(int i=0;i<10;++i)game.deck.obtainRaw(Card(CardId::DEFEND_RED));
        game.deck.obtainRaw(Card(CardId::DRAMATIC_ENTRANCE,up));game.deck.obtainRaw(Card(CardId::MIND_BLAST,up));
        game.enterBattle(MonsterEncounter::CULTIST);BattleContext b;b.init(game);b.executeActions();
        bool entrance=false,mind=false;
        for(int i=0;i<b.cards.cardsInHand;++i) {entrance|=b.cards.hand[i].id==CardId::DRAMATIC_ENTRANCE;mind|=b.cards.hand[i].id==CardId::MIND_BLAST;}
        check(entrance&&mind,"both innate variants appear in actual opening hand across seeds");
        check(b.cards.cardsInHand==5&&b.cards.drawPile.size()==7,"innate cards replace ordinary draws, not added cards");
        const std::array<std::uint64_t,7> seeds={101,102,103,104,105,106,107};
        public_sampling::resampleCombatContinuation(game,b,seeds);
        check(b.cards.cardsInHand==5&&b.cards.drawPile.size()==7,"actual resolved opening root is sampleable after innate cards are drawn");
        // Construct an unresolved innate ordering constraint; do not silently
        // broaden the existing sampler to unknown constrained draw orders.
        int index=0;while(b.cards.hand[index].id!=CardId::MIND_BLAST)++index;
        const auto card=b.cards.hand[index];b.cards.removeFromHandAtIdx(index);b.cards.drawPile.push_back(card);
        bool rejected=false;
        try {public_sampling::resampleCombatContinuation(game,b,seeds);}
        catch(const std::runtime_error &e) {rejected=std::string(e.what()).find("innate/bottled")!=std::string::npos;}
        check(rejected,"unrevealed innate-order constraint retains conservative rejection");
    }
}
using Cards=std::map<int,std::array<int,3>>;
Cards visibleCards(const BattleContext &b) {
    Cards r;auto add=[&](const CardInstance &c){r[c.uniqueId]={int(c.id),c.getUpgradeCount(),c.costForTurn};};
    for(int i=0;i<b.cards.cardsInHand;++i)add(b.cards.hand[i]);
    for(const auto &c:b.cards.drawPile)add(c);for(const auto &c:b.cards.discardPile)add(c);for(const auto &c:b.cards.exhaustPile)add(c);
    return r;
}
void roots() {
    for(int sample=1;sample<=16;++sample)for(bool frozen:{false,true})
    for(auto id:{CardId::DRAMATIC_ENTRANCE,CardId::MIND_BLAST})for(bool up:{false,true}) {
        Fixture f(frozen);f.supply(8);f.add(id,up);auto twin=f.b;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        twin.cardRandomRng=Random(98765);
        const std::array<std::uint64_t,7> seeds={static_cast<unsigned>(sample),102,103,104,105,106,107};
        auto before=visibleCards(f.b);auto draw=f.b.cards.drawPile;
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        check(visibleCards(f.b)==before,"sampling preserves public card identities and variant costs");
        for(size_t i=0;i<draw.size();++i) {
            check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden-order paired roots agree");
            if(frozen)check(f.b.cards.drawPile[i].uniqueId==draw[i].uniqueId,"Frozen Eye order retained");
        }
        search::Action(search::ActionType::CARD,0,0).execute(twin);f.use();
        for(int i=0;i<2;++i)check(f.b.monsters.arr[i].curHp==twin.monsters.arr[i].curHp,"paired damage outcomes agree");
        check(visibleCards(f.b)==visibleCards(twin),"paired post-play lifecycle agrees");
        auto after=visibleCards(f.b);public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(visibleCards(f.b)==after,"resolved post-play root preserves public cards");
    }
}
}
int main(){matrix();orderingAndCallbacks();openings();roots();std::cout<<"OPENING_ATTACK_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";return failures?1:0;}
