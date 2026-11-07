#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include "combat/BattleContext.h"
#include "game/GameContext.h"
#include "sim/PublicCombatResampling.h"

using namespace sts;
namespace {
void require(bool v, const char *message) { if (!v) throw std::runtime_error(message); }
struct Fixture {
    GameContext game;
    BattleContext battle;
    Fixture(CardId id, bool up, int energy=3, bool chemical=false, bool frozen=false,
            bool drill=false) : game(CharacterClass::IRONCLAD, 123456789, 0) {
        game.floorNum = 1;
        game.relics = {};
        if (chemical) game.relics.add({RelicId::CHEMICAL_X});
        if (frozen) game.relics.add({RelicId::FROZEN_EYE});
        if (drill) game.relics.add({RelicId::HAND_DRILL});
        game.enterBattle(MonsterEncounter::SMALL_SLIMES);
        battle.init(game);
        battle.executeActions();
        battle.cards = CardManager{};
        battle.player.curHp = 60;
        battle.player.block = 0;
        battle.player.energy = energy;
        for (int i=0; i<2; ++i) {
            battle.monsters.arr[i].curHp = battle.monsters.arr[i].maxHp = 200;
            battle.monsters.arr[i].block = 0;
        }
        CardInstance card(id, up); card.setUniqueId(100);
        battle.cards.hand[battle.cards.cardsInHand++] = card;
        int uid=101;
        for (auto id : {CardId::STRIKE_RED, CardId::DEFEND_RED, CardId::BASH}) {
            CardInstance c(id); c.setUniqueId(uid++); battle.cards.drawPile.push_back(c);
        }
        battle.cards.nextUniqueCardId = uid;
    }
};
void play(BattleContext &b, bool free=false) {
    b.cards.hand[0].freeToPlayOnce = free;
    auto c=b.cards.hand[0];
    require(c.canUse(b, 0, false), "not playable");
    b.addToBotCard(CardQueueItem(c, 0, b.player.energy));
    b.inputState=InputState::EXECUTING_ACTIONS; b.executeActions();
}
std::vector<int> pile(const BattleContext &b) {
    std::vector<int> r; for (auto c:b.cards.drawPile) r.push_back(c.uniqueId); return r;
}
void whirlwind() {
    for (bool up:{false,true}) for (int energy:{0,1,3})
    for (bool chemical:{false,true}) for (bool free:{false,true}) {
        Fixture f(CardId::WHIRLWIND,up,energy,chemical);
        auto &b=f.battle;
        b.player.buff<PS::STRENGTH>(2);
        b.player.debuff<PS::WEAK>(1);
        b.monsters.arr[0].block=4;
        b.monsters.arr[1].addDebuff<MS::VULNERABLE>(1,false);
        const double base=((up?8:5)+2)*.75;
        const int count=energy+(chemical?2:0);
        play(b,free);
        require(b.monsters.arr[0].curHp==200-std::max(0,int(base)*count-4), "X hit/block mismatch");
        require(b.monsters.arr[1].curHp==200-int(base*1.5)*count, "X target damage matrix mismatch");
        require(b.player.energy==(free?energy:0), "X/free payment mismatch");
        require(b.cards.cardsInHand==0 && b.cards.discardPile.size()==1 &&
                b.cards.discardPile[0].uniqueId==100, "X lifecycle mismatch");
    }
}
void samplingAndRandom() {
    std::set<std::pair<int,int>> outcomes;
    for (auto id:{CardId::SWORD_BOOMERANG,CardId::WHIRLWIND})
    for (bool up:{false,true}) for (bool frozen:{false,true}) for (int seed=0;seed<32;++seed) {
        Fixture f(id,up,3,false,frozen);
        auto original=f.battle;
        auto twin=f.battle;
        if (!frozen) std::reverse(twin.cards.drawPile.begin(),twin.cards.drawPile.end());
        // Changing the source hidden RNG must not affect a common sampled world.
        twin.cardRandomRng=Random(987654321);
        std::array<std::uint64_t,7> seeds{11,static_cast<std::uint64_t>(seed),33,44,55,66,77};
        public_sampling::resampleCombatContinuation(f.game,f.battle,seeds);
        public_sampling::resampleCombatContinuation(f.game,twin,seeds);
        require(pile(f.battle)==pile(twin), "hidden pile order leaked");
        play(f.battle); play(twin);
        for (int i=0;i<2;++i) require(f.battle.monsters.arr[i].curHp==twin.monsters.arr[i].curHp,
                                     "source RNG leaked into sampled targets");
        if (id==CardId::SWORD_BOOMERANG) {
            int a=200-f.battle.monsters.arr[0].curHp, b=200-f.battle.monsters.arr[1].curHp;
            require(a+b==3*(up?4:3) && a%3==0 && b%3==0, "random sequence total mismatch");
            outcomes.emplace(a,b);
        }
        public_sampling::resampleCombatContinuation(f.game,f.battle,seeds);
        require(f.battle.cards.cardsInHand==0, "post-play sample changed hand");
        if (frozen) require(pile(f.battle)==pile(original), "Frozen Eye order changed");
        require(original.monsters.arr[0].curHp==200 && original.cards.cardsInHand==1,
                "source state mutated");
    }
    require(outcomes.size()>4, "different sampled seeds never varied random targets");
}
void lethalAndRecalculation() {
    for (bool up:{false,true}) {
        // Only one enemy survives after the first hit: remaining random hits must retarget.
        int retargeted=0;
        for (int seed=0;seed<32;++seed) {
            Fixture f(CardId::SWORD_BOOMERANG,up);
            f.battle.monsters.arr[0].curHp=1;
            f.battle.monsters.arr[1].addDebuff<MS::VULNERABLE>(1,false);
            f.battle.cardRandomRng=Random(seed);
            play(f.battle);
            if (f.battle.monsters.arr[0].curHp<=0) {
                ++retargeted;
                require(f.battle.monsters.arr[1].curHp==200-4*((up?4:3)-1), "dead target took later hits");
            } else require(f.battle.monsters.arr[1].curHp==200-4*(up?4:3), "target modifier/hit lost");
        }
        require(retargeted>0, "retarget fixture never killed its first enemy");
        for (auto id:{CardId::SWORD_BOOMERANG,CardId::WHIRLWIND}) {
            Fixture kill(id,up);
            kill.battle.monsters.arr[0].curHp=kill.battle.monsters.arr[1].curHp=1;
            play(kill.battle);
            require(kill.battle.outcome==Outcome::PLAYER_VICTORY, "kill did not terminate");
            Fixture drill(id,up,3,false,false,true);
            // Hand Drill queues Vulnerable at the BOTTOM, after all queued hits.
            // Recalculation alone must not apply a power that has not executed yet.
            for (int i=0;i<2;++i) drill.battle.monsters.arr[i].block=1;
            play(drill.battle);
            int loss=400-drill.battle.monsters.arr[0].curHp-drill.battle.monsters.arr[1].curHp;
            if (id==CardId::WHIRLWIND) require(loss==2*((up?8:5)*3-1), "X damage was recalculated");
            else {
                int touched=0; for(int i=0;i<2;++i) if(drill.battle.monsters.arr[i].block==0) ++touched;
                require(loss==3*(up?4:3)-touched, "queued Hand Drill applied too early");
            }
        }
    }
}
}
int main() {
    try { whirlwind(); samplingAndRandom(); lethalAndRecalculation();
        std::cout << "RANDOM_X_ATTACK_RECIPES_OK (2 cards, 4 variants, 24 X cases, 256 paired worlds)\n";
    } catch(const std::exception &e) { std::cerr << "RANDOM_X_ATTACK_RECIPES_FAILED: " << e.what() << '\n'; return 1; }
}
