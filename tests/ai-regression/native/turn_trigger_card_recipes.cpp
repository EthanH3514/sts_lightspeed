#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include "combat/Actions.h"
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"
#include "sim/search/Action.h"
using namespace sts;
namespace {
int failures=0;
void check(bool ok,const char *message) {
    if(!ok) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(bool frozen=false,RelicId relic=RelicId::INVALID)
        : game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game); b.executeActions(); b.cards=CardManager{};
        b.cards.nextUniqueCardId=100; b.player.energy=20;
        b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=200;
        for(int i=0;i<10;++i) {
            CardInstance c(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED);
            c.setUniqueId(b.cards.nextUniqueCardId++); b.cards.drawPile.push_back(c);
        }
    }
    void execute() {b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();}
    void play(CardId id,bool up=false) {
        CardInstance c(id,up); c.setUniqueId(b.cards.nextUniqueCardId++);
        b.cards.hand[b.cards.cardsInHand++]=c;
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); execute();
    }
    void end() {search::Action(search::ActionType::END_TURN).execute(b);}
};
void variants() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::BRUTALITY,CardId::COMBUST,CardId::DEMON_FORM,CardId::BERSERK})
    for(bool up:{false,true}) for(bool frozen:{false,true}) {
        Fixture f(frozen); auto twin=f.b;
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden order leak");
        const auto before=f.b.cards.drawPile;
        f.play(id,up);
        check(f.b.player.energy==20-(id==CardId::COMBUST?1:id==CardId::DEMON_FORM?3:0),"immediate energy/cost");
        check(f.b.player.curHp==70&&f.b.player.strength==0&&f.b.cards.cardsInHand==0,"premature turn trigger");
        check(f.b.cards.discardPile.empty()&&f.b.cards.exhaustPile.empty(),"power lifecycle");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        if(frozen) for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"Frozen Eye order");
        f.end();
        check(f.b.player.curHp==70-(id==CardId::BRUTALITY||id==CardId::COMBUST?1:0),"turn HP loss");
        check(f.b.player.strength==(id==CardId::DEMON_FORM?(up?3:2):0),"turn Strength");
        check(f.b.player.energy==(id==CardId::BERSERK?4:3),"turn energy");
        check(f.b.cards.cardsInHand==(id==CardId::BRUTALITY?6:5),"turn draw");
        check(f.b.monsters.arr[0].curHp==200-(id==CardId::COMBUST?(up?7:5):0),"Combust damage");
        if(id==CardId::BERSERK) check(f.b.player.getStatus<PS::VULNERABLE>()==(up?0:1),"self Vulnerable duration");
    }
}
void interactions() {
    check(!isCardInnate(CardId::BRUTALITY,false)&&isCardInnate(CardId::BRUTALITY,true),"Brutality upgrade Innate");
    for(bool up:{false,true}) {
        Fixture f; f.play(CardId::COMBUST,up); f.play(CardId::COMBUST,!up);
        f.b.player.buff<PS::RUPTURE>(2); f.b.player.block=100;
        f.b.player.strength=20; f.b.player.debuff<PS::WEAK>(2,false);
        f.b.monsters.arr[0].buff<MS::VULNERABLE>(2);
        f.end();
        check(f.b.player.curHp==68&&f.b.player.combustHpLoss==2,"Combust stacks HP loss independently of damage");
        check(f.b.player.strength==22,"Combust triggers Rupture once per combined loss");
        check(f.b.monsters.arr[0].curHp==188,"Combust is non-attack damage");
        Fixture demon; demon.play(CardId::DEMON_FORM,up); demon.play(CardId::DEMON_FORM,!up);
        demon.end(); check(demon.b.player.strength==5,"Demon Form stacking");
        Fixture berserk; berserk.b.player.artifact=1; berserk.play(CardId::BERSERK,up);
        check(!berserk.b.player.hasStatus<PS::VULNERABLE>()&&berserk.b.player.artifact==0,"Artifact self-debuff");
        berserk.play(CardId::BERSERK,up); berserk.end();
        check(berserk.b.player.energy==5,"Berserk stacking independent of Artifact");
        Fixture brutality; brutality.b.player.buff<PS::RUPTURE>(2);
        brutality.play(CardId::BRUTALITY,up); brutality.end();
        check(brutality.b.player.strength==2,"Brutality must trigger Rupture");
    }
    Fixture order; order.play(CardId::BRUTALITY);
    order.b.player.applyStartOfTurnPostDrawPowers(order.b);
    auto first=order.b.actionQueue.popFront(); first(order.b);
    check(order.b.cards.cardsInHand==1&&order.b.player.curHp==70,"Brutality must draw before losing HP");
    order.execute(); check(order.b.player.curHp==69,"Brutality loss after draw");
    for(auto id:{CardId::BRUTALITY,CardId::COMBUST}) {
        Fixture rod(false,RelicId::TUNGSTEN_ROD); rod.b.player.buff<PS::RUPTURE>(2);
        rod.play(id); rod.end();
        check(rod.b.player.curHp==70&&rod.b.player.strength==0,"prevented HP loss must not trigger Rupture");
    }
    Fixture ice(false,RelicId::ICE_CREAM); ice.play(CardId::BERSERK); ice.end();
    check(ice.b.player.energy==24,"Berserk and Ice Cream recharge");
    Fixture stacked; stacked.play(CardId::BRUTALITY); stacked.play(CardId::BRUTALITY,true);
    stacked.b.player.buff<PS::RUPTURE>(3); stacked.end();
    check(stacked.b.cards.cardsInHand==7&&stacked.b.player.curHp==68&&stacked.b.player.strength==3,"stacked Brutality draw/loss/callback");
    Fixture repeated; repeated.play(CardId::DEMON_FORM); repeated.end();
    repeated.b.player.applyStartOfTurnPostDrawPowers(repeated.b); repeated.execute();
    check(repeated.b.player.strength==4,"Demon Form recurring callback");
    Fixture lethal; lethal.play(CardId::COMBUST); lethal.b.player.curHp=1; lethal.end();
    check(lethal.b.player.curHp==0&&lethal.b.monsters.arr[0].curHp==200,"lethal Combust interrupts damage");
    Fixture noDraw; noDraw.play(CardId::BRUTALITY); noDraw.b.player.debuff<PS::NO_DRAW>(1,false);
    noDraw.b.player.applyStartOfTurnPostDrawPowers(noDraw.b); noDraw.execute();
    check(noDraw.b.cards.cardsInHand==0&&noDraw.b.player.curHp==69,"No Draw does not cancel Brutality loss");
}
}
int main() {
    variants(); interactions();
    std::cout<<"TURN_TRIGGER_CARD_RECIPES_"<<(failures?"FAILED":"OK")<<" failures="<<failures<<'\n';
    return failures?1:0;
}
