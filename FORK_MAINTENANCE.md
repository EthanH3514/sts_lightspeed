# Maintained AI integration fork

Branch: `EthanH3514_dev/ai-integration`. Upstream base:
`7476a81954020087da31d41d16fddf475746ec2d` from gamerpuppy/sts_lightspeed.

The initial 55 integration commits import the reviewed spire-ai patch stack at
`563476c`, in its original order. Each commit identifies its originating patch.
This migration does not update upstream game logic. Existing independent PR
branches are unaffected. It includes both general fidelity corrections and
AI-specific public-state/continuation interfaces; not every interface is intended
for upstream submission. Scoped support is not universal simulator fidelity.

Consumers pin a commit SHA, never the moving branch head. json and pybind11 are
submodules pinned to the versions used by the previous validated build. Clone
recursively (or initialize submodules) without `--remote`:

```sh
git clone --branch EthanH3514_dev/ai-integration --recurse-submodules https://github.com/EthanH3514/sts_lightspeed.git
cd sts_lightspeed
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTS_BUILD_AI_REGRESSIONS=ON
cmake --build build --target slaythespire ai-regressions -j 8
ctest --test-dir build --output-on-failure -j 4
```

The optional suite contains 69 first-party C++ executable fixtures: 68 imported
from spire-ai 563476c plus the passive-status batch. No game JARs, decompiled game sources, raw trajectories, secrets
or model/RNG captures are included. Test sources are provided under the MIT license
in `tests/ai-regression/LICENSE`. MinGW runtime DLLs are resolved from the selected
compiler and copied beside the test programs; generated binaries remain ignored.
The AI project's static catalogue exporter and live numeric-reference comparator
stay in that project and must also pass before its dependency lock is updated.

`tests/ai-regression/native` exercises the integrated fork. Its `upstream` helpers
retain isolated-fix fixtures used by independent PRs; the Violence wrapper opts
into settled-discard costs for the integrated stack. The fork's tests are now
maintained here. The AI project initially retains a migration snapshot, with
subsequent test changes synchronized deliberately rather than implicitly.

Future work: make mechanism-sized commits with regression tests. Integrate only
reviewed upstream changes, checking for already-fixed overlap. Do not rebase or
force-push commits pinned by consumers. Offer independent general fixes upstream
selectively; do not block the AI project on PR acceptance. Do not expose real
hidden RNG in normal policy observations. Existing explicit limitations (including
non-frame-accurate movement timing and partial power-order coverage) still apply.
