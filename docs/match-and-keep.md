# Match and Keep

The event is enabled. At its playable board the console prints all legal pair
commands as `i j: [Flip] Tiles i and j.`; GameAction enumerates and executes the
same unordered pairs. A pair is one completed two-flip attempt, not an extra
attempt or an early leave. Both matches and misses consume one of five attempts.
Matching IDs obtain one fresh card through normal acquisition callbacks and
remove those tiles. Preview upgrades must not be copied and applied twice.

The public menu never prints unknown identities. SimulatorSession's
`public_match_and_keep()` returns `public-match-and-keep-v1`, with an explicit
availability flag, attempts remaining, and twelve indexed tile records. Unknown
records omit the card field. Revealed or removed records retain the legitimately
observed card ID/upgrades/misc. Outside the active event it returns only version
and `available:false`. Reading the endpoint consumes no RNG and does not reveal
future or unflipped tiles. Exact clones continue to own their event state.

The simulator does not implement a player policy or supply an oracle board.
The consuming AI's separately authorized event-rewind planner can legally reveal
cards on cloned attempts and remember those results; normal policies do not gain
hidden card identities through this endpoint.

Validation: 100 CTest tests pass, including 369 Match and Keep checks across
A0/A10/A20. Coverage includes hidden-board twins, action parity, invalid/removed
tile rejection without mutation, attempt accounting, source isolation, map exit,
Molten Egg + Searing Blow, Omamori and Ceramic Fish. The existing Python session
smoke passes all12cases. Consumers must validate and pin this revision before
adopting it; existing frozen runtimes are unchanged.
