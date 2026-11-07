# Consolidation of maintained simulator corrections

Starting remote master: `7476a81954020087da31d41d16fddf475746ec2d`.
Published integration tip: `b32428a067b5ffdab7d90aaf6b1fbded17135bb6`.
Later local history: through `948e37bd936d68f92706e5607a778597d27ec069`.

## Historical branches

The 31 independent fix branches were checked against the consolidated source.
28 retain their substantive added lines directly (including two removed-only
fixes checked against exact code and regressions). Three use later generalizations:

- Draw-trigger order: the old pairwise Fire Breathing/Evolve boolean is replaced
  by the ordered power-instance registry. `draw_trigger_card_recipes` tests both
  acquisition orders and reacquisition.
- Independent Bomb packets: three old vectors are replaced by independent power
  instances with countdown and damage payload. Each expiring instance queues its
  own packet. Original packet, Bomb, public-state and order regressions remain.
- Panache counter: initial and post-trigger reset to five remain; repeated
  per-card reset sites are consolidated into the ordered on-card-play callback.
  The original Panache regression remains.

The removed-only Enlightenment and Violence fixes respectively preserve temporary
cost independently of permanent cost and remove transferred attacks from the draw
pile even when fewer attacks exist than requested. Original regressions remain.

This is one current-version snapshot commit based on master. For this one-time
consolidation, the maintainer explicitly approved an SSH fast-forward after the
GitHub PR API denied write access; no PR was created. Future corrections still
use the PR workflow in `FORK_MAINTENANCE.md`. Old branch tips and
their complete history were saved in a verified local Git bundle before deleting
branch names; they are not all replayed into the new master's ancestry.
No original-upstream PR state needs changing for this consolidation.

## Historical correction inventory

These are the substantive corrections from the 31 independent fix branches.
The final implementation includes later generalizations, so the old patch text
is not always the current implementation.

| Mechanism | Corrections retained in the current version |
| --- | --- |
| Block | Iron Wave applies Dexterity/Frail in the proper order; player block is capped at 999, including Entrench. |
| Card metadata | Correct eight color classifications; Rage and Trip cost zero both before and after upgrade. |
| Costs and upgrades | Blood for Blood retains accumulated discounts on upgrade; generated copies inherit prior HP-loss discounts; generic upgrades and Enlightenment preserve temporary costs; upgrading the whole hand filters non-upgradable cards. |
| Strength | Disarm+ removes three Strength; Dark Shackles has the correct sign and Artifact/recovery behavior; negative Limit Break respects Artifact; Strength and restoration counters use correct bounds/storage. |
| Damage and exhaust | Hand of Greed uses attack callbacks; Seeing Red exhausts; Sever Soul exhausts before damage; empty multi-exhaust resolves without an invalid selection. |
| Draw and HP-loss callbacks | Brutality draws before self-inflicted loss and triggers Rupture; Fire Breathing/Evolve follow acquisition order; Impatience tests for attacks when its queued effect executes. |
| Automatic play | Autoplayed X-cost cards use the free-play flag; rejected autoplay still performs exhaust/discard lifecycle; Mayhem queues its flip after normal drawing and preserves FIFO card execution. |
| Generated and moved cards | Random generation preserves X costs; Magnetism generates at turn start and prevents premature empty-deck loss; Violence removes transferred cards even when supply is short. |
| Stateful powers and selection | Panache initializes/resets its five-card counter; Forethought+ allows zero-to-many ordered choices; Bomb effects retain independent countdown/damage packets without 8-bit overflow. |
| RNG accounting | Relic-pool shuffle consumption advances the appropriate counters. Hidden RNG is not a policy input. |

## Later corrections and interfaces

Sources were reconstructed from the effective source behind each object in a
sealed tested runtime, not by blindly copying an overlay directory. This retains
repairs whose objects came from separate builds. Included changes cover:

- Growing queues, potion lifecycle, ordered power callbacks, public enemy/draw/
  movement/theft history, random insertion and temporary-cost reset lifecycle.
- Ascension-aware damage, empty emerald slots, bottled-deck detection, Ritual
  Dagger event initialization, Toke selection and Duplicate selector display.
- Mausoleum reward and Designer/Scrap Ooze/Dead Adventurer/Falling/Goop fixes.
- Full/Sozu potion-purchase refusal without charging, and Orrery's five rewards.
  These previously verified isolated shop fixes were absent from the active
  training runtime and are explicitly included here.
- Public deck/bottle and shop-shelf exports moved into standalone simulator headers.

Search/draw hooks preserve their existing implementation when enabled. External
AI dependencies are guarded for standalone builds; AI algorithms and learned
models are not published or replaced by this consolidation.

## Validation scope

Verified on Windows/MinGW GCC 13.2 with CPython 3.11:

- Standalone module builds without any AI source path.
- All 96 CTest regressions pass (13.66 s in the final run).
- Python session smoke: 12 seed/ascension cases pass, checking public exports,
  invalid-ascension rejection, deterministic clone continuation and isolation.
- The optional AI session/draw-hook module also builds against the consuming
  project's sealed compatible native snapshot. It passes the same 12 session
  smoke cases. This is build/API evidence, not a new search-strength benchmark.

Build the standalone Python module and run the complete `ai-regressions` CTest
target. New cases cover A10/A20 event settlement, ascension encounter roots,
blocked purchases, Orrery reward sizes, Toke, Ritual Dagger and empty emerald slots.
Historical scope checks now reject A11 for recipes reviewed through A10, and
continue checking the upper boundary; they no longer incorrectly reject A1.
The hidden-state-twin test permutes only unknown draw positions, retaining public
top/bottom evidence, and checks the current below-top insertion contract.

Synthetic cases establish their asserted mechanics, not natural-state frequency
or playing strength. The consuming AI project separately checks public adapters
and optional session integration before advancing its frozen dependency pin.
