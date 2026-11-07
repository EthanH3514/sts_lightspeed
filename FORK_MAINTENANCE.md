# Maintained simulator fork

Canonical repository: https://github.com/EthanH3514/sts_lightspeed

`master` is the only maintained long-lived branch. Future simulator corrections
are submitted as pull requests to this repository's master, tested, merged,
and their temporary branches deleted. Maintenance does not depend on the original
upstream repository or its PR decisions. Attribution and licensing remain intact.
Do not rebase or force-push published commits pinned by consumers.

## Build and regressions

The fork builds independently of the consuming AI project. Initialize the pinned
json and pybind11 gitlinks without `--remote`:

```sh
git clone --branch master --recurse-submodules https://github.com/EthanH3514/sts_lightspeed.git
cd sts_lightspeed
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTS_BUILD_AI_REGRESSIONS=ON
cmake --build build --target slaythespire ai-regressions -j 4
ctest --test-dir build --output-on-failure -j 4
python tests/ai-regression/session_smoke.py build
```

The suite includes historical card/power/encounter corrections and newer event,
shop, reward, ascension and public-state cases. Scoped evidence is not certification
of every interaction. MinGW test executables receive their compiler's runtime DLLs.
Generated outputs remain ignored.
On Windows, copy the matching MinGW runtime DLLs beside the Python module before
the Python smoke test; the CTest executable runtime deployment is automatic.

Public map, deck/bottle supplement and shop shelf exports are standalone simulator
interfaces. Raw simulator state contains audit information; consumers must keep
hidden RNG exclusively in their audit channel, never normal policy inputs.

## Optional AI integration

Search policies, neural models, draw-belief algorithms and training code belong
to the consuming AI project. They are not copied into this fork. To enable the
optional integration, supply an explicit compatible source snapshot:

```sh
cmake -S . -B build-ai -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSPIRE_AI_SEARCH_SOURCE_ROOT=/path/to/compatible/native \
  -DSPIRE_AI_SESSION_SEARCH=ON
cmake --build build-ai --target slaythespire -j 4
```

The session option enables existing session bindings and draw-event hooks and
requires the external source path. Test-only fixture entry points are OFF by
default. Standard simulator builds need no AI source directory.

Consumers pin exact commits and validate adapter/catalogue contracts before
switching. Publishing a new master does not hot-swap a running training job's
frozen simulator, model or search budget. Historical consumers keep their sealed
checkouts and archived commits until an explicitly validated migration.

## Consolidation provenance

The 2026-11-07 consolidation publishes one current source snapshot on master,
incorporating the published integration, 23 later local commits through
`948e37bd936d68f92706e5607a778597d27ec069`, and reviewed runtime corrections.
It does not replay every historical release. Old branch tips and their complete
history were preserved in a verified local Git bundle before branch deletion.
This one-time consolidation was authorized to use an SSH fast-forward because
PR API access was unavailable. Future corrections still follow the PR workflow.
Their behavior remains present or is replaced by the general power-instance
implementation. See `docs/consolidation-20261107.md` for the change inventory.

No game JARs, decompiled game sources, model weights, raw trajectories, captured
RNG states, build products or credentials are included. First-party regression
sources retain their MIT license in `tests/ai-regression/LICENSE`.
