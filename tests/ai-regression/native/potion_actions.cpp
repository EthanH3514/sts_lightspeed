// Original desktop-1.0.jar: BloodPotion, SmokeBomb, AbstractRoom.endBattle,
// AbstractPlayer.updateEscapeAnimation and onVictory. No RNG assumptions.
#define main bombFoundationMain
#include "the_bomb_foundation.cpp"
#undef main

void give(Fixture &f, Potion p) {
    f.b.potions.fill(Potion::EMPTY_POTION_SLOT);
    f.b.potions[0]=p;f.b.potionCount=1;f.b.potionCapacity=3;
    f.b.inputState=InputState::PLAYER_NORMAL;
}
int main() {
    for(bool bark:{false,true}) {
        Fixture f;give(f,Potion::BLOOD_POTION);f.b.player.curHp=20;
        if(bark)f.b.player.setHasRelic<R::SACRED_BARK>(true);
        f.b.drinkPotion(0,0);f.run();
        check(f.b.player.curHp==(bark?52:36),"Blood Potion heals 20 percent, doubled by Sacred Bark");
        check(f.b.potionCount==0,"manual use consumes exactly one potion");
    }
    Fixture escape;give(escape,Potion::SMOKE_BOMB);escape.b.player.curHp=20;
    escape.game.relics.add({R::BURNING_BLOOD});escape.game.relics.add({R::MEAT_ON_THE_BONE});
    check(escape.b.canUsePotion(0),"ordinary encounter can be escaped");
    escape.b.drinkPotion(0,0);escape.run();
    check(escape.b.outcome==Outcome::PLAYER_ESCAPED,"escape is distinct from enemy victory");
    check(escape.b.monsters.arr[0].curHp==1000,"escaping does not kill enemies");
    escape.b.exitBattle(escape.game);
    check(escape.game.screenState==ScreenState::MAP_SCREEN,"escape returns without encounter rewards");
    check(escape.game.curHp==38,"escape still triggers Meat on the Bone and Burning Blood");
    check(escape.game.info.stolenGold==0,"escape does not recover enemies' stolen gold");
    for(auto encounter:{MonsterEncounter::HEXAGHOST,MonsterEncounter::SHIELD_AND_SPEAR}) {
        Fixture f;give(f,Potion::SMOKE_BOMB);f.b.encounter=encounter;
        if(encounter==MonsterEncounter::SHIELD_AND_SPEAR)f.b.player.buff<PS::SURROUNDED>();
        check(!f.b.canUsePotion(0),"boss and back-attack encounters cannot be escaped");
        bool refused=false;try{f.b.drinkPotion(0,0);}catch(const std::invalid_argument &){refused=true;}
        check(refused&&f.b.potionCount==1&&f.b.potions[0]==Potion::SMOKE_BOMB,
              "invalid escape is rejected before consuming its potion");
        if(encounter==MonsterEncounter::SHIELD_AND_SPEAR) {
            f.b.player.removeStatus<PS::SURROUNDED>();
            check(f.b.canUsePotion(0),"escape becomes legal after the pair's back-attack power is removed");
        }
    }
    Fixture fairy;give(fairy,Potion::FAIRY_POTION);
    check(!fairy.b.canUsePotion(0),"Fairy is passive");
    bool refused=false;try{fairy.b.drinkPotion(0,0);}catch(const std::invalid_argument &){refused=true;}
    check(refused&&fairy.b.potionCount==1,"manual Fairy use does not consume it");
    std::cout<<"POTION_ACTIONS "<<failures<<" failures / "<<checks<<" checks\n";
    return failures?1:0;
}
