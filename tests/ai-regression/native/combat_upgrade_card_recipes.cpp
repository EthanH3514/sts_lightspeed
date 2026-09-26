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
    explicit Fixture(bool frozen=false):game(CharacterClass::IRONCLAD,123456789,0) {
        game.floorNum=1; game.relics={};
        if(frozen) game.relics.add({RelicId::FROZEN_EYE});
        game.enterBattle(MonsterEncounter::CULTIST); b.init(game); b.executeActions();
        b.cards=CardManager{}; b.cards.nextUniqueCardId=100; b.player.energy=20;
        b.player.maxHp=80; b.player.curHp=70;
        b.monsters.arr[0].curHp=b.monsters.arr[0].maxHp=1000;
        for(int i=0;i<10;++i) b.cards.createTempCardInDrawPile(i,CardInstance(i%2?CardId::STRIKE_RED:CardId::DEFEND_RED));
    }
    void add(CardId id,bool up=false) {b.cards.createTempCardInHand(CardInstance(id,up));}
    void drain() {b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();}
    void play(CardId id,bool up=false) {
        add(id,up); auto c=b.cards.hand[b.cards.cardsInHand-1];
        b.addToBotCard(CardQueueItem(c,0,b.player.energy)); drain();
    }
};
void selection() {
    for(int count:{0,1,2}) {
        Fixture f; f.add(CardId::WOUND); f.add(CardId::BURN); f.add(CardId::REGRET);
        f.add(CardId::DEFEND_RED,true);
        for(int i=0;i<count;++i) f.add(CardId::STRIKE_RED);
        f.play(CardId::ARMAMENTS);
        check(f.b.player.block==5&&f.b.player.energy==19,"Armaments block/payment before selection");
        check((f.b.inputState==InputState::CARD_SELECT)==(count>1),"zero/one auto resolves; multiple opens selection");
        if(count>1) {
            check(f.b.cardSelectInfo.cardSelectTask==CardSelectTask::ARMAMENTS,"upgrade select task");
            check(!search::Action(search::ActionType::SINGLE_CARD_SELECT,0).isValidAction(f.b),"cannot select a status");
            const int uid=f.b.cards.hand[4].uniqueId;
            search::Action(search::ActionType::SINGLE_CARD_SELECT,4).execute(f.b);
            check(f.b.inputState==InputState::PLAYER_NORMAL,"selection resumes queue");
            bool found=false;
            for(int i=0;i<f.b.cards.cardsInHand;++i) if(f.b.cards.hand[i].uniqueId==uid)
                found=f.b.cards.hand[i].isUpgraded();
            check(found,"selected identity upgraded despite hand reordering");
        }
        int upgraded=0;
        for(int i=0;i<f.b.cards.cardsInHand;++i) {
            const auto &c=f.b.cards.hand[i];
            if(c.id==CardId::STRIKE_RED&&c.isUpgraded()) ++upgraded;
            if(c.getType()==CardType::STATUS||c.getType()==CardType::CURSE)
                check(!c.isUpgraded(),"unupgradeable cards unchanged");
        }
        check(upgraded==(count?1:0),"exactly one eligible upgrade");
        check(f.b.cards.discardPile.size()==1&&f.b.cards.discardPile[0].id==CardId::ARMAMENTS,
              "played Armaments discards after selection");
    }
}
void scopesAndSampling() {
    const std::array<std::uint64_t,7> seeds{11,22,33,44,55,66,77};
    for(auto id:{CardId::ARMAMENTS,CardId::APOTHEOSIS}) for(bool up:{false,true})
    for(bool frozen:{false,true}) {
        Fixture f(frozen); f.add(CardId::STRIKE_RED); f.add(CardId::DEFEND_RED,true);
        f.add(CardId::BURN); f.add(CardId::REGRET);
        CardInstance searing(CardId::SEARING_BLOW); searing.upgrade(); searing.upgrade();
        f.b.cards.createTempCardInDiscard(searing);
        CardInstance exhausted(CardId::BASH); exhausted.setUniqueId(f.b.cards.nextUniqueCardId++);
        f.b.cards.exhaustPile.push_back(exhausted);
        const auto deckSize=f.game.deck.size();
        auto twin=f.b; twin.cardRandomRng=Random(98765);
        if(!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==twin.cards.drawPile[i].uniqueId,"hidden-order independence");
        const auto before=f.b.cards.drawPile;
        f.play(id,up);
        check(f.b.inputState==InputState::PLAYER_NORMAL,"single eligible hand card auto resolves");
        check(f.b.cards.hand[0].isUpgraded(),"hand upgraded");
        check(!f.b.cards.hand[2].isUpgraded()&&!f.b.cards.hand[3].isUpgraded(),"Burn/curse cannot upgrade");
        for(int i=0;i<10;++i) {
            check(f.b.cards.drawPile[i].isUpgraded()==(id==CardId::APOTHEOSIS),"draw-pile scope");
            check(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"upgrade preserves pile order/identity");
        }
        check(f.b.cards.discardPile[0].getUpgradeCount()==(id==CardId::APOTHEOSIS?3:2),"repeatable Searing Blow upgrades once per action");
        check(f.b.cards.exhaustPile[0].isUpgraded()==(id==CardId::APOTHEOSIS),"exhaust-pile scope");
        check(f.b.player.energy==20-(id==CardId::APOTHEOSIS?(up?1:2):1),"four variant costs");
        check(f.b.cards.exhaustPile.size()==(id==CardId::APOTHEOSIS?2:1),"Apotheosis exhausts");
        if(id==CardId::APOTHEOSIS) check(f.b.cards.exhaustPile.back().isUpgraded()==up,"card in play not upgraded by itself");
        check(f.game.deck.size()==deckSize,"master deck unchanged size");
        for(int i=0;i<f.game.deck.size();++i) check(!f.game.deck.cards[i].isUpgraded(),"combat upgrade not permanent");
        public_sampling::resampleCombatContinuation(f.game,f.b,seeds);
        check(f.b.cards.hand[0].isUpgraded(),"sample preserves upgrades");
        if(frozen) for(int i=0;i<10;++i) check(f.b.cards.drawPile[i].uniqueId==before[i].uniqueId,"Frozen Eye order retained");
    }
}
void costs() {
    for(auto id:{CardId::APOTHEOSIS,CardId::DARK_EMBRACE,CardId::BODY_SLAM,CardId::ENTRENCH})
    for(int cost:{0,1,2,3}) for(int turn:{0,1,3}) {
        CardInstance c(id); c.cost=cost; c.costForTurn=turn; c.setUniqueId(222);
        const int target=getEnergyCost(id,true);
        c.upgrade();
        check(c.cost==target&&c.costForTurn==(turn>0?std::max(0,target+turn-cost):0),"generic upgrade preserves turn-cost difference/zero");
        check(c.uniqueId==222&&c.isUpgraded(),"upgrade keeps identity");
        c.upgrade(); check(c.cost==target,"ordinary second upgrade is no-op");
    }
    for(auto upgrade:{CardId::ARMAMENTS,CardId::APOTHEOSIS}) for(bool up:{false,true}) {
        Fixture f; f.add(CardId::DARK_EMBRACE); f.b.cards.hand[0].costForTurn=0;
        f.play(upgrade,up);
        check(f.b.cards.hand[0].cost==1&&f.b.cards.hand[0].costForTurn==0,"upgrade action preserves temporary zero cost");
        Fixture g; g.add(CardId::BLOOD_FOR_BLOOD); g.b.cards.hand[0].cost=2; g.b.cards.hand[0].costForTurn=1;
        g.play(upgrade,up);
        check(g.b.cards.hand[0].cost==1&&g.b.cards.hand[0].costForTurn==0,"Blood for Blood discount composes with upgrades");
    }
}
void functionalControls() {
    Fixture burn; burn.add(CardId::BURN); burn.play(CardId::ARMAMENTS,true);
    burn.b.player.block=0; burn.b.monsters.arr[0].setMove(MMID::CULTIST_INCANTATION);
    search::Action(search::ActionType::END_TURN).execute(burn.b);
    check(burn.b.player.curHp==68,"Armaments+ must not turn normal Burn's 2 damage into4");
    for(bool up:{false,true}) {
        Fixture f; f.add(CardId::STRIKE_RED); f.b.player.buff<PS::DEXTERITY>(3);
        f.b.player.debuff<PS::FRAIL>(2,false); f.play(CardId::ARMAMENTS,up);
        check(f.b.player.block==6,"Armaments block applies Dexterity/Frail once");
        auto strike=f.b.cards.hand[0]; const int hp=f.b.monsters.arr[0].curHp;
        f.b.addToBotCard(CardQueueItem(strike,0,f.b.player.energy)); f.drain();
        check(f.b.monsters.arr[0].curHp==hp-9,"upgraded card's subsequent play uses upgraded damage");
        Fixture g; g.add(CardId::SEARING_BLOW); g.play(CardId::ARMAMENTS,up);
        g.play(CardId::ARMAMENTS,up);
        check(g.b.cards.hand[0].getUpgradeCount()==2,"hand repeatable upgrade count increases on both plays");
    }
    Fixture exhaust; exhaust.play(CardId::FEEL_NO_PAIN); exhaust.play(CardId::APOTHEOSIS);
    check(exhaust.b.player.block==3,"Apotheosis exhaustion triggers Feel No Pain");
}
}
int main() {
    selection(); scopesAndSampling(); costs(); functionalControls();
    std::cout<<"COMBAT_UPGRADE_RECIPES_"<<(failures?"FAILED":"OK")<<" ("<<failures<<" failures / "<<checks<<" checks)\n";
    return failures?1:0;
}
