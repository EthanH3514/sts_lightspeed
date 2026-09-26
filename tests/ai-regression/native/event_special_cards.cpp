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
int checks=0;
void check(bool ok,const char *message) { ++checks; if(!ok) throw std::runtime_error(message); }
struct Fixture {
    GameContext game;
    BattleContext b;
    Fixture(RelicId relic=RelicId::INVALID):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(relic!=RelicId::INVALID) game.relics.add({relic});
        game.deck.cards[0]=Card(CardId::RITUAL_DAGGER);
        game.deck.cards[0].misc=15;
        game.enterBattle(MonsterEncounter::CULTIST);
        b.init(game);b.executeActions();b.cards=CardManager{};
        b.cards.nextUniqueCardId=100;b.player.energy=20;
        b.player.maxHp=80;b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=200;
        for(int i=0;i<10;++i) {
            CardInstance c(CardId::DEFEND_RED);c.setUniqueId(b.cards.nextUniqueCardId++);
            b.cards.drawPile.push_back(c);
        }
    }
    int add(CardInstance c,int uid=-1) {
        c.setUniqueId(uid<0?b.cards.nextUniqueCardId++:uid);
        b.cards.hand[b.cards.cardsInHand++]=c;return c.uniqueId;
    }
    void execute(){b.inputState=InputState::EXECUTING_ACTIONS;b.executeActions();}
    void play(int index=0) {
        auto c=b.cards.hand[index];check(c.canUse(b,0,false),"fixture card playable");
        b.addToBotCard(CardQueueItem(c,0,b.player.energy));execute();
    }
    void end(){search::Action(search::ActionType::END_TURN).execute(b);}
};
void apparition() {
    for(bool up:{false,true}) {
        Fixture f;CardInstance card(CardId::APPARITION,up);
        check(card.cost==1&&card.doesExhaust()&&card.isEthereal()==!up,"Apparition traits");
        f.add(card);f.play();
        check(f.b.player.energy==19&&f.b.player.getStatus<PS::INTANGIBLE>()==1,"Apparition effect/cost");
        check(f.b.cards.exhaustPile.size()==1,"played Apparition exhausts");
        for(int damage:{0,1,9,30}) {
            const auto before=f.b.player.curHp;
            f.b.addToBot(Actions::AttackPlayer(0,f.b.monsters.arr[0].calculateDamageToPlayer(f.b,damage)));f.execute();
            check(f.b.player.curHp==before-(damage>0?1:0),"Intangible per attack packet including zero");
        }
        f.b.player.block=1;auto hp=f.b.player.curHp;
        f.b.addToBot(Actions::AttackPlayer(0,f.b.monsters.arr[0].calculateDamageToPlayer(f.b,30)));f.execute();
        check(f.b.player.curHp==hp&&f.b.player.block==0,"Intangible before block");
        f.add(card);f.play();
        check(f.b.player.getStatus<PS::INTANGIBLE>()==2,"Intangible stacks");
        f.end();check(f.b.player.getStatus<PS::INTANGIBLE>()==1,"Intangible first round decrement");
        f.end();check(!f.b.player.hasStatus<PS::INTANGIBLE>(),"Intangible second round expires");

        Fixture unused;unused.add(card);unused.b.player.buff<PS::FEEL_NO_PAIN>(3);unused.end();
        check(unused.b.cards.exhaustPile.size()==(up?0:1),"base-only Ethereal on end hand");
    }
    for(auto relic:{RelicId::INVALID,RelicId::TUNGSTEN_ROD}) {
        Fixture f(relic);f.add(CardInstance(CardId::APPARITION));f.play();
        f.b.player.buff<PS::RUPTURE>(2);
        f.b.addToBot(Actions::PlayerLoseHp(6,true));f.execute();
        check(f.b.player.curHp==(relic==RelicId::INVALID?69:70),"Intangible self HP loss/Rod");
        check(f.b.player.getStatus<PS::STRENGTH>()==(relic==RelicId::INVALID?2:0),"Rupture after actual loss");
        f.b.player.buff<PS::BUFFER>(1);auto hp=f.b.player.curHp;
        f.b.addToBot(Actions::AttackPlayer(0,f.b.monsters.arr[0].calculateDamageToPlayer(f.b,30)));f.execute();
        check(f.b.player.curHp==hp&&!f.b.player.hasStatus<PS::BUFFER>(),"Intangible and Buffer");
    }
}
void dagger() {
    for(bool up:{false,true})for(bool generated:{false,true})for(bool fatal:{false,true})
    for(bool minion:{false,true}) {
        Fixture f;CardInstance c(CardId::RITUAL_DAGGER,up);c.specialData=15;
        int uid=f.add(c,generated?100:0);
        f.b.monsters.arr[0].curHp=fatal?1:200;
        if(minion)f.b.monsters.arr[0].buff<MS::MINION>();
        f.play();f.b.updateCardsOnExit(f.game.deck);
        const int expected=15+(fatal&&!minion?(up?5:3):0);
        check(f.b.cards.exhaustPile.size()==1,"Dagger exhausts even last kill");
        check(f.b.cards.exhaustPile[0].uniqueId==uid&&f.b.cards.exhaustPile[0].specialData==expected,"Dagger combat identity/growth");
        check(f.game.deck.cards[0].misc==(generated?15:expected),"Dagger master identity/growth");
        check(f.b.player.energy==19,"Dagger cost");
    }
    for(bool up:{false,true}) {
        Fixture f;CardInstance c(CardId::RITUAL_DAGGER,up);c.specialData=37;f.add(c,0);
        f.b.monsters.arr[0].buff<MS::THORNS>(3);
        f.play();check(f.b.monsters.arr[0].curHp==163,"Dagger mutable base damage");
        check(f.b.player.curHp==67,"Dagger Thorns callback");
        Fixture angry;angry.add(c,0);angry.b.monsters.arr[0].buff<MS::ANGRY>(2);angry.play();
        check(angry.b.monsters.arr[0].getStatus<MS::STRENGTH>()==2,"Dagger Angry callback");
        Fixture blocked;blocked.add(c,0);blocked.b.monsters.arr[0].curHp=1;blocked.b.monsters.arr[0].block=999;
        blocked.play();check(blocked.b.cards.exhaustPile[0].specialData==37,"blocked Dagger has no fatal reward");
    }
}
void sampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::APPARITION,CardId::RITUAL_DAGGER})for(bool up:{false,true})
    for(bool frozen:{false,true}) {
        Fixture f(frozen?RelicId::FROZEN_EYE:RelicId::INVALID);
        CardInstance c(id,up);if(id==CardId::RITUAL_DAGGER)c.specialData=37;
        f.add(c);auto twin=f.b;const auto original=f.b.cards.drawPile;
        if(!frozen)std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(size_t i=0;i<original.size();++i) {
            check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"sampled hidden order invariant");
            if(frozen)check(f.b.cards.drawPile[i].uniqueId==original[i].uniqueId,"Frozen Eye preserved");
        }
        f.play();public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.cards.exhaustPile.size()==1,"post-play sampled exhaustion preserved");
    }
}
}
int main(){try{apparition();dagger();sampling();std::cout<<"EVENT_SPECIAL_CARDS_OK ("<<checks<<" checks)\n";}
catch(const std::exception &e){std::cerr<<"EVENT_SPECIAL_CARDS_FAILED at "<<checks<<": "<<e.what()<<'\n';return 1;}}
