# Colosseum event completion

The previously disabled event had a Fight action that immediately returned to the
map. It now spawns after map row7 and follows the two-combat lifecycle:

1. Fight the blue/red slavers. No selectable combat rewards follow this fight.
2. Return to the event and choose Leave or the taskmaster/gremlin-nob encounter.
3. Generate rare relic, uncommon relic and100gold before the second combat;
   offer them with ordinary event card/potion rewards after victory.

The existing initial Fight command continues to combine the original game's
cosmetic Enter/Fight confirmations. No resource or random operation is skipped.
The empty post-second-fight leave dialog is represented by returning to the map
after the reward screen. No card-selection or resource decision is collapsed.

Verified rules from the original game's Colosseum, EventRoom, AbstractRoom,
RewardItem, AbstractDungeon, AbstractPlayer, PreservedInsect, SlaversCollar,
Sling, and BlackStar bytecode:

- The first fight suppresses the reward screen, but AbstractRoom still performs
  its potion drop calculation. Consume that roll and chance adjustment without
  exposing a reward or generating a card offer.
- Second-fight relics are removed from their pools before combat, in rare then
  uncommon order. Golden Idol modifies the100gold to125. Pending rewards are
  retained in the copied game state, not in references to another branch.
- The second fight sets the equivalent of eliteTrigger. Preserved Insect,
  Sling of Courage and Slaver's Collar apply; room identity remains EVENT.
  Black Star's extra elite-room relic and emerald key do not apply. Card reward
  rarity uses event-room rules. The public menu does not expose pending relics.
- The first fight's victory or Smoke Bomb escape reopens the event. A second
  escape exits without rewards; death ends the run in either fight. Face of
  Cleric is settled through the normal survived-battle callback.
- The second fight is on the same floor. Continue the AI, monster HP, shuffle
  and card-random streams saved by exitBattle instead of reinitializing them.
  There is no new RNG field in a public policy observation.

`tests/ai-regression/native/colosseum_event.cpp` exercises A0/A10/A20, spawn
eligibility, both encounters, no first reward, the suppressed potion roll,
same-floor stream continuation, exact branch isolation, reward ordering,
Golden Idol/Black Star/White Beast Statue, event elite relics, nested bottled
relic selection, actual Smoke Bomb use, death and the legacy skipBattles path.
Combat outcomes in these fixtures are controlled to test boundaries, not to
measure a strategy's playing strength.

The nested bottled-relic case also reproduced a shared reward bug: completing
the selector returned straight to the map and bypassed all remaining rewards.
ConsoleSimulator and GameAction now use GameContext::obtainRewardRelic, which
removes the claimed item once and restores the remaining rewards and parent
callback when the child screen closes. Captured values keep cloned games
independent. The additional reward_relic_continuation fixture covers a nested
Cauldron reward, sapphire-key linkage and separately replayed cloned callbacks.

Build with STS_BUILD_AI_REGRESSIONS=ON and run `ctest -R ai.colosseum_event`.
The simulator implementation makes no choice of event or combat policy. The
consuming AI continues to exclude the entire Colosseum from event rewind/search.
