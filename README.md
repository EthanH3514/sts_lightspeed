# sts_lightspeed

This is a maintained fork of `sts_lightspeed`, a C++ simulator for *Slay the
Spire*. It runs without the game or Steam. The `slaythespire` Python module
exposes a simulator session for starting runs, reading visible game state,
enumerating legal actions and advancing the game. This fork supplies the
simulator used by Spire AI. [Spire AI](https://github.com/EthanH3514/slay_the_spire_ai)

## Build the Python module

Use a C++17 compiler, CMake, Ninja and a Python installation with development
headers. The Windows setup uses MinGW-w64 and Python 3.11.

```sh
git clone --recurse-submodules https://github.com/EthanH3514/sts_lightspeed.git
cd sts_lightspeed
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The default build produces the Python extension. On Windows, make the
matching MinGW runtime DLLs available when importing it. Build the console
with `cmake --build build --target main`, or request the `legacy-test` target
for older tools such as action replay and save-file inspection.

## Scope and testing

The simulator covers Ironclad runs and exposes combat, map, reward, shop and
event decisions. The regression suite covers specific mechanics and public
interfaces. Game accuracy and mechanic coverage remain work in progress.
For the regression commands and the optional Spire AI integration, see the
[maintenance guide](FORK_MAINTENANCE.md).

This fork is maintained on `master`. The correction inventory describes the
changes consolidated into the fork. [Correction inventory](docs/consolidation-20261107.md) ·
[MIT license](LICENSE.md)
