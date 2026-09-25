# Autoplay X-cost regression

Autoplayed X-cost cards must retain their queued `energyOnUse` for their effect,
without consuming current energy. The original game's `AbstractPlayer.useCard`
marks X-cost cards with `isInAutoplay` as `freeToPlayOnce` before calling `card.use`.

The focused interaction starts with Double Tap, Whirlwind and Nunchaku at counter
9, with 4 energy. Double Tap leaves 3 energy. The manual Whirlwind spends those 3,
then triggers Nunchaku for 1 energy. Its replay must still execute X=3, but leave
that new energy untouched. Previously it spent the new energy, leaving 0.

The test constructs public combat states, not a recorded full game. It covers
base/upgraded Whirlwind, zero/nonzero X, queued X different from current energy,
Nunchaku counters 8 and 9, normal manual energy consumption, and fixed-cost
manual/autoplay controls. Checks remain active in release builds.

## Standalone upstream build

This project's fixture is `tests/native/upstream_autoplay_x_cost.cpp`. In the
independent upstream branch it is copied verbatim to `tests/autoplay_x_cost.cpp`.
These commands run from that upstream checkout, not the first-party project root.
Initialize the pinned JSON submodule, then compile all simulator sources without
Python bindings or other pending patches:

```sh
git submodule update --init json
c++ -std=c++17 -O2 -Wno-shift-count-overflow -Iinclude -Ijson/single_include \
  tests/autoplay_x_cost.cpp $(find src -name '*.cpp') -o autoplay_x_cost
./autoplay_x_cost
```

On Windows, MinGW can statically link compiler runtimes to avoid DLL dependencies:

```powershell
$coreSources = (Get-ChildItem src -Recurse -Filter *.cpp).FullName
c++ -std=c++17 -O2 -Wno-shift-count-overflow -static -static-libgcc -static-libstdc++ `
  -Iinclude -Ijson/single_include tests/autoplay_x_cost.cpp @coreSources `
  -o autoplay_x_cost.exe
./autoplay_x_cost.exe
```

Results against upstream `7476a81954020087da31d41d16fddf475746ec2d`:

- Before: `AUTOPLAY_X_COST_FAILED (8 failures / 56 checks)`; exit 1.
- With only the autoplay-X correction: `AUTOPLAY_X_COST_OK (0 failures / 56 checks)`;
  exit 0.

JSON headers are from the pinned submodule revision
`0b345b20c888f7dc8888485768e4bf9a6be29de0`. Existing upstream shift-count warnings
are outside this correction; the flag above matches the upstream CMake suppression.
