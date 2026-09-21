# JamesDSP reliability and EEL correctness: Luna implementation plan

> **For agentic workers:** Execute this plan task by task using `superpowers:executing-plans` if available. Keep the checkboxes, task board, decisions, and append-only work log current. This document is a handoff for Luna; no implementation task below has been completed by the reviewer.

**Goal:** Remove demonstrated memory-safety, data-loss, Liveprog, and EEL correctness defects, then make those guarantees repeatable in tests and packages.

**Architecture:** Preserve the Qt application, existing PipeWire/PulseAudio backends, native JamesDSP library, and EEL runtime. Repair boundaries and state ownership with focused changes; use offline DSP tests and temporary-file tests before application-level verification. Keep script compatibility and current user changes intact.

**Tech stack:** C/C++, Qt/qmake, PipeWire, GStreamer/PulseAudio, native EEL, libarchive, Bash; Python only for the adjacent EELVault validator.

**Spec:** The user requested a full JamesDSP review including `.eel` files, followed by an ordered, detailed Luna implementation prompt and progress log for roughly one week to one month of valuable work. This file contains the review and resulting requirements. Also read `docs/liveprog-four-stage-spec.md` and the contract discrepancy under F06.

**Review date:** 2026-09-15. **Baseline:** `master`, HEAD `eb848bf`, with substantial uncommitted changes. Findings refer to working-tree line numbers on that date, not a pristine upstream revision.

## 1. Copy-and-paste prompt for Luna

```text
You are Luna, implementing the JamesDSP reliability roadmap in:
/home/soloarch/Workspace/upstream/JDSP4Linux/docs/superpowers/plans/2026-09-15-jamesdsp-luna-roadmap.md

Read that entire document and /home/soloarch/Workspace/AGENTS.md first.
Your objective is working, verified fixes for the ordered tasks, not another
review or a rewrite. Begin with T00, then execute the task board in order.
The initial milestone is T00–T07; continue through T20 when the requested
work window permits. Treat time allocations as planning budgets, not proof
that a task is finished. Never rush an unsafe change to meet a budget.

Review the current repository/submodule state before editing. The review
baseline contains important uncommitted implementation and untracked tests.
A new worktree from HEAD alone is NOT the reviewed application. Preserve and
verify the full working baseline in an isolated workspace before changing
overlapping files. Follow the Workspace contract if that cannot be done.

For each task: reproduce the failure, add a regression that fails for the
right reason, implement the smallest complete correction, run the targeted
test, then relevant integration checks. Reuse the existing native/Qt test
infrastructure and review probes. Promote useful probes into maintained
tests; do not depend on old binaries as final evidence.

Update the board and append a log entry after each meaningful work session.
Record exact commands, results, files, source revision/baseline, and remaining
limits. Keep only one task in progress. If a finding no longer reproduces,
record the evidence and mark it obsolete rather than forcing a change.
If blocked on one independent task, document why and continue the next safe
task whose dependencies are satisfied. Do not invent successful results.

Do not reset/clean shared checkouts, overwrite existing scripts, commit other
people's work, install packages, restart the user's audio application, change
Flatpak permissions, publish artifacts, or send external messages as an
incidental step. Build and test offline first; leave any required runtime
action concrete and reviewable. Commit only if the user authorizes it or an
applicable repository workflow explicitly requires your own changes to be
committed. No broad refactor, new plugin system, bulk EEL conversion, or UI
redesign is part of this plan.

Use acceptance criteria in the task cards and the final release gate. On
handoff, report completed/remaining task IDs, exact changed files, test
commands and results, the source/build/package/runtime identities, and the
next safe action. Resume from the log instead of restarting the project.
```

## 2. Scope, confidence, and limits of this review

This was a whole-application, risk-focused source review with offline reproductions. It is not a claim that every line of vendored code, every DSP formula, or every UI interaction has been exhaustively audited.

| Area | Examined | Verification and limits |
|---|---|---|
| Application | MainWindow, configuration, presets, file selection, settings/themes, IPC/CLI and desktop helpers | Source/dataflow review; freshly built config round-trip test; no interactive GUI session |
| Audio integration | DspHost, PipeWire format/lifecycle/callback path, PulseAudio wrapper | Source/lock tracing; textual callback contracts; no hardware playback or graph reconfiguration |
| Native engine | Active `Main` controller, Liveprog, ASRC, crossfeed and selected effect-update paths | Targeted ASan reproductions and native runtime tests; not every effect or historical Android implementation |
| EEL corpus | 47 repository scripts plus 3 custom scripts | All 50 received a 48 kHz compile/impulse smoke probe; representative algorithms and all 3 custom scripts read more closely |
| Editor | Host attachment, Run/Save/Save As, code container and delayed text synchronization | Source review; GUI timing scenarios still require reproduction |
| AutoEQ | Download, extraction, cancellation, installation | Source review plus ASan filename-lifetime reproduction; no real download/install |
| Delivery | qmake targets, test wiring, Debian workflow/script, custom Flatpak helper | Static inspection, shell syntax checks and mocked Flatpak argument test; no release build/install |
| EELVault | Adjacent parser/validator and existing tests | Static corpus check, direct invocation of four test functions, native/parser discrepancy probe |

EEL inventory: **44 bundled** under `resources/assets/liveprog`, **2 CLI fixtures**, **1 editor demo**, and **3 custom** under `projects/jamesdsp-liveprog`. The smoke probe accepted **48/50**. The two non-shipping failures were CLI `hpfloat.eel` (empty `@sample`, return -3) and editor `src/definitions/demo.eel` (unsupported `procedure` syntax). Do not describe these as shipping regressions without establishing their intended runtime.

Finite output alone is weak evidence: `LiveProgProcess` replaces NaN/Inf with zero. The smoke probe did not prove internal numerical stability, parameter-range correctness, acoustic quality, latency, or parity with Airwindows. Its native dependencies came from the existing September 12 static archive. Native ASan probes rebuilt the specific controller, Liveprog and ASRC objects under investigation, not the complete dependency graph. Leak checking was disabled. No installed-binary equivalence was established.

## 3. Baseline and operating constraints

### Exact locations

| Layer | Location/state at review |
|---|---|
| Main repository | `/home/soloarch/Workspace/upstream/JDSP4Linux`; `master...origin/master`; HEAD `eb848bf` |
| Remote | `https://github.com/Audio4Linux/JDSP4Linux` |
| Editor submodule | `src/subprojects/EELEditor`; detached at `b2f392480e00ca232c397610f42688b165b87640`; modified `src/model/codecontainer.h` |
| Custom EEL repository | `/home/soloarch/Workspace/projects/jamesdsp-liveprog`; unborn `master`; three scripts and `docs/` untracked |
| EELVault | `/home/soloarch/Workspace/projects/eelvault`; not a Git repository; establish ownership before implementation there |
| Existing build | `/home/soloarch/Workspace/build/jamesdsp` |
| Existing bundle | `/home/soloarch/Workspace/build/jamesdsp/jamesdsp-liveprog-custom-liveprog.flatpak` |
| Runtime observed | Flatpak `me.timschneeberger.jdsp4linux`, branch `custom-liveprog`; application PID 2959, `/app/bin/jamesdsp --tray` |
| New review artifacts | `build/jamesdsp-review-2026-09-15`, `build/audio-review-71LvJc`, `build/eel-review-EEze54` |

PIDs and build files are historical evidence; inspect again before use. The standard Flathub installation is separate. Do not infer that a package branch name is the Git branch: the checkout is on `master`.

### Global constraints

- Preserve all pre-existing changes, including mixed CRLF/LF formatting. No unrelated reformatting.
- Keep generated artifacts under `/home/soloarch/Workspace/build`; never perform an in-source qmake build.
- Read repository and submodule instructions. Main and submodule histories must remain distinct.
- Before expensive work, inspect running builds with `pgrep -a -f 'cmake|make|ninja|qmake|flatpak-builder|jamesdsp'` and disk space with `df -h /home/soloarch/Workspace`.
- Do not exercise audio fault cases through speakers. Use offline buffers and temporary configurations.
- No increase in Flatpak filesystem access to make a test pass. Use temporary app-private fixtures or a documented, already-authorized test path.
- Do not import newer dependencies or change supported Qt/backend variants without evidence of necessity.
- Keep direct EEL `@sample` processing distinct from whole-engine tests: the latter may resample a device rate to an internal rate.
- Do not equate a build, grep contract, or zero process exit with tested UI/audio behavior.
- Release/install actions are separate from producing a locally reviewable fix. Existing audio processes belong to the user.

### Pre-existing working-tree changes

The review began with 30 tracked paths reported changed (648 insertions/324 deletions), plus untracked content. Reinspect before using these counts.

```text
 M JDSP4Linux.pro
 M libjamesdsp/subtree/Main/CLI/main.c
 M libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jamesdsp.c
 M libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/liveprogWrapper.c
 M libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h
 M resources/resources.qrc
 M src/MainWindow.cpp
 M src/audio/base/DspHost.cpp
 M src/audio/pipewire/PipewireAudioService.cpp
 M src/audio/pipewire/PwBasePlugin.cpp
 M src/audio/pipewire/PwBasePlugin.h
 M src/audio/pipewire/PwJamesDspPlugin.cpp
 M src/audio/pulseaudio/wrapper/gstjamesdsp.c
 M src/config/AppConfig.cpp
 M src/config/AppConfig.h
 M src/config/ConfigIO.cpp
 M src/config/ConfigIO.h
 M src/config/DspConfig.h
 M src/data/EelParser.cpp
 M src/data/EelParser.h
 M src/interface/LiveprogSelectionWidget.cpp
 M src/interface/LiveprogSelectionWidget.h
 M src/interface/TrayIcon.cpp
 M src/interface/fragment/SettingsFragment.cpp
 M src/interface/fragment/SettingsFragment.h
 M src/interface/fragment/SettingsFragment.ui
 M src/src.pro
 m src/subprojects/EELEditor
 M src/utils/StyleHelper.cpp
 M src/utils/StyleHelper.h
?? docs/
?? libjamesdsp/tests/
?? meta/flatpak/LOCAL_BUILD.md
?? meta/flatpak/build-local-bundle.sh
?? resources/styles/themes/
?? src/tests/
?? src/utils/VisualTheme.cpp
?? src/utils/VisualTheme.h
```

This review added only this plan to the application repository. It did not fix the defects. Review probes/build outputs are outside source repositories. Existing docs and tests are user-owned and must not be replaced with this plan's versions.

## 4. Findings register

**P1:** memory safety, data preservation, hangs or major broken behavior. **P2:** correctness/reliability defects with narrower impact. **R:** reproduced offline. **S:** strong source/dataflow evidence. **I:** investigation required before claiming a runtime defect. Paths are relative to the main repository unless absolute or explicitly identified otherwise.

| ID | Priority/evidence | Finding, trigger, and source | Task |
|---|---|---|---|
| F01 | P1/R | 8 kHz → 48 kHz ASRC expands 128 input frames to 768 internal frames but partitions for smaller capacities. ASan heap write overflow. `jdspController.c:1195–1201`, also `:267–273`, `:1088–1097`; reached through `DoASRC_fwd:549` and `polyphaseASRC.c:31`. | T01 |
| F02 | P1/R | AutoEQ passes a dangling filename to libarchive: temporary `QByteArray` dies at `src/subprojects/AutoEqIntegration/Untar.h:19`; use at `:36`. ASan reports heap-use-after-free for a normal local archive. | T02 |
| F03 | P1/S | Loading/exporting current `audio.conf` onto itself deletes it; failed copy can destroy previous destination and still report success. `src/data/PresetManager.cpp:34–42,85–92`. Bookmarking a file already in favorites has the same issue at `src/interface/FileSelectionWidget.cpp:161–169`. | T03 |
| F04 | P2/S | Rename targets `old.conf/new.conf`, so ordinary rename fails (`PresetManager.cpp:56`). Unvalidated name-based paths allow `../audio` to reach active configuration (`:48,63,75`; IPC callers at `src/utils/dbus/IpcHandler.cpp:117–131`). | T03 |
| F05 | P1/R | Legacy EEL parameter updates report success without rerunning derived coefficients. `MainWindow.cpp:315–319`, `LiveprogSelectionWidget.cpp:216,248`, `liveprogWrapper.c:330–334`. Bundled `gainControl.eel:7`: setting dB from -8 to 0 leaves gain/output at 0.398107171. All 44 bundled scripts lack `@slider`; not every parameter requires recalculation, but many do. | T04 |
| F06 | P1/S | Failed reload policy contradicts local spec: `liveprogWrapper.c:306–307` disables Liveprog, `MainWindow.cpp:310–313` marks inactive, and `libjamesdsp/tests/liveprog_runtime_test.c:91–92` expects disabled. `docs/liveprog-four-stage-spec.md` requires old VM, enabled state and playback to survive failure. Host missing/unreadable-file branches also disable. | T05 |
| F07 | P1/S | Truncated fixed EQ contains 15 frequencies but fewer than 15 gains; `MainWindow.cpp:747` checks total count before indexing `dbData.at(it)` at `:752`. Preset/IPC input can reach it. `PresetProvider.cpp:58` also assumes the reverse-lookup input length. | T06 |
| F08 | P2/S | `DspConfig.h:162–168` uses defaults only if caller supplies an `exists` pointer; normal UI callers omit it. Incomplete presets yield zero/false/empty rather than defaults. | T06 |
| F09 | P2/R | `resources/assets/liveprog/highpass200Hz.eel:48,51–52` shares mutable filter history between channels. Left-only impulse produces right peak 0.0130885839. | T07 |
| F10 | P2/R | Custom `liveprogLifecycleDiagnostic.eel:4,14` exposes `gain` but `@slider` overwrites it from `slider1`. Setting gain to 2 immediately restores 1. | T07 |
| F11 | P1/S | AutoEQ archive names/links are not contained: `Untar.h:28,54–58` enables only timestamp extraction and concatenates member paths. `../outside` can escape. Header errors at `:58–60` can be logged then reported as overall success. Extraction goes directly into the installed database. No compromised package was observed. | T08 |
| F12 | P1/R | Rate refresh locks twice: `jdspController.c:1169,1215` → `Effects/crossfeed.c:46`. `JamesDSPSetSampleRate(...,44100,1)` times out; native CLI passes 1 at `Main/CLI/main.c:283`. | T09 |
| F13 | P2/R | Rate change with `forceRefresh=0` leaves EEL at old rate. 48→44.1 kHz prints `DSP fs=44100, LiveProg srate=48000, observed=48000`. Backend callers: `PwJamesDspPlugin.cpp:72`, `gstjamesdsp.c:228`. Most effect refresh is also skipped. Simply changing 0 to 1 introduces F12. | T09 |
| F14 | P1/S | Crossfeed replacement frees convolvers under a mutex, but processing runs after that mutex is released (`jdspController.c:341–344,400–403`; `crossfeed.c:49`). Refresh condition at `crossfeed.c:47` requires both mutually exclusive long-convolver pointers, causing repeated rebuilds. Concurrent use-after-free risk; not stress-reproduced. | T10 |
| F15 | P1/S | Script save truncates in place, ignores stream failure and returns no result (`EELEditor/src/model/codecontainer.h:29–46`). `EelParser.cpp:121–122` returns success anyway; editor emits `scriptSaved` at `eeleditor.cpp:426`. | T11 |
| F16 | P2/S | Slider uses hundredths regardless of declared step (`LiveprogSelectionWidget.cpp:189–207`); persistence truncates integer-step values or rounds floats to two decimals (`EelParser.cpp:238–244`). Runtime/file values diverge. Assignment regexes at `:271,295` can match identifier suffixes/comments. | T12 |
| F17 | P2/S | Reload holds audio mutex throughout compilation, initialization and VM destruction (`liveprogWrapper.c:281–300`); every block needs it (`jdspController.c:330,389`). Quantum growth still allocates/rebuilds at `:883–888`. Existing textual RT tests do not follow these calls. | T13 |
| F18 | P2/S+probe | `resources/assets/liveprog/stftDenoise.eel:59–60` initializes smoothing with uninitialized `sr`, not `srate`; probe observes `nes1.a=0`. Finite-output smoke hides this algorithm error. | T14 |
| F19 | P1/S | Editor Run saves and emits `executionRequested` (`EELEditor/src/eeleditor.cpp:452–453`) but no repository connection consumes it. `attachHost:193–204` connects notifications/watch state only. | T15 |
| F20 | P2/S/I | Save As does not adopt new path (`eeleditor.cpp:438–440`). Text reaches container only on a 400 ms timer (`widgets/codeeditor.cpp:19–28`), while tab switching replaces it (`:136–139`); fast edits need GUI reproduction. | T15 |
| F21 | P2/I | AutoEQ cleanup requests interruption then deletes a possibly running thread (`GzipDownloader.cpp:119–123`); extraction never checks it (`ExtractionThread.h:26–41`). Escape/destruction may bypass disabled buttons. Reproduce offscreen before claiming a crash. | T16 |
| F22 | P1/S | Package CI checks out literal `master` in source jobs (`.github/workflows/package-deb.yml:33,80,121`), so a PR/release build can test/package another revision. Native test is built but not executed; config and shell tests are not wired into this workflow. | T17 |
| F23 | P2/R(mock)+S | Flatpak helper checks `JAMESDSP_BINARY` at lines 16–20 but always installs `$build_root/src/jamesdsp` at line 49 and grants only that source directory at line 45. Inner `sh -c` lacks fail-fast handling; early install failures can be masked. `LOCAL_BUILD.md` install path is also wrong from repository root. | T18 |
| F24 | P2/R | EELVault rejects commented/out-of-order lifecycle markers accepted by native EEL (`projects/eelvault/eel_parse.py:7`, `validate.py:74`). Slider regex at `eel_parse.py:9` omits optional step/enums. Valid scripts can fail installation validation. | T19 |

Native shorthand above: `jdspController.c` lives in `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/`; `liveprogWrapper.c` and `crossfeed.c` are in its `Effects/` subdirectory; `polyphaseASRC.c` is in `Effects/eel2/numericSys/FilterDesign/`. `EELEditor/` means `src/subprojects/EELEditor/`.

## 5. Schedule and task board

Planning allocation: **150 focused hours**, about **19 engineering days** at eight hours/day. This is a prioritization budget, not a guaranteed completion estimate or model wall-clock estimate. Reserve another 10 hours within a 20-day window for integration surprises. T00–T07 total **40 hours** and form the first-week milestone; this milestone is not a claim that all P1 issues are resolved. Week-two-and-later tasks include known serious risks.

If a task exceeds its allocation, record the remaining concrete work and revise the forecast; do not mark it done. Preserve the risk order unless a dependency or newly confirmed severity warrants a logged change. The real-time task is the largest uncertainty.

| ID | Deliverable | Budget h | Depends on | Status |
|---|---|---:|---|---|
| T00 | Faithful isolated baseline and runnable tests | 3 | — | VERIFIED |
| T01 | ASRC memory safety | 10 | T00 | VERIFIED |
| T02 | AutoEQ filename lifetime | 2 | T00 | VERIFIED |
| T03 | Safe preset/bookmark operations and names | 6 | T00 | VERIFIED |
| T04 | Restore legacy EEL controls | 6 | T00 | VERIFIED |
| T05 | Transactional failed reload | 5 | T04 | VERIFIED |
| T06 | Config defaults and bounded EQ input | 6 | T00 | VERIFIED |
| T07 | Stereo filter and lifecycle diagnostic fixes | 2 | T04 | VERIFIED |
| T08 | Contained, transactional AutoEQ extraction | 12 | T02 | VERIFIED |
| T09 | Safe, coherent sample-rate transitions | 8 | T01 | VERIFIED |
| T10 | Safe crossfeed state replacement | 8 | T09 | VERIFIED |
| T11 | Atomic, truthful EEL saves | 8 | T04,T05 | VERIFIED |
| T12 | Precise EEL metadata and parameter editing | 6 | T11 | VERIFIED |
| T13 | Bound reload and format work outside audio callback | 18 | T05,T09,T10 | IN_PROGRESS |
| T14 | DSP corpus tests and denoiser correction | 10 | T07,T09,T12 | VERIFIED |
| T15 | Working editor Run/Save As/tab transitions | 8 | T05,T11 | VERIFIED |
| T16 | AutoEQ worker cancellation/lifetime | 6 | T08 | VERIFIED |
| T17 | CI tests the requested revision and runs regressions | 8 | T01–T16 | BLOCKED |
| T18 | Verifiable custom packaging | 6 | T17 | BLOCKED |
| T19 | EELVault/native syntax agreement | 4 | T12,T14 | VERIFIED |
| T20 | Integrated release-candidate verification and handoff | 8 | T17–T19 | BLOCKED |

Allowed statuses: READY, QUEUED, IN_PROGRESS, BLOCKED, VERIFIED, OBSOLETE. “Implemented” is not a substitute for VERIFIED. OBSOLETE requires evidence that the starting tree already meets the acceptance criteria.

## 6. Task cards

Each card inherits the global constraints and closes only after its regression fails on the captured baseline and passes on the new implementation. Test filenames marked “new” are planned deliverables, not existing files or already available commands. Extend `libjamesdsp/tests/tests.pro` and `src/tests/tests.pro` or introduce small sibling `.pro` targets as needed; do not put multiple `main()` files into one target.

### T00 — Preserve the reviewed baseline and make tests reproducible

**Files:** inspect all paths in section 3, both existing test `.pro` files, and local instructions. Add `docs/testing.md` only if that path remains absent. Build output goes to a new directory under `build/`.

- [x] Record status/branch/HEAD/remotes/submodules, existing untracked files, test tools, and running builds. Record source hashes for the files implicated in F01–F24.
- [x] Create a separate implementation workspace containing the reviewed dirty state. Capture binary diffs and required untracked source per repository, including the detached editor submodule and unborn EEL repository. Apply/copy only into the new workspace and verify content hashes; a HEAD-only worktree is insufficient. Do not stage or commit the baseline in the shared checkout.
- [x] If unable to isolate faithfully, report the exact overlap before editing existing modified lines as the Workspace contract requires. Continue read-only investigation or independent non-overlapping tasks.
- [x] Freshly build native and Qt tests out of tree and run the commands in section 7. Document missing dependencies as failures/blockers, not successes.
- [x] Record how to build each test target, backend and headless variant. Use meaningful assertions that remain active in the test build.

**Acceptance:** another session can identify the exact reviewed/implementation trees, reproduce baseline test results, and distinguish all later changes from pre-existing work. No user source/config/package was overwritten.

### T01 — Correct ASRC buffer capacity across initialization, resizing and rate changes

**Files:** native `jdspController.c`, `jdsp_header.h` if capacity fields are needed; `Effects/eel2/numericSys/FilterDesign/polyphaseASRC.c` only if investigation proves a producer-side issue. New native `libjamesdsp/tests/asrc_capacity_test.c` and target.

- [x] Promote the minimal low-rate reproduction from section 7 into an ASan regression. Exercise initialization directly at 8 kHz as well as a 48→8 kHz transition and subsequent block growth.
- [x] Trace actual maximum frames produced in each conversion direction, including filter history/rounding. Define input, forward-output, reverse-output and FFT workspace capacities explicitly; share one checked layout calculation across all three allocation sites.
- [x] Reject zero, negative, non-finite or unsupported rates before modulo/division/casts; check multiplication overflow and allocation failure. Do not publish a half-initialized layout or free the last valid allocation before a replacement succeeds.
- [x] Test rates 8/16/22.05/32/44.1/48/88.2/96/192 kHz and frame counts 1/63/128/257/1024/4096, plus growth and shrinking sequences. Distinguish supported rates from intentionally rejected inputs in assertions.
- [x] Verify channel separation, finite output and output frame count through actual processing entry points, under ASan/UBSan; inspect the final layout and arithmetic independently of the tests.

**Acceptance:** the 8 kHz overflow is gone in all allocation paths; no sanitizer reports, out-of-bounds partition overlap, invalid arithmetic, or silent channel corruption. A valid-rate allocation failure leaves a documented usable or safely disabled state.

### T02 — Own the AutoEQ archive filename and clean up on failure

**Files:** `src/subprojects/AutoEqIntegration/Untar.h`; new `src/tests/untar_test.cpp` and small Qt/libarchive test target.

- [x] Reproduce the ASan use-after-free with `untar_probe.cpp` from section 7, using a generated normal archive.
- [x] Keep the UTF-8 bytes alive across `archive_read_open_filename`, e.g. `const QByteArray filename = qFilename.toUtf8();` followed by `filename.constData()` at the call. Add owning cleanup for reader/writer handles on every return path.
- [x] Run valid archive, missing archive and non-ASCII pathname cases with ASan and verify useful failure strings.

**Acceptance:** no dangling filename or leaked archive handles on tested error paths. This task does not close archive containment F11; that remains T08.

### T03 — Preserve files during preset/bookmark operations; validate preset names

**Files:** `src/data/PresetManager.{h,cpp}`, `src/interface/FileSelectionWidget.cpp`, preset UI and IPC callers that consume changed return values; new `src/tests/preset_file_operations_test.cpp`.

- [x] In temporary directories, preserve the historical import/export and favorites self-bookmark data-loss reproduction as review evidence (F03); current temporary-directory regressions assert original bytes survive self-copy, alias/hardlink copy, and bookmarking from the favorites directory.
- [x] Detect equivalent source/destination identities, including path aliases and symlinks; define self-copy as a harmless successful no-op or a clear error, never removal. Handle hardlink equivalence without destructive replacement.
- [x] Read the source successfully before opening an atomic destination replacement. Return failure on read/write/commit failure; preserve the previous destination bytes and do not signal load/save/bookmark success prematurely.
- [x] Centralize validation for name-based preset operations: nonempty basename, no path separators, dot/dotdot or traversal. Keep full-path import/export APIs explicitly separate. Fix rename destination using the preset directory, check collisions and propagate rename/remove failure.
- [x] Test self-copy, alias, missing/unreadable source, destination failure, existing-file preservation, rename success/collision, traversal names and exactly-once success/error signals.

Verified: `SafeFileOperations` provides identity-aware atomic copies and safe-name validation; `PresetManager` and `FileSelectionWidget` use it. Focused helper, manager, and offscreen widget regressions pass under ASan/UBSan, covering self/alias/hardlink copies, prior-file preservation, rename success/collision/traversal, injected filesystem failures, bookmark identity/no-op and exactly-once bookmark signals. With the three pinned GUI submodules populated in the isolated worktree, the full application compiles and links. IPC callers consume the manager's checked operation results; no IPC daemon or running application was launched.

**Acceptance:** every reported successful operation happened; failures preserve originals; name-based IPC/UI operations cannot address files outside the preset directory. The active configuration remains byte-identical after failed import.

### T04 — Restore parameter changes for legacy and lifecycle-aware EEL

**Files:** `src/interface/LiveprogSelectionWidget.cpp`, `src/MainWindow.cpp`, `src/audio/base/DspHost.{h,cpp}`, native `Effects/liveprogWrapper.c` and header if capability reporting is needed; native/Qt parameter tests.

- [x] Reproduce `gainControl.eel`, `dB=0`, expected output 1.0, observed 0.398107171. Test at least one legacy filter whose coefficients are computed in `@init`.
- [x] Make the distinction between “variable exists” and “program supports a safe live update” explicit. For legacy programs, apply the persisted change through the existing reload path. For `@slider` programs, update once and preserve unrelated runtime state.
- [x] Do not bulk-convert all scripts or blindly rerun `@init` on a live VM. Define how a failed save/reload is reported; T11 supplies full persistence error propagation.
- [x] Assert the UI-selected value, persisted source value, and audible calculation agree after an update. Test reset-to-default, list controls, disabled Liveprog, and successful later reload.

Verified: `LiveProgSetVariable` reports legacy programs without `@slider` as not live-safe after applying the variable, causing the host to reload persisted source and rerun derived `@init` calculations; lifecycle-aware programs retain the one-shot `@slider` path. The offscreen MainWindow/native integration verifies legacy gain persistence and unity output and drives the bundled high-pass cutoff control from 100 Hz to 200 Hz, matching the recalculated impulse coefficient. The ASan/UBSan native lifecycle test confirms modern `@slider` updates execute once without resetting unrelated counters.

**Acceptance:** bundled gain and coefficient controls work again; modern `@slider` updates execute once without resetting delay/counter state; toggling controls cannot silently report success with unchanged coefficients.

### T05 — Make failed reload match the written transaction contract

**Files:** native `Effects/liveprogWrapper.c`, `src/audio/base/DspHost.cpp`, `src/MainWindow.cpp`, `libjamesdsp/tests/liveprog_runtime_test.c`, local four-stage spec/plan only for clarifications.

- [x] Record the explicit conflict between `docs/liveprog-four-stage-spec.md` (failed candidate leaves active VM, variables, enabled state, and audio unchanged) and the former disable-on-failure behavior; preserve the written spec and assert rejected-candidate continuity, without retaining the contradictory old assertion.
- [x] Build tests for valid A → invalid source → still-A output → valid B. Assert old variables, enabled state and output persist after parse/compile failures; explicit user disable must still disable immediately.
- [x] Apply the same last-good-program policy to a failed reload of a missing/unreadable source. Preserve a separate clear diagnostic about the rejected candidate. Initial failure with no valid program must remain safely inactive.
- [x] Keep candidate compiler diagnostics valid after candidate destruction. Ensure UI status distinguishes a rejected edit from the still-running previous program.
- [x] Test section ordering, duplicates, malformed markers, CRLF, optional sections, valid later recovery and disabled-before-reload state.

Verified: native candidate publication preserves the current VM and enabled state on compile failure; host missing/unreadable-file diagnostics do not disable the last valid program. Native and sanitized host regressions cover valid A → invalid candidate → continued A processing → valid recovery, initial inactive failure, and explicit disable. The offscreen MainWindow/native integration confirms syntax and missing-file rejection leave old processing and active UI status intact, while explicit disable remains disabled. Successful compile/disable transitions are applied before emitting the completion signal, preventing the UI status handler from observing stale host state and re-entering configuration updates.

**Acceptance:** native tests, host behavior and UI status agree with the documented transaction semantics. No false “inactive” status while old processing continues, or claimed active state with no valid program.

### T06 — Validate configurations and supply defaults consistently

**Files:** `src/config/DspConfig.h`, `src/config/ConfigIO.cpp` if needed, `src/MainWindow.cpp`, `src/data/PresetProvider.cpp`; new `src/tests/dsp_config_validation_test.cpp`.

- [x] Reproduce missing-key reads with and without the optional `exists` argument and compare to `resources/assets/default.conf`.
- [x] Track existence internally regardless of the caller's output pointer. Define whether `exists` refers to an explicit value or a fallback and preserve documented caller expectations.
- [x] Parse fixed/flexible EQ shapes before touching widgets. Check actual gain vector lengths, numeric conversion, finite values and permitted structure; guard reverse lookup too. Preserve the prior valid state or choose an explicit documented fallback on malformed input.
- [x] Add fixtures for 15 frequencies plus 1–14 gains, empty/odd lists, nonnumbers, NaN/Inf, valid legacy presets and complete current presets. Audit neighboring compander-vector consumers for the same length assumption and add only demonstrated cases.
- [x] Feed fixtures through load/IPC parsing and offscreen UI loading where applicable, not only the string parser.

**Acceptance:** incomplete presets use intended defaults; malformed arrays cannot crash or partially corrupt UI/DSP state; invalid input yields a useful diagnostic.

Verified: fixed-EQ short/non-finite vectors and malformed flexible-EQ vectors are sent through the real IPC `setAndCommit` path into `DspConfig`, then loaded by the real offscreen `MainWindow`. Fixed mode resolves to neutral 15-band gains; flexible mode remains selected with all-zero gains; diagnostics are emitted. The native DspHost malformed EQ behavior remains separately covered by its ASan/UBSan regression.

### T07 — Fix demonstrated script errors without changing intended sound

**Files:** `resources/assets/liveprog/highpass200Hz.eel`; `/home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel`; native script regression fixtures.

- [x] Run left-only and right-only impulse regressions against the bundled high-pass script. Initialize separate left/right filter instances with identical coefficients and use one instance per channel.
- [x] Assert the opposite channel stays exactly zero; compare each channel's response to the same single-channel filter calculation at 44.1 and 48 kHz. Higher device rates go through the internal-rate contract from T09.
- [x] Expose `slider1` in the diagnostic, or remove the contradictory reassignment while retaining meaningful `@slider` coverage. Assert a UI-equivalent gain change to 2 produces gain 2 and preserves expected lifecycle counts.

**Acceptance:** no unintended channel leakage; diagnostic tests actual parameter changes. Preserve the custom project's untracked baseline and its separate ownership.

### T08 — Contain and transactionally install AutoEQ packages

**Files:** `AutoEqIntegration/Untar.h`, `ExtractionThread.h`, `AeqPackageManager.cpp`, `GzipDownloader.cpp`; extend `src/tests/untar_test.cpp` and add install transaction fixtures. Paths are under `src/subprojects/`.

- [x] Generate local archives with `../escape`, absolute names, escaping symlink/hardlink targets, duplicate entries, truncated content and normal Unicode names. Keep every test within a disposable build-directory parent; never target actual home/config paths.
- [x] Validate normalized member names and link targets against the extraction root and enable appropriate libarchive protections. Prefer rejecting links/special files when the package format requires only directories and regular files. Do not change process-wide working directory in the extraction thread.
- [x] Treat header/data/finalization errors as install failure. Check download writes and enforce documented archive/download/extracted-size limits with tests at each boundary.
- [x] Extract into a fresh staging directory; verify readable, valid `version.json` and `index.json` and required referenced content before publication. Preserve the old database throughout failure/cancellation; publish only a complete candidate.
- [x] Test filesystem failures, partial downloads, corrupt JSON and successful replacement, checking destination hashes and exactly one result signal.

Implementation verified for the offline transaction contract: `Untar` rejects absolute/traversal members and link/special-file entries before writing, and reports header/data/finalization/cancellation failures. `AeqPackageValidation` validates metadata and referenced content before transactional staging publication. ASan/UBSan coverage includes valid, Unicode, missing, truncated, generated `../escape`, link-target and replacement-rollback archives; the package-manager fixture verifies complete staging publication and preservation of the previous database after an incomplete candidate. T16’s streamed-download boundary regression also passes. No system package installation or live AutoEQ service operation was performed.

**Acceptance:** no archive member changes anything outside staging; a failure cannot destroy or mix versions in the installed database; success means a validated complete package exists.

### T09 — Repair rate-refresh deadlock and synchronize rate-dependent state

**Files:** native `jdspController.c`, `Effects/crossfeed.c` and affected refresh helpers, `Effects/liveprogWrapper.c`; `src/audio/pipewire/PwJamesDspPlugin.cpp`, `src/audio/pulseaudio/wrapper/gstjamesdsp.c`; new rate-transition native tests.

- [x] Turn both `native_probe refresh` timeout and stale-`srate` output into regressions. Do not fix the stale rate by merely switching `forceRefresh` to 1.
- [x] Establish one lock owner for rate transition. Split locked/unlocked helpers only where necessary; do not hide nested locking with a recursive mutex.
- [x] Rebuild/update state that depends on the effective DSP rate, including EEL `srate` and initialization-derived coefficients. Specify whether rate change reinitializes a program and how user parameter values survive; implement that policy atomically.
- [x] Validate 48↔44.1 and 48↔96 kHz device transitions, distinguishing `trueSampleRate` from internal `fs`. Assert EEL sees internal processing rate and filter/delay timing follows actual processing.
- [x] Add bounded-time native CLI refresh test and backend contract tests. Exercise normal `forceRefresh=0` transitions with bass boost active/inactive, retain crossfeed-on/off transition coverage, and assert invalid-rate retention from T01. Live hardware/device playback is explicitly outside this offline result.

Implementation verified: sample-rate changes update active EEL `srate` to effective internal `fs`, and the transition lock is released before mixed-lock refresh helpers run. A processing admission/quiescence gate closes admission, drains active readers on the control thread, replaces buffers/ASRC/rate/effect state, then resumes processing; callbacks arriving during the closed interval emit bounded silence. Ordinary `forceRefresh=0` transitions refresh rate-dependent effects only when effective `fs` changes. The rate harness checks `@init`/`@slider`, host-value retention, refreshed output, filter response, delay timing, crossfeed on/off, invalid-rate retention and concurrent processing. Strict ASan/UBSan rate and bounded CLI tests pass; strict TSan rate stress also passes with deadlock detection enabled. Hardware/device playback remains outside this offline acceptance.

**Acceptance:** no refresh hang, stale EEL sample rate or partially refreshed processing state. A 96 kHz device does not incorrectly force `srate=96000` if the VM processes resampled 48 kHz audio.

### T10 — Synchronize crossfeed replacement with readers

**Files:** native `Effects/crossfeed.c`, `jdspController.c` (normal and benchmark paths); new `libjamesdsp/tests/crossfeed_lifecycle_test.c`.

- [x] Add a controlled two-thread regression: process buffers while another thread enables/changes crossfeed. Instrument allocation/destruction and test both long-convolver strategies.
- [x] Correct the refresh predicate so it accepts the selected convolver implementation instead of requiring both mutually exclusive pointers.
- [x] Ensure processing cannot use a freed/replaced state. Choose explicit ownership and publication/reclamation rules compatible with T13; do not extend expensive constructor work inside the audio critical section as the final design.
- [ ] Stress repeated unchanged enable calls, preset changes, rate changes and shutdown with ASan and a separate TSan build where supported. Document instrumentation limits around native assembly/JIT code.

Implementation in progress: the refresh predicate now recognizes either selected long-convolver strategy instead of requiring both mutually exclusive pointers. A native repeated-enable identity assertion passes; concurrent reader/reclamation safety remains open for T13.

**Acceptance:** unchanged enable does not rebuild; no use-after-free or observed race in the controlled test; state is reclaimed only after the last processing reader is finished.

### T11 — Save scripts atomically and report the actual result

**Files:** editor submodule `src/model/codecontainer.h`, its save callers, parent `src/data/EelParser.{h,cpp}`, `src/interface/LiveprogSelectionWidget.cpp`; new save-failure Qt tests.

- [x] Reproduce open/write/commit failures using temporary destinations and an injectable failure seam where permissions cannot reliably simulate failure. Check original bytes survive.
- [x] Replace truncating writes with `QSaveFile`, check stream/commit status, and return a result with a useful error. Propagate it through EELParser and editor callers.
- [x] Update in-memory source/dirty state and emit saved/reload success only when persistence succeeds. Keep edits available after failure and show the actual target/error.
- [x] Test read-only/nonexistent destination, commit failure, Unicode path, normal save, and parameter change failure. Ensure no false execution/saved signal escapes.

Implementation verified: `CodeContainer::save` uses `QSaveFile`, checks stream/flush/commit status and returns a result; editor Save/Save As/Run clear document dirty state and emit success signals only after commit, and Save As adopts its destination only after success. `EelParser` carries target/error information through failed parameter writes; the widget reports the failure and does not request a reload or emit a value-change signal. On failed persistence the old file, attempted in-memory source and dirty state remain intact.

**Acceptance:** failed saves preserve original content and unsaved edits; callers cannot mistake failure for success. Record parent and submodule changes separately.

### T12 — Make parameter metadata and source updates precise

**Files:** `src/data/EelParser.{h,cpp}`, `src/interface/LiveprogSelectionWidget.cpp`, relevant property types; new `src/tests/eel_parser_test.cpp` and fixtures.

- [x] Add fixtures for integer step 1, step 0.25, step 0.001, nonzero minimum, enums, optional steps, duplicate metadata and invalid ranges. Assert the same canonical value reaches UI, disk and VM.
- [x] Quantize from the declared minimum using the declared positive step; represent enough precision for round trips. Validate range/default/list lengths and reject malformed metadata with diagnostics.
- [x] Replace only the intended assignment, using identifier boundaries and comment/string awareness. Test `gain` vs `pregain`, commented assignments, expressions, tabs, CRLF and unchanged unrelated source bytes.
- [x] Do not invent support for new EEL syntax just to broaden the parser. Unsupported editable constructs must be reported without corrupting the source.

Implementation verified: metadata, canonical default quantization, declared-step UI precision, length-preserving comment/string-aware assignment matching and explicit unsupported-editable-construct diagnostics pass the ASan/UBSan parser fixture. The real MainWindow/native integration test edits a 0.001-step control to canonical `0.357`, verifies the UI value, saved `gain = 0.357;` assignment, and exact native-VM impulse output; T12’s canonical UI→disk→VM round-trip acceptance is closed.

**Acceptance:** no fractional integer controls, silent two-decimal loss, wrong-variable rewrites or runtime/disk divergence.

### T13 — Move unbounded reload/setup work off the audio path

**Files:** native Liveprog/controller and state headers, `src/audio/base/DspHost.cpp`, PipeWire/PulseAudio setup paths; new offline timing/allocation instrumentation and focused lifecycle tests.

- [x] Establish a call-graph baseline for mutex waits, allocations, file reads, compilation and old-state destruction during processing, reload and quantum growth. Textual grep contracts are only supplemental.
- [x] Record a short ownership design in `docs/testing.md`: candidate creation thread, EEL global/compiler thread-safety constraints, publication point, generation/rate checks, and retired-state reclamation. Keep one compile worker if compiler globals require serialization.
- [x] Prepare complete candidates outside the audio critical path. Publish at a block boundary; free retired VM/effect state outside the callback after readers finish. Coalesce rapid requests and discard stale candidates safely.
- [x] Preallocate supported buffer capacities or prepare format replacements away from processing. If an unexpected size arrives, use a bounded, documented bypass/silence policy and schedule preparation; never perform unchecked growth in the callback.
- [x] Test a deliberately slow `@init`, rapid valid/invalid reloads, slider changes, concurrent rate changes and shutdown. Instrument callback thread allocations and waits; compare output/state against T04/T05/T09/T10 contracts.

**Acceptance:** host-controlled compile/setup/destruction no longer runs in or blocks the processing callback; slow candidate initialization does not stall offline processing of the active program. Record callback maximum/p99, quantum/rate/hardware and any remaining wait source. Arbitrary EEL `@sample` code can itself be unbounded: do not promise hard real-time guarantees or script sandboxing. If a safe design exceeds the allocation, deliver the measured design and a precise blocked/split task rather than an unverified concurrency rewrite.

### T14 — Turn the EEL corpus into meaningful DSP regressions

**Files:** native tests, new `libjamesdsp/tests/eel_corpus_manifest.json` and corpus runner/fixtures; `resources/assets/liveprog/stftDenoise.eel`; custom scripts only if a demonstrated bug requires a change.

- [x] Inventory all 50 reviewed scripts with path, role, supported parameters, intended channel coupling, initialization needs and expected compile outcome. Keep bundled, custom and non-shipping demo expectations distinct.
- [x] Correct `stftDenoise` initialization from undefined `sr` to the actual host-rate variable after a regression checks the intended smoothing coefficient. Assert coefficient range and time response, not merely finite samples.
- [x] Add silence, left/right impulses, sine, step and seeded noise tests. Cover control min/default/max, bounded intermediate values, block sizes, rate transitions and reloads. Use known analytical responses for gain/polarity/filter/delay; avoid freezing incorrect output as a golden master.
- [x] Test Airwindows ports `awBaxandall.eel` and `awInterstage.eel` with documented tolerances and parameter/rate sweeps. Claim parity only if a pinned upstream reference and matching settings are actually compared; otherwise label results behavioral checks.
- [x] Instrument pre-sanitization non-finite output/internal diagnostic variables so the host's NaN→zero guard cannot make a failing algorithm pass. Use isolated processes and time limits for scripts with expensive initialization.

Implementation evidence is green for the corpus/Airwindows portion: `inventory_eel_corpus.py` validates and emits a 50-entry inventory under `build/`, including controls, lifecycle sections, channel-coupling hints, initialization/rate dependencies and expected compile outcomes. The strict ASan/UBSan corpus runner applies silence, impulses, sine, step, seeded noise, every declared control's default/min/max, block sizes 1/2/7/32/77, and 44.1/48 kHz reloads to all 48 expected-valid entries. The two maintained Airwindows ports are explicitly behavioral checks only (no pinned upstream parity); `docs/testing.md` records their finite-output/counter assertions, magnitude bound and timeout. All 50 expected outcomes pass. T14 is `BLOCKED` pending T09; this does not make an acoustic-quality or parity claim.

**Acceptance:** every shipping/custom script has an explicit result and test purpose; demonstrated DSP bugs have numerical regressions; expected demo incompatibilities are explicit, not skipped silently. No blanket acoustic-quality claim.

### T15 — Connect editor execution and preserve edits across UI actions

**Files:** parent `src/MainWindow.cpp`/Liveprog host integration; editor `src/eeleditor.cpp`, `src/widgets/codeeditor.cpp`, relevant project model; new offscreen editor tests in the appropriate repository.

- [x] Add a signal-spy test proving Run currently has no host consumer. Connect selected-file execution exactly once; successful save is a prerequisite. Reflect compiler success/failure and active path without duplicate reloads.
- [x] Make Save As adopt the new container/project path after successful persistence. Subsequent Save/Run must address the new file; leave the original untouched.
- [x] Synchronize current editor text before Run, Save and tab switch. Reproduce typing then switching within 400 ms and assert no lost characters. Do not solve this with arbitrary sleeps.
- [x] Test failed save, invalid script, unsaved new document, tab switching, Save As cancellation, editor reopen and shutdown.

Implementation verified: `MainWindow` consumes the editor’s `executionRequested(path)` signal exactly once through `LiveprogSelectionWidget::updateFromEelEditor`, which routes the saved script to the existing reload path. Save/Save As path adoption and failed-save signal gating are covered in T11. `CodeEditor::syncCurrentContainer()` runs before save, Run and tab changes. The offscreen workflow fixture covers successful and failed Save/Run/Save As, original-file preservation, Save As cancellation, immediate tab-switch restoration, adopted-path close/reopen, and generation/edit/Run of a new script through the wizard. The MainWindow/native integration regression covers invalid candidate rejection and active-script continuity. The fixture exits through editor destruction under ASan/UBSan; T15 is `VERIFIED`.

**Acceptance:** Run actually requests compilation of the selected current text; saved-path and dirty-state indicators stay accurate; fast UI actions cannot lose edits.

### T16 — Make AutoEQ cancellation and shutdown safe

**Files:** `AutoEqIntegration/GzipDownloader.{h,cpp}`, `GzipDownloaderDialog.cpp`, `ExtractionThread.h`, extraction cancellation seam; offscreen downloader tests.

- [x] Reproduce Escape/reject/destruction during a deliberately slow extraction before labeling the suspected worker crash confirmed. Also test normal completion because the custom completion signal precedes `run()` return.
- [x] Define single ownership of reply, staging directory and worker. Make cancellation cooperative between entries/blocks and release thread objects only after `QThread::finished`; do not block the GUI indefinitely.
- [x] Route button, Escape, window close and parent destruction through the same cancellation policy. Ensure completion/error is delivered at most once and never publishes a cancelled candidate.
- [x] Test cancel during download, extraction, validation and completion, plus repeated start/abort and shutdown.

Implementation verified: extraction interruption is checked before and after archive work; downloader cleanup requests interruption and defers worker/file destruction until `QThread::finished`, with a completion-once guard. Package validation runs in that same worker before success/publication and receives its interruption predicate. UI Abort/Escape/close paths are cancellation-safe. The sanitizer fixtures cover pending-download cancel, chunked download and size/write failures, repeated start/abort, extraction and validation cancel, a deterministic interruption-vs-successful-validation-return race with no success emission, and destruction of the real ownership tree (host plus child `AeqPackageManager`) during extraction. The production manager regression confirms rejection and preservation of the previous database; the original host-owned dialog UAF is fixed with a guarded `QPointer` after `exec()` returns.

**Acceptance:** no deletion of a running thread, use-after-free, orphaned staging installation or false success; GUI remains responsive.

### T17 — Make CI verify the changed code and execute tests

**Files:** `.github/workflows/package-deb.yml`, top-level `JDSP4Linux.pro`, test project files, new CI regression workflow if separation is clearer, `docs/testing.md`.

- [x] Remove hard-coded `master` from source checkouts so each event builds its intended revision; record resolved source and recursive submodule SHAs in artifacts. Keep release metadata tied to that same revision.
- [x] Separate verification from publication and explicitly gate publishing to intended trusted events. Do not publish or modify secrets while validating this task.
- [x] Run config, native, script corpus and appropriate Qt tests, not only build them. Include PipeWire/PulseAudio and full/headless compile coverage using the repository's existing supported matrix.
- [x] Add focused ASan/UBSan jobs; keep TSan separate and document exclusions. Ensure a failing regression exits nonzero even in release-style test builds.
- [x] Validate event/revision behavior for PR, branch push and release with local workflow inspection or authorized CI. Use out-of-source build directories and preserve logs as artifacts.

Implementation in progress: package workflow source checkouts now use `${{ github.sha }}`; nightly publication is gated to pushes on `master`; a non-publishing native verification job builds and executes the Liveprog/rate regressions out of tree. Local workflow contracts now cover PR/master-push/dispatch/release triggers and publication gating. Hosted Actions execution and the full Qt/backend matrix remain open.

**Acceptance:** a deliberately failing regression fails verification on the actual PR revision; artifacts identify their inputs; no unrelated branch is substituted and verification cannot publish accidentally.

### T18 — Package exactly the selected binary and fail on staging errors

**Files:** `meta/flatpak/build-local-bundle.sh`, `meta/flatpak/LOCAL_BUILD.md`, `meta/build_deb_package.sh` for demonstrated failure handling, new shell packaging tests.

- [x] Promote the mocked `JAMESDSP_BINARY=/usr/bin/true` argument test into a maintained fixture using fake executables under build. Assert the selected file is the installed source and its precise directory gets read-only staging access.
- [x] Pass the selected binary explicitly to staging. Make every inner install/extract failure fatal; verify required executable, desktop file, icon and metadata before exporting.
- [x] Correct documented commands to work from the stated directory; the local bundle path from repository root is `../../build/jamesdsp/...`, not `../build/jamesdsp/...`. Prefer printing the absolute artifact path.
- [x] Check argument validation/fail-fast behavior in Debian packaging with missing input/invalid flavor, failed copy and staging/output collision fixtures. Keep all output under build and avoid partially successful packages.
- [ ] Record binary hash, build configuration, source/submodule identity, runtime and stable dependency-package identity. Test paths with spaces and custom output. Build a local candidate if dependencies permit; inspect contents and permissions without installation.

**Acceptance:** override input and packaged binary hashes match; failed staging cannot report a created usable bundle; existing stable/custom installations and permissions remain unchanged.

### T19 — Align EELVault validation with the native contract

**Files:** `/home/soloarch/Workspace/projects/eelvault/eel_parse.py`, `validate.py`, `test_eelvault.py`; shared golden lifecycle/metadata fixtures. This directory has no Git history; capture its baseline and agree its project ownership before edits.

- [x] Reproduce native acceptance/Python rejection of `@sample // run audio` before `@init`. Test CRLF, leading whitespace, duplicates, malformed suffixes and unsupported markers.
- [x] Align structural validation with the current native four-stage contract; do not pretend a regex validator establishes compilation or runtime safety.
- [x] Parse optional step and enum metadata consistently with T12. Add focused numeric/list optional-step and enum cases; cases from `depthsurround.eel` and `stereoPhaseInvert.eel` remain part of the broader fixture gap.
- [x] Run the same fixtures against native parsing and Python validation. Record intentional differences such as structural acceptance versus actual compiler failure.

**Acceptance:** native-valid lifecycle syntax is not rejected for the demonstrated formatting/order reasons; supported metadata is not silently lost. No installation is performed against the user's live EEL library during testing.

### T20 — Verify the integrated candidate and leave a resumable handoff

**Files:** this task board/log, `docs/testing.md`, task-specific test reports under build. No new product scope.

- [x] Fresh build and tests from the final isolated state; rerun all fixed reproductions, backend/full/headless matrix and EEL corpus. Explain any unavailable variant explicitly.
- [ ] Inspect diffs, submodule changes and source hashes. Verify that every change belongs to a task and pre-existing work remains intact.
- [ ] Inspect package identity and permissions. Prepare an exact runtime verification procedure for the custom branch, with temporary fixture paths and expected results.
- [ ] If runtime testing is authorized, verify installed binary hash/build identity before testing device/rate change, preset load/save, editor Run and error recovery. Otherwise record runtime verification pending; do not equate offline success with installed behavior.
- [ ] Summarize unresolved P1/P2 findings, known unsupported script fixtures, benchmark results, task IDs, files, commands and next safe action. Do not call the candidate release-ready with unresolved memory corruption, data loss or unverified required runtime behavior.

**Acceptance:** a new worker can resume immediately from this file, and the user can tell exactly what is fixed, what was tested, and what remains.

## 7. Review evidence and commands

### Commands already run by the reviewer

Routine inspection used `git status --short --branch`, `git log -5 --oneline`, `git remote -v`, `git submodule status`, `git diff --stat`, `git diff --check`, and `rg`/`nl`/`sed`/`cat` on the paths cited above. The main checkout has shallow/limited visible history: the log returned `eb848bf`. No fetch was needed for this local review.

| Check | Result |
|---|---|
| Fresh `src/tests/tests.pro` qmake/make/config_io_test | PASS; successful round trip only |
| Existing native liveprog_runtime_test | PASS; also passed with three custom scripts |
| Targeted current native objects/test rebuilt with ASan | PASS for existing runtime tests; not a full sanitized library build |
| ASan native low-rate probe | FAIL as expected: heap write overflow |
| Native sample-rate probe | FAIL semantically: VM rate stays 48000 after DSP becomes 44100 |
| Native force-refresh probe with timeout | FAIL as expected: timeout exit 124 |
| Fresh ASan Untar probe | FAIL as expected: heap-use-after-free at filename open |
| All 50 EEL compile/impulse probes | 48 pass, 2 non-shipping incompatibilities; numerical limits described in section 2 |
| gainControl, highpass200Hz, diagnostic probes | Reproduced F05, F09, F10 |
| EELVault check: 44 bundled + 3 custom | 47 accepted; 8 CRLF warnings |
| Four EELVault existing test functions invoked directly | PASS; `python3 -m pytest` unavailable (`No module named pytest`) |
| PipeWire/PulseAudio/theme shell contracts | All PASS; these only inspect source text/resources |
| Bash syntax checks for packaging scripts | PASS |
| Mock Flatpak build arguments with binary override | Reproduced wrong staged binary; all flatpak calls mocked, no real bundle produced |
| Main `git diff --check` | PASS before review documentation |
| Runtime identity read-only inspection | Custom Flatpak observed; no restart, playback, install or hash-equivalence claim |

Fresh config test command sequence (executed successfully):

```bash
mkdir -p /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/config
qmake6 /home/soloarch/Workspace/upstream/JDSP4Linux/src/tests/tests.pro -o /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/config/Makefile
make -C /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/config -j2
/home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/config/config_io_test
bash /home/soloarch/Workspace/upstream/JDSP4Linux/src/tests/pipewire_rt_contract_test.sh
bash /home/soloarch/Workspace/upstream/JDSP4Linux/src/tests/pulse_wrapper_contract_test.sh
bash /home/soloarch/Workspace/upstream/JDSP4Linux/src/tests/visual_theme_registry_test.sh
bash -n /home/soloarch/Workspace/upstream/JDSP4Linux/meta/flatpak/build-local-bundle.sh /home/soloarch/Workspace/upstream/JDSP4Linux/meta/build_deb_package.sh
```

Preserved native reproductions (offline, expected failure on review baseline):

```bash
ASAN_OPTIONS=detect_leaks=0 /home/soloarch/Workspace/build/audio-review-71LvJc/native_probe lowrate
ASAN_OPTIONS=detect_leaks=0 /home/soloarch/Workspace/build/audio-review-71LvJc/native_probe
ASAN_OPTIONS=detect_leaks=0 timeout 4 /home/soloarch/Workspace/build/audio-review-71LvJc/native_probe refresh
```

The corresponding C source and instrumented objects are beside the binary. Its essential regression sequence is:

```c
JamesDSPGlobalMemoryAllocation();
JamesDSPInit(dsp, 128, 48000);
JamesDSPSetSampleRate(dsp, 8000, 0);
/* in/out each contain 128 floats; dsp is an allocated JamesDSPLib. */
dsp->processFloatDeinterleaved(dsp, in, in, outL, outR, 128);
```

For the rate deadlock, call `JamesDSPSetSampleRate(dsp,44100,1)` after initialization instead. A regression must fail on the timeout, not swallow exit 124. For stale rate, load `@init\ninitial=srate;\n@sample\nseen=srate;\n`, enable, change to 44100 with flag 0, process, then compare `dsp->fs`, `*dsp->eel.vmFs`, and `seen`.

Preserved EEL probes:

```bash
/home/soloarch/Workspace/build/eel-review-EEze54/probe /home/soloarch/Workspace/upstream/JDSP4Linux/resources/assets/liveprog/gainControl.eel dB 0 gainLin
/home/soloarch/Workspace/build/eel-review-EEze54/probe /home/soloarch/Workspace/upstream/JDSP4Linux/resources/assets/liveprog/highpass200Hz.eel
/home/soloarch/Workspace/build/eel-review-EEze54/probe /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel gain 2 gain
```

`probe.c` is beside the executable; it loads one script, optionally mutates a variable, and processes 64 blocks × 256 frames at 48 kHz with a left-only impulse. Its observed gain/right-channel numbers are in the findings register. Rebuild it against the final library before using it for final acceptance.

ASan archive-lifetime reproduction (executed; compilation succeeded, execution failed with use-after-free):

```bash
g++ -g -O1 -fsanitize=address -fno-omit-frame-pointer /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/untar_probe.cpp -I/home/soloarch/Workspace/upstream/JDSP4Linux/src/subprojects/AutoEqIntegration $(pkg-config --cflags --libs Qt6Core libarchive) -o /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/untar_probe
ASAN_OPTIONS=detect_leaks=0 /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/untar_probe /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/untar-input.tar.gz /home/soloarch/Workspace/build/jamesdsp-review-2026-09-15/untar-output
```

The archive contains only the repository's public `resources/assets/default.conf`, created with `tar -czf .../untar-input.tar.gz -C /home/soloarch/Workspace/upstream/JDSP4Linux resources/assets/default.conf`; no user configuration was copied. The test calls `Untar::extract` directly using the supplied archive and output paths.

Validator baseline, run from `/home/soloarch/Workspace`:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m projects.eelvault.cli check projects/jamesdsp-liveprog upstream/JDSP4Linux/resources/assets/liveprog
```

## 8. Completion rules and work log

### Per-task verification checklist

- [ ] Current baseline and touched files recorded; no unexplained overlap.
- [ ] Original failure reproduced or convincingly disproved and recorded.
- [ ] Regression tests fail for the intended reason on the captured baseline.
- [ ] Implementation passes targeted tests and relevant integration checks.
- [ ] Diff reviewed; no unrelated formatting or user-work removal.
- [ ] Diagnostics and compatibility behavior checked, including failure paths.
- [ ] Exact commands/results and limitations logged; task board updated.

### Decisions register

| ID | Decision | Rationale/status |
|---|---|---|
| D01 | Review current dirty tree, not pristine upstream HEAD | The custom lifecycle/theme/runtime work is present only locally |
| D02 | Use offline tests before runtime changes | Running custom Flatpak belongs to the user; reproducible faults need no audio playback |
| D03 | Follow existing failed-reload preservation spec unless explicitly superseded | Current test/code contradict it; T05 must resolve the mismatch openly |
| D04 | Preserve legacy script reload semantics | Bulk converting all scripts is unnecessary to fix broken controls |
| D05 | No automatic publishing/installing/committing | Review handoff does not authorize external delivery or inclusion of unrelated work |

### Append-only session entry template

Copy this block for each session and fill every field. Use `not run — reason` rather than omitting a verification result.

```text
Date/time and worker:
Task ID/status before → after:
Repository/worktree/branch/HEAD and baseline manifest:
Intent and reproduced failure:
Files changed (separate parent/submodule/custom project):
Implementation and rationale:
Commands run, working directories, exit codes and key results:
Regression baseline result → fixed result:
Source/build/package/runtime identities tested:
User/other-agent files intentionally preserved:
Open risks, failures or blockers:
Decision IDs added/changed:
Next safe action and exact resume location:
Commit/PR reference, or not committed:
```

### Review session — 2026-09-15

- Worker: coordinating reviewer with independent audio, EEL and application source reviews.
- Task state: review and roadmap authored; **T00–T20 implementation remains unstarted**.
- Source change: added this Markdown file only. No commit, package installation, runtime configuration or application edit.
- Generated review files: config test objects/binary; `untar_probe.cpp`, its binary and safe archive/output directory under `build/jamesdsp-review-2026-09-15`; native ASan probe sources/objects/binaries under `build/audio-review-71LvJc`; EEL probe/fixture under `build/eel-review-EEze54`.
- Evidence: section 7 records passing baseline checks and deliberately failing probes. No full application rebuild, GUI test, archive traversal exploit, concurrent crossfeed stress or installed-runtime validation was performed.
- Preserved: all modifications/untracked files in section 3, detached editor changes, three custom EEL scripts, EELVault source, existing build/package and running audio processes.
- Next safe action: T00, capture the complete current baseline and establish an isolated implementation tree before changing overlapping files.

### Luna implementation session — 2026-09-15

- Worker: Codex implementation session.
- Task state: T00 READY → VERIFIED; T01–T20 remain QUEUED.
- Repository/worktree: `/home/soloarch/Workspace/upstream/JDSP4Linux-luna`, branch `luna/2026-09-15-roadmap`, HEAD `eb848bf507325ecbe765569d37c163d3b7c6fd11`; original shared checkout `/home/soloarch/Workspace/upstream/JDSP4Linux` on `master` was not modified by this session.
- Baseline preservation: copied all tracked modifications and untracked source/docs into the isolated worktree; initialized the editor submodule at `b2f392480e00ca232c397610f42688b165b87640` and restored its `src/model/codecontainer.h` modification; preserved the unborn custom EEL repository unchanged. Content/hash manifest: `/home/soloarch/Workspace/build/jamesdsp-luna-baseline-2026-09-15/baseline-manifest.txt`.
- Commands/results: `qmake6 <worktree>/JDSP4Linux.pro -o <build>/Makefile` PASS; initial aggregate `make -C <build> -j2` exposed a qmake parallel dependency race before the library completed (exit 2), then native test build PASS; `liveprog_runtime_test` PASS; fresh Qt `config_io_test` qmake/build/run PASS; PipeWire, PulseAudio, and visual-theme contract scripts PASS; Flatpak/DEB Bash syntax checks PASS; parent and editor `git diff --check` PASS.
- Build/test identity: out-of-tree build root `/home/soloarch/Workspace/build/jamesdsp-luna-baseline-2026-09-15`; no full GUI/backend runtime or package installation was attempted; existing JamesDSP processes were preserved.
- User/other-agent files intentionally preserved: all dirty source and untracked files in the shared checkout, running audio processes, existing build/package artifacts, and adjacent project repositories.
- Open risks: the reviewed baseline still contains F01–F24; the aggregate parallel qmake invocation is not a reliable baseline command until its dependency wiring is addressed by a later task or the documented serial/targeted sequence is used.
- Decision IDs: D01, D02, D05 reaffirmed; no new design decision.
- Next safe action: T01, add the ASan low-rate ASRC regression in `libjamesdsp/tests` and reproduce the captured overflow before changing production code.
- Commit/PR: not committed.

### Luna implementation session — continuation final checkpoint — 2026-09-20

- T10 is `VERIFIED`: fresh current-source ASan/UBSan and strict TSan builds of `crossfeed_lifecycle_test` both exited 0 with `crossfeed lifecycle test passed`; the fixture covers unchanged-enable identity, concurrent processing/toggle/mode replacement, active 44.1/48/96/48 kHz rate refresh, disable and shutdown.
- T13 remains `IN_PROGRESS`: fresh ASan/UBSan and strict TSan reload fixtures exited 0. `liveprog_callback_probe_test` passed with zero callback allocations/frees and control-side preparation allocations; `liveprog_reload_lock_test` reported `callback latency max=0ms p99=0ms during slow reload` at quantum 64/rate 48 kHz. The first attempted TSan build of the inherited project failed exactly with GCC `-fsanitize=thread is incompatible with -fsanitize=address`; dedicated `liveprog_reload_lock_tsan_test.pro` removed that project limitation and the runtime was clean. Live GStreamer/PipeWire/PulseAudio device-linked acceptance remains unavailable because GStreamer development headers/pkg-config entries are absent.
- T14 is `VERIFIED` from the recorded strict sanitizer corpus evidence: all 50 manifest outcomes, including two explicit expected rejects, stimuli/control/block/rate coverage, Airwindows behavioral checks and non-finite diagnostics passed.
- T19 is `VERIFIED`: `cd /home/soloarch/Workspace; PYTHONPATH=/home/soloarch/Workspace python3 projects/eelvault/run_regressions.py` exited 0; native comparison exited 0 for 47 entries with only the two documented intentional Python/native differences (`hpfloat.eel`, editor `demo.eel`).
- T17/T18/T20 remain `BLOCKED` on T13’s unavailable live backend/full-runtime evidence and hosted CI/package execution. Current repository contract command passed: workflow, packaging, PipeWire RT, PulseAudio wrapper, visual-theme and shell syntax checks; Python compilation and both repository `git diff --check` checks passed.
- Exact final integrity command: `git diff --check; git -C src/subprojects/EELEditor diff --check` -> exit 0. All build output remains under `/home/soloarch/Workspace/build/`. No install, publish, commit, Flatpak permission change, or running audio application restart occurred. Next action is the T13 backend/runtime boundary when the required development/runtime environment is available.
- Commit/PR: not committed.

### Luna implementation session — T19 fixture restoration and native/Python closure — 2026-09-20

- T19 dependency-free command: `cd /home/soloarch/Workspace; PYTHONPATH=/home/soloarch/Workspace python3 projects/eelvault/run_regressions.py` -> exit 0; output `EELVault dependency-free regression suite passed`. The previously missing `projects/jamesdsp-liveprog` fixtures were present; no fixture was fabricated or copied.
- Native comparison command: `python3 libjamesdsp/tests/compare_eel_manifest.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite/test/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite/eel-corpus-comparison-t19.json` -> exit 0; `manifest comparison passed: 47 files; python/native differences: 2`. Differences are explicitly recorded for non-shipping `libjamesdsp/subtree/Main/CLI/hpfloat.eel` and editor `src/subprojects/EELEditor/src/definitions/demo.eel`, where Python structural validation accepts input that the native compiler rejects.
- Syntax verification: `python3 -m py_compile libjamesdsp/tests/compare_eel_manifest.py /home/soloarch/Workspace/projects/eelvault/eel_parse.py /home/soloarch/Workspace/projects/eelvault/validate.py` -> exit 0. T19 is now `VERIFIED`; supported lifecycle and metadata fixtures agree without claiming structural validation is runtime/compiler equivalence. No installation or live EEL-library modification occurred.
- Next action: retain T13 `IN_PROGRESS`; T17/T18/T20 remain blocked by that incomplete ownership design and unavailable hosted/full runtime/package acceptance.
- Commit/PR: not committed.

### Luna implementation session — T13 measured slow-reload follow-up — 2026-09-20

- The reload fixture now measures 64 callback samples during a deliberately delayed candidate load and reports max/p99 latency. Exact ASan/UBSan command: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/libjamesdsp; qmake6 libjamesdsp/tests/liveprog_reload_lock_test.pro 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/tests/liveprog_reload_lock_test.Makefile"; make -C "$root/tests" -f liveprog_reload_lock_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/liveprog_reload_lock_test"` -> exit 0; `callback latency max=0ms p99=0ms during slow reload`.
- Exact TSan command: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-tsan/libjamesdsp; qmake6 libjamesdsp/tests/liveprog_reload_lock_tsan_test.pro 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/tests/liveprog_reload_lock_tsan_test.Makefile"; make -C "$root/tests" -f liveprog_reload_lock_tsan_test.Makefile -B -j2; setarch "$(uname -m)" -R env TSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/liveprog_reload_lock_tsan_test"` -> exit 0; same `callback latency max=0ms p99=0ms during slow reload`; no TSan diagnostic. The dedicated TSan `.pro` is required because the original test project hard-enables ASan/UBSan, and GCC rejects combined ThreadSanitizer/AddressSanitizer flags.
- T13 evidence now covers candidate compilation outside the processing mutex, serialized compiler access, stale-rate rejection, parallel-load serialization, callback allocation count (zero), measured callback latency at quantum 64/rate 48 kHz, and control-side allocation during quantum preparation. T13 remains `IN_PROGRESS` because live PipeWire/PulseAudio runtime/device transitions and a full production backend-linked build are unavailable here; the GStreamer development packages are absent (`gst/gst.h` and `gstreamer-1.0` pkg-config entries missing). No completion claim is made from the offline fixtures alone.
- T17/T18/T20 remain blocked by this T13 boundary and hosted/full-runtime acceptance. No install, publish, commit, Flatpak permission change, or running audio application launch/restart occurred.
- Commit/PR: not committed.

### Luna implementation session — T01 checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Task state: T01 QUEUED → IN_PROGRESS; overflow correction is implemented and verified for the exercised paths, but the task remains open for invalid-rate and allocation-failure transaction handling.
- Reproduced failure: new `asrc_capacity_test` built with an ASan-instrumented native library failed on the preserved layout with `heap-buffer-overflow` in `psrc_filt_stereo`, reached through `DoASRC_bwd`; the allocation site was `JamesDSPSetSampleRate`.
- Files changed in the isolated parent repository: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`; new tests `libjamesdsp/tests/asrc_capacity_test.c` and `libjamesdsp/tests/asrc_capacity_test.pro`. Shared `master` checkout remains untouched.
- Implementation: added checked shared buffer-layout calculation/installation used by initialization, block growth, and sample-rate refresh; workspace buffers conservatively cover both conversion directions and the ring buffer; replacement allocation occurs before freeing the previous buffer.
- Red command/result: qmake `CONFIG+=DEBUG_ASAN` native library and ASan test, then `ASAN_OPTIONS=detect_leaks=0 .../asrc_capacity_test`; baseline failed with the sanitizer overflow described above.
- Green commands/results: rebuilt the ASan target after the change and ran the same test across 9 rates (8–192 kHz), 6 frame sizes (1–4096), rate transitions, growth/shrinking, finite output and stereo inputs: `ASRC capacity test passed`; rebuilt normal native library and ran `liveprog_runtime_test`: `liveprog runtime test passed`; `git diff --check`: PASS.
- Open risks: invalid/unsupported sample-rate validation and failure-state propagation are not complete; the test currently exercises ASan, not a combined ASan+UBSan native library; no GUI/audio runtime action performed.
- Next safe action: complete T01 failure handling and UBSan coverage, then mark T01 VERIFIED; exact source resume location is `calculate_buffer_layout`, `allocate_buffer_layout`, and the three callers in `jdspController.c`.
- Commit/PR: not committed.

### Luna implementation session — T01 validation checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Task state: T01 remains IN_PROGRESS; validation is complete for invalid inputs and sanitizer coverage, while forced valid-allocation failure propagation remains open.
- Regression extension: `asrc_capacity_test` now asserts that zero, NaN, infinity, 7,999 Hz, and 192,001 Hz leave the active 48 kHz state and buffer unchanged; zero and `SIZE_MAX` block requests likewise leave the current allocation unchanged.
- Red evidence: before validation, the same ASan/UBSan test aborted with `AddressSanitizer: FPE` in `psrc_generate` during `JamesDSPSetSampleRate(0.0f, 0)`.
- Green evidence: after validation, `qmake6 .../libjamesdsp.pro CONFIG+=DEBUG_ASAN`, `qmake6 .../asrc_capacity_test.pro CONFIG+=DEBUG_ASAN`, both builds, and `ASAN_OPTIONS=detect_leaks=0 .../asrc_capacity_test` passed with ASan, UBSan, and float-divide-by-zero instrumentation; normal `liveprog_runtime_test` also passed.
- Files changed: `jdspController.c` and `asrc_capacity_test.c`; shared `master` checkout remains untouched.
- Open risk: none identified within T01 acceptance scope; the deterministic test seam covers replacement failure and inactive initialization, while allocator exhaustion remains an environmental condition.
- Next safe action: T02, fix AutoEQ archive filename lifetime and error-path cleanup.
- Commit/PR: not committed.

### Luna implementation session — T01 completion — 2026-09-15

- Worker: Codex implementation session.
- Task state: T01 IN_PROGRESS → VERIFIED.
- Files changed: `jdspController.c`, `jdsp_header.h`, `libjamesdsp/tests/asrc_capacity_test.c`, and `libjamesdsp/tests/asrc_capacity_test.pro`.
- Deterministic failure evidence: `JamesDSPSetBufferAllocationFailureForTests(1)` rejects a valid replacement before changing buffer, sample-rate, `fs`, or ASRC state; the test processes the previous 48→8 kHz state successfully after failure. The same seam verifies a failed initial allocation leaves a safely inactive instance with null audio buffers.
- Verification: ASan/UBSan/float-divide-by-zero build and regression passed (`ASRC capacity test passed`); normal native rebuild and `liveprog_runtime_test` passed; `git diff --check` passed.
- Open risks: no T01 acceptance gaps identified; no runtime audio process was restarted.
- Next safe action: T02, reproduce and fix AutoEQ filename lifetime.
- Commit/PR: not committed.

### Luna implementation session — T01 current-state sanitizer refresh — 2026-09-15

- Worker: Codex implementation session.
- Verification: rebuilt the hook-enabled current ASan native library and test with `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp -j2 && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests -j2`; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test` exited 0 with `ASRC capacity test passed`.
- This refresh includes the later spectral-interpolator fix and confirms the T01 deterministic allocation-failure seam still passes; no runtime audio process was restarted.

### Luna implementation session — T02 completion — 2026-09-15

- Worker: Codex implementation session.
- Task state: T02 QUEUED → VERIFIED.
- Reproduced failure: new `src/tests/untar_test.cpp` with a generated tar.gz failed under ASan on the baseline `Untar.h` with heap-use-after-free in libarchive `strlen` during `archive_read_open_filename`; the freed allocation was the temporary `qFilename.toUtf8()` buffer.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h`, new `src/tests/untar_test.cpp`, new `src/tests/untar_test.pro`.
- Implementation: owns the UTF-8 filename in a local `QByteArray`, keeps the derived output pathname bytes alive through `archive_entry_set_pathname`, and centralizes reader/writer close/free cleanup for all error and success returns. Archive path containment remains deferred to T08.
- Verification: `qmake6 .../src/tests/untar_test.pro CONFIG+=DEBUG_ASAN`, `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -j2`, `ASAN_OPTIONS=detect_leaks=0 .../untar_test`, and `ASAN_OPTIONS=detect_leaks=1 .../untar_test` all exited 0. Test covered valid archive extraction, missing archive with nonempty error, and a non-ASCII archive pathname; `git diff --check` passed.
- Open risks: no T02 acceptance gaps identified; test uses local `tar` to generate the fixture and does not close archive containment/security risk F11.
- Next safe action: T03, reproduce and repair destructive/self-copy preset and bookmark operations.
- Commit/PR: not committed.

### Luna implementation session — T03/T04 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T03 QUEUED → IN_PROGRESS; T04 QUEUED → IN_PROGRESS.
- T03 files changed: `src/data/SafeFileOperations.{h,cpp}`, `src/data/PresetManager.{h,cpp}`, `src/interface/FileSelectionWidget.cpp`, `src/src.pro`, new `src/tests/preset_file_operations_test.cpp` and target. The focused operation regression passed; `git diff --check` passed. Full app compilation was attempted and reached the existing absent `LiquidEqualizerWidget.h`/`.pri` subproject dependency; no submodule or packaging setup was changed.
- T04 files changed: native `Effects/liveprogWrapper.c` and `libjamesdsp/tests/liveprog_runtime_test.c`. Red evidence: the new legacy gain assertion failed because `LiveProgSetVariable` returned success while `gainLin` remained at the `@init`-derived value. Green evidence: `liveprog runtime test passed` after making no-`@slider` programs request reload, while existing `@slider` updates remain direct.
- Exact T04 verification: normal native rebuild plus `/home/soloarch/Workspace/build/jamesdsp-luna-t04/libjamesdsp/tests/liveprog_runtime_test` exited 0; ASan/UBSan native rebuild and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test` exited 0.
- Open acceptance work: T03 still needs direct PresetManager/UI signal coverage and rename/import assertions; T04 still needs Qt-level selected/persisted/reset/list/disabled coverage. Keep both IN_PROGRESS.
- Follow-up T03 hardening: `PresetManager::remove` now rejects unsafe names before constructing a filesystem path, and route-autoload rules reject unsafe preset names before loading. `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -j2 PresetManager.o` exited 0; `git diff --check` exited 0. Direct GUI/signal and import/rename tests remain open.
- Next safe action: T05, preserve the last valid Liveprog program and enabled state across failed reloads, with safe inactive initialization only when no valid program exists.
- Commit/PR: not committed.

### Luna implementation session — T05 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T05 QUEUED → IN_PROGRESS.
- Red evidence: after extending `liveprog_runtime_test.c` with the written last-good contract, the pre-change parser failed at `assert(jdsp->liveprogEnabled)` because candidate compile failure forcibly cleared the enabled flag.
- Files changed: native `Effects/liveprogWrapper.c`, `src/audio/base/DspHost.cpp`, and `libjamesdsp/tests/liveprog_runtime_test.c`.
- Implementation: failed candidate parsing no longer clears `liveprogEnabled`; missing/unreadable host source paths report diagnostics while leaving the current program untouched. Valid A → invalid candidate → continued A processing → valid recovery is covered.
- Verification: normal `/home/soloarch/Workspace/build/jamesdsp-luna-t04/libjamesdsp/tests/liveprog_runtime_test` exited 0; host `DspHost.o` compilation exited 0; ASan/UBSan rebuild and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test` exited 0.
- Open acceptance work: direct host-level missing/unreadable source tests, explicit-disabled reload coverage, section-order/CRLF/optional-section cases, and safe inactive initial failure remain. Keep T05 IN_PROGRESS.
- Next safe action: T06, config defaults and bounded EQ input, independent of the open host-level T05 tests.
- Commit/PR: not committed.

### Luna implementation session — T06/T07 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T06 QUEUED → IN_PROGRESS; T07 QUEUED → IN_PROGRESS.
- T06 files changed: `src/data/PresetProvider.cpp`, `src/MainWindow.cpp`, new `src/tests/eq_preset_validation_test.cpp` and target. Red evidence: short EQ input caused a segmentation fault in `EQ::reverseLookup`; green evidence: empty, short, non-finite and valid 15-band vectors pass after size/finite validation. Load-side EQ gain indexing now bounds-checks and numeric conversion rejects non-finite values. The focused `eq_preset_validation_test` exited 0. Full `MainWindow.o` compilation remains blocked by the existing absent `GraphicEQFilterGUI.h` subproject include.
- T07 files changed: `resources/assets/liveprog/highpass200Hz.eel` and `libjamesdsp/tests/liveprog_runtime_test.c`. Red evidence: bundled high-pass left impulse produced nonzero right-channel output when one shared state object was used. Green evidence: separate left/right filter states pass the exact-zero untouched-channel assertion at the bundled script’s native runtime test; normal and ASan/UBSan runs both exited 0.
- Open acceptance work: T06 still needs config-default existence semantics and offscreen load/IPC fixtures; T07 still needs 44.1 kHz comparison and the separately owned custom diagnostic fixture. Keep both IN_PROGRESS.
- Follow-up T06: `DspConfig::get` now tracks explicit-key existence independently of fallback selection, so omitted `exists` callers receive defaults and callers requesting existence can distinguish persisted values. `DspConfig` now owns and destroys its containers/watcher safely, including the disabled-watcher case. New `dsp_config_validation_test` covers default fallback, explicit values and no-default reads. `qmake6 src/tests/dsp_config_validation_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t06-config clean && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t06-config -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'` completed, and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/dsp_config_validation_test` exited 0 with no leak report. Offscreen load/IPC fixtures remain open.
- Next safe action: T08, extend AutoEQ extraction tests with containment and transactional staging without touching the live database.
- Commit/PR: not committed.

### Luna implementation session — T08 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T08 QUEUED → IN_PROGRESS.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h` and `src/tests/untar_test.cpp`.
- Implementation: normalized archive member names are checked before destination path construction; absolute/traversal paths, symlinks, hardlinks and special files are rejected. No process-wide working directory or live database was touched.
- Verification: `qmake6 .../src/tests/untar_test.pro CONFIG+=sanitize_address CONFIG+=sanitize_undefined`, `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08 -j2`, and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .../untar_test` exited 0. The test covered valid extraction, Unicode archive filename, missing archive diagnostic and generated `../escape` member rejection.
- Open acceptance work: extraction staging/publication, link-target coverage, truncation/size limits, JSON completeness validation, cancellation and exactly-once result signaling remain. Keep T08 IN_PROGRESS.
- Follow-up T08 lifetime hardening: `GzipDownloader::cleanup` now requests interruption and returns before closing/removing an archive whenever `extractThread` exists; the thread-finished handler remains the sole post-worker removal point. `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -j2 GzipDownloader.o` exited 0 and `git diff --check` exited 0. Staging/publication and end-to-end cancellation tests remain open.
- Follow-up T08 transaction: `AeqPackageManager::installPackage` now extracts into a unique sibling `QTemporaryDir`, requires nonempty valid JSON arrays in `version.json` and `index.json`, and publishes with same-parent rename plus rollback if replacement fails. The old database is untouched until validation succeeds. `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -j2 AeqPackageManager.o` exited 0 (only pre-existing qtpromise maybe-uninitialized warnings); `git diff --check` exited 0. End-to-end fake network/dialog and size-limit tests remain open.
- Follow-up T08 local-read validation: `isPackageInstalled`, `getLocalVersion`, and `getLocalIndex` now reject malformed/non-array JSON with single early-return paths instead of resolving/rejecting after invalid input. The same `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -j2 AeqPackageManager.o` command exited 0 with only the pre-existing qtpromise warnings; `git diff --check` exited 0.
- Next safe action: T09, exercise coherent device/internal sample-rate transitions using the T01 allocation guarantees.
- Commit/PR: not committed.

### Luna implementation session — T09 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T09 QUEUED → IN_PROGRESS.
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, new `libjamesdsp/tests/rate_transition_test.c` and target.
- Implementation: active EEL `srate` follows effective internal `fs`; the rate transition no longer holds its mutex while invoking refresh helpers with mixed lock ownership. Invalid rates retain prior state.
- Verification: normal library rebuild, `timeout 5 /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/rate_transition_test`, and output `rate transition test passed` exited 0. Coverage includes 48↔44.1, 44.1→96 internal-rate selection, invalid-rate rejection and `forceRefresh=1` bounded completion.
- Current-state rerun: after the spectral interpolator correction, normal `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp -j2`, test rebuild and `timeout 10 /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/rate_transition_test` passed. The first corpus invocation in that directory failed because `liveprog_runtime_test` had not yet been built; after generating `liveprog.Makefile` from `tests.pro`, the 50-file runner completed with `native EEL corpus expectations passed: 50 files`.
- Open acceptance work: ASan/UBSan rate run, backend contract/integration coverage, effects-on/off output checks and stronger atomicity/state assertions remain. Keep T09 IN_PROGRESS.
- Next safe action: T10, audit crossfeed state replacement for the same refresh/ownership hazards.
- Commit/PR: not committed.

### Luna implementation session — T10 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T10 QUEUED → IN_PROGRESS.
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/crossfeed.c` and `libjamesdsp/tests/rate_transition_test.c`.
- Implementation: corrected the mutually exclusive long-convolver refresh predicate; repeated unchanged enables now retain the selected convolver object.
- Verification: native rebuild and `timeout 5 /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/rate_transition_test` produced `rate transition test passed` and exited 0.
- Open acceptance work: controlled concurrent processing/enable stress, ASan/TSan race evidence, allocation/destruction instrumentation and final reader reclamation policy remain. Keep T10 IN_PROGRESS.
- Next safe action: T11, atomic EEL source saves and truthful persistence results, after T04’s legacy-control path.
- Commit/PR: not committed.

### Luna implementation session — T11/T12 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T11 QUEUED → IN_PROGRESS; T12 QUEUED → IN_PROGRESS.
- T11 files changed: editor submodule `src/model/codecontainer.h`, `src/eeleditor.cpp`, parent `src/data/EelParser.cpp`, and new `src/tests/codecontainer_save_test.cpp`/target.
- T11 implementation: atomic `QSaveFile` writes reject stream/flush/commit failures; Save, Save As and Run no longer emit success/execution signals after failed persistence; Save As changes the active path only after commit.
- Verification: sanitizer build and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test` exited 0, covering Unicode normal save and failed destination preservation. Standalone editor compile without `HAS_JDSP_DRIVER` exposed an existing conditional-build issue at `eeleditor.cpp:353`; parent build configuration supplies that define and remains affected by missing unrelated subproject headers.
- T12 is now opened for metadata/parameter regression work; no T12 acceptance claim yet.
- Open acceptance work: editor dirty-state/UI signal tests, commit-failure seam, parser propagation and T12 precision/identifier-boundary tests remain. Keep T11/T12 IN_PROGRESS.
- Next safe action: implement T12 quantization and precise source replacement tests.
- Commit/PR: not committed.

### Luna implementation session — T12 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T12 QUEUED → IN_PROGRESS.
- Files changed: `src/data/EelParser.h`, `src/data/EelParser.cpp`, new `src/tests/eel_parser_test.cpp` and target.
- Implementation: numeric properties quantize from the declared minimum by positive step; persistence uses precision derived from that step. Assignment matching now escapes identifiers, anchors assignments to source lines, and avoids suffix/comment rewrites.
- Verification: sanitizer-configured parser target built and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t12/eel_parser_test` exited 0. Fixture covered `gain` versus `pregain`, commented assignment, tab indentation, 0.001 step and source preservation.
- Open acceptance work: integer/fractional/list metadata matrix, CRLF and duplicate metadata, nonzero minimum quantization, malformed metadata diagnostics, and UI/VM round-trip coverage remain. Keep T12 IN_PROGRESS.
- Next safe action: continue T13/T15 host/editor boundary work while preserving T11/T12 test evidence.
- Commit/PR: not committed.

### Luna implementation session — T15 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T15 QUEUED → IN_PROGRESS.
- Files changed: parent `src/MainWindow.cpp` and roadmap; editor Save/Save As/Run behavior is covered by the T11 submodule changes.
- Implementation: connected `EELEditor::executionRequested(QString)` to `LiveprogSelectionWidget::updateFromEelEditor(QString)`, ensuring Run’s successfully saved current path enters the existing reload route once.
- Verification: source inspection confirms one `executionRequested` consumer and one reload route; T11’s atomic-save sanitizer regression remains green. Full parent compile is still limited by absent `GraphicEQFilterGUI.h` subproject files.
- Open acceptance work: offscreen signal-spy, invalid/unsaved Run, Save As cancellation, fast tab-switch synchronization and shutdown tests remain. Keep T15 IN_PROGRESS.
- Next safe action: T16, make AutoEQ cancellation ownership and completion signaling safe.
- Commit/PR: not committed.

### Luna implementation session — T16 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T16 QUEUED → IN_PROGRESS.
- Files changed: `src/subprojects/AutoEqIntegration/ExtractionThread.h` and `GzipDownloader.{h,cpp}`.
- Implementation: cancellation checks now stop extraction before/after archive work; downloader cleanup no longer deletes a running thread or removes its input before completion. Worker/file cleanup is deferred to `QThread::finished`; completion signals are guarded against duplicates.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -j2 GzipDownloader.o` exited 0. Full GUI build remains limited by pre-existing absent subproject headers.
- Open acceptance work: slow extraction cancellation, download cancellation, dialog close/parent destruction, repeated abort and exactly-once end-to-end tests remain. Keep T16 IN_PROGRESS.
- Next safe action: inspect T17 CI revision/publishing workflow and add local static workflow contracts.
- Commit/PR: not committed.

### Luna implementation session — T17 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T17 QUEUED → IN_PROGRESS.
- Files changed: `.github/workflows/package-deb.yml`.
- Implementation: all source checkouts use `${{ github.sha }}`; nightly SFTP publication is restricted to trusted pushes on `master`; a separate non-publishing `verify-native` job builds and executes native regressions from `build/`.
- Verification: workflow inspection found no remaining literal source checkout `ref: 'master'`; `git diff --check` exited 0. CI execution is not available in this local session.
- Open acceptance work: full/headless/backend matrix, focused ASan/UBSan/TSan jobs, artifact source identity and deliberate failing-regression validation remain. Keep T17 IN_PROGRESS.
- Additional CI contract: `package-deb.yml` now records `git rev-parse HEAD` and recursive submodule SHAs as an artifact in `verify-native`, and adds a non-publishing `verify-native-sanitizers` job with explicit ASan/UBSan link flags running ASRC, Liveprog and rate regressions. Local Python assertions confirmed the workflow contains the SHA checkout references, identity capture and sanitizer job; actual GitHub Actions execution remains unavailable locally.
- Next safe action: T18, verify packaging scripts select the requested binary and fail closed on staging errors.
- Commit/PR: not committed.

### Luna implementation session — T18 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T18 QUEUED → IN_PROGRESS.
- Files changed: `meta/flatpak/build-local-bundle.sh`, `meta/flatpak/LOCAL_BUILD.md`, `meta/build_deb_package.sh`, and new `meta/tests/flatpak`/`meta/tests/packaging_contract_test.sh`.
- Implementation: the Flatpak helper now canonicalizes and grants read-only access to the selected executable directory, stages the selected `$binary`, and uses `set -eu` inside the staging shell. The Debian helper now validates its two arguments, uses quoted paths, rejects unknown flavors before staging, and fails on copy/metadata/ownership/package-build errors.
- Verification: `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18 /home/soloarch/Workspace/upstream/JDSP4Linux-luna/meta/tests/packaging_contract_test.sh` exited 0 with `packaging contract test passed`; the test ran `bash -n`, validated the custom `/usr/bin/true` binary and exact filesystem grant, and confirmed a simulated Flatpak staging failure propagates. `git diff --check` exited 0. No package was installed, published, or bundled with a real runtime.
- Follow-up: the Flatpak staging shell now tests executable, desktop, icon, license and metainfo artifacts after extraction and before export; the same packaging contract test was rerun and exited 0.
- Open acceptance work: T17 CI execution and the real Flatpak/DEB runtime matrix remain unavailable or intentionally not run in this session; T18 remains IN_PROGRESS pending those checks.
- Next safe action: continue T19 EELVault/native syntax agreement and add source-level parser agreement regressions.
- Commit/PR: not committed.

### Luna implementation session — T19 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T19 QUEUED → IN_PROGRESS.
- Files changed in the separately owned no-history EELVault project: `/home/soloarch/Workspace/projects/eelvault/eel_parse.py` and `test_eelvault.py`.
- Implementation: section parsing now follows the native four-stage marker boundary, accepting leading whitespace and `//` suffix comments while rejecting executable suffixes; slider metadata accepts omitted steps and `{A, B, ...}` choice lists, preserving numeric validation and CRLF reporting.
- Verification: native ASan/UBSan `liveprog_runtime_test` passed for `resources/assets/liveprog/depthsurround.eel` and `stereoPhaseInvert.eel`. Dependency-free Python assertions passed for CRLF, `@sample // run audio`, omitted-step metadata, enum metadata, malformed markers and duplicate markers. `python3 -m py_compile` passed for the parser, validator and maintained tests. `python3 -m pytest -q .../test_eelvault.py` was attempted and could not run because pytest is not installed.
- Open acceptance work: full shared fixture sweep and native/Python result comparison remain; no EEL installation was performed.
- Next safe action: run integrated T20 checks from the isolated tree, preserving unavailable GUI/backend/package variants explicitly.
- Commit/PR: not committed.

### Luna implementation session — T14 progress — 2026-09-15

- Worker: Codex implementation session.
- Task state: T14 QUEUED → IN_PROGRESS.
- Files changed: `resources/assets/liveprog/stftDenoise.eel` and `libjamesdsp/tests/liveprog_runtime_test.c`.
- Implementation: corrected the denoiser’s undefined `sr` input to the host-registered `srate`; the corpus smoke test now checks the effective 48 kHz host rate and the resulting `nes1.a` smoothing coefficient is finite and strictly between zero and one.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests -j2 liveprog_runtime_test LFLAGS='-fsanitize=address -fsanitize=undefined'` exited 0; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 10 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/upstream/JDSP4Linux-luna/resources/assets/liveprog/stftDenoise.eel` printed `liveprog runtime test passed` and exited 0. The prior link attempt without explicit sanitizer `LFLAGS` failed with unresolved ASan symbols; this is recorded as build-command plumbing, not a product regression.
- Open acceptance work: the 50-script manifest, seeded signal corpus, rate/block/control sweeps, Airwindows behavioral checks and pre-sanitization diagnostics remain.
- Additional verification: added `libjamesdsp/tests/eel_corpus_manifest.json` and `run_eel_corpus.py`; `python3 libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t04/libjamesdsp/tests/liveprog_runtime_test` exited 0 with `native EEL corpus expectations passed: 50 files`. The manifest records 47 expected-valid files and two intentional native rejections (`hpfloat.eel`, editor `demo.eel`) plus the three custom expected-valid files.
- Sanitizer verification: `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 python3 libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test` completed with `native EEL corpus expectations passed: 50 files`; ASan/UBSan reported no memory or undefined-behavior failure. The two expected parser/compiler rejections were observed and classified by the runner.
- Cross-task sanitizer discovery/fix: a genuinely sanitizer-linked T09 rate test initially found a heap-buffer-overflow in `InitSpectralInterpolator` during `CompressorSetParam` at the 48→44.1 kHz transition. Root cause was a scratch `levels` allocation of `idxLen + 3` doubles followed by a final `levels[i + 1]` read; it is now `idxLen + 4`. After rebuilding the current ASan library and relinking, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 10 /home/soloarch/Workspace/build/jamesdsp-luna-t09-asan/libjamesdsp/tests/rate_transition_test` exited 0 with `rate transition test passed`.
- Verification note: a follow-up attempt to link `asrc_capacity_test` against the general T04 ASan library failed at link time because that library was not built with `JDSP_TEST_HOOKS` (`JamesDSPSetBufferAllocationFailureForTests` unresolved). The dedicated T01 sanitizer build remains the authoritative allocation-failure evidence; no passing result is claimed for this mismatched target.
- Open acceptance work: seeded signal corpus, rate/block/control sweeps, Airwindows behavioral checks and pre-sanitization diagnostics remain. Keep T14 IN_PROGRESS.
- Next safe action: preserve this correction and extend the offline corpus runner without changing the running application.
- Commit/PR: not committed.

### Luna implementation session — T14 behavioral corpus checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c`.
- Implementation: the native corpus executable now performs deterministic signal assertions for shipped `gainControl.eel` (−8 dB on both channels), `swapChannels.eel` (default unity-gain exchange), and `stereoPhaseInvert.eel` (default identity), in addition to the existing finite-output and denoiser/high-pass checks.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests -j2` after `qmake6 libjamesdsp/tests/tests.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/Makefile` exited 0; `python3 libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/liveprog_runtime_test` exited 0 with `native EEL corpus expectations passed: 50 files`.
- Sanitizer verification: `qmake6 libjamesdsp/tests/tests.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests -j2 CFLAGS='-fsanitize=address -fsanitize=undefined' CXXFLAGS='-fsanitize=address -fsanitize=undefined' LFLAGS='-fsanitize=address -fsanitize=undefined'` exited 0; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 python3 libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test` exited 0 and its captured log ended with `native EEL corpus expectations passed: 50 files`, with no sanitizer failure. The two expected parser/compiler rejections remained correctly classified.
- Open acceptance work: seeded signal corpus, rate/block/control sweeps, Airwindows behavioral checks, pre-sanitization diagnostics, direct UI/runtime coverage and remaining task criteria remain. Keep T14 IN_PROGRESS.
- Next safe action: continue direct preset/file-operation acceptance coverage without touching the running application.
- Commit/PR: not committed.

### Luna implementation session — T20 checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Task state: T20 IN_PROGRESS.
- Verification command: `set -o pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t03-red/preset_file_operations_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 5 /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t12/eel_parser_test; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-packaging meta/tests/packaging_contract_test.sh; git diff --check` exited 0 and printed `integrated offline regression sweep passed` (with expected missing-archive and failed-save diagnostics).
- Scope preserved: no install, publish, Flatpak permission change, commit, or running audio application restart was performed. Full GUI/backend matrix, real packaging runtime, pytest-dependent EELVault suite, T13 concurrency design, remaining T03–T12/T15–T19 acceptance items and final handoff criteria remain open.
- Next safe action: continue T03–T19 open acceptance work in dependency order, starting with the T14 corpus manifest/runner and T17 CI sanitizer/matrix contracts.
- Commit/PR: not committed.

### Luna implementation session — T13/T06 checkpoint — 2026-09-15

- Worker: Codex implementation session.
- T06 verification: added `dsp_config_validation_test`; after an initial sanitizer leak report exposed missing `DspConfig` ownership cleanup, the class now initializes its watcher to null and destroys the watcher/config containers. The rebuilt ASan/UBSan test exited 0 with no leak report.
- T13 files changed: new `docs/testing.md` records the current PipeWire format handoff and LiveProg candidate ownership boundary. The document explicitly identifies the remaining synchronous host compilation/retirement work and the required generation/block reclamation design; it does not claim hard real-time safety.
- T13 status: QUEUED → IN_PROGRESS. Current source contract remains green, but runtime allocation/scheduler instrumentation and compile-worker/reclamation implementation are still required.
- Contract verification: `src/tests/pipewire_rt_contract_test.sh && src/tests/pulse_wrapper_contract_test.sh && src/tests/visual_theme_registry_test.sh && git diff --check` exited 0, printing all three contract-pass messages.
- Next safe action: continue dependency-order acceptance work, prioritizing direct host/UI tests and bounded reload lifecycle evidence before any concurrency rewrite.
- Commit/PR: not committed.

### Luna implementation session — integrated regression checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification command: `set -o pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t03-red/preset_file_operations_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/upstream/JDSP4Linux-luna/resources/assets/liveprog/gainControl.eel /home/soloarch/Workspace/upstream/JDSP4Linux-luna/resources/assets/liveprog/swapChannels.eel /home/soloarch/Workspace/upstream/JDSP4Linux-luna/resources/assets/liveprog/stereoPhaseInvert.eel; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 5 /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t12/eel_parser_test; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-packaging meta/tests/packaging_contract_test.sh; git diff --check` exited 0 and printed `integrated offline regression sweep passed`.
- Observed expected diagnostics: the untar test reported its intentional missing-archive failure, and the save test reported its intentional missing-destination failure; no sanitizer or undefined-behavior failure occurred.
- Scope: no install, publish, Flatpak permission change, commit, or running audio application restart. GUI/backend/runtime packaging, T13 concurrency implementation, and remaining open acceptance criteria stay IN_PROGRESS.
- Next safe action: continue dependency-order acceptance coverage; do not mark open tasks VERIFIED without their full criteria.

### Luna implementation session — T10 crossfeed reclamation checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Task state: T10 remains IN_PROGRESS.
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c` and this roadmap log.
- Implementation: both native processing entry points now hold `m_in_processing` while invoking `CrossfeedProcess`. Crossfeed replacement already acquires that mutex before freeing/rebuilding convolution state, so processing and reclamation are serialized.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp -j2 && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests -j2 rate_transition_test` exited 0; normal `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 10 /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/rate_transition_test` printed `rate transition test passed`; rebuilt sanitizer-linked library/test with explicit ASan/UBSan flags and the equivalent `jamesdsp-luna-t04-asan` command also printed `rate transition test passed`; `git diff --check` exited 0.
- Open acceptance work: controlled concurrent processing/enable stress, TSan coverage and lifecycle/reclamation verification remain. Keep T10 IN_PROGRESS.
- Next safe action: add or run a bounded concurrent offline stress harness before considering T10 verification.
- Commit/PR: not committed.

### Luna implementation session — T10 concurrent stress checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/rate_transition_test.c` and this roadmap log.
- Implementation: the rate regression now starts a bounded pthread processing loop with crossfeed mode 2 (long-convolver path) while the control thread performs 32 forced crossfeed replacements. The test joins the processor before proceeding to sample-rate assertions.
- Verification: rebuilt normal and sanitizer-linked native libraries/tests; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/rate_transition_test` and the equivalent normal command both exited 0 with `rate transition test passed`; `git diff --check` exited 0. No ASan/UBSan failure occurred.
- Open acceptance work: TSan coverage, broader enable/disable/mode transitions and complete lifecycle/reclamation review remain. Keep T10 IN_PROGRESS.
- Next safe action: continue T10 transition stress coverage or return to the remaining direct T03–T08 acceptance tests in dependency order.
- Commit/PR: not committed.

### Luna implementation session — T10 TSan attempt — 2026-09-15

- Worker: Codex implementation session.
- Verification command: `mkdir -p /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp/tests && qmake6 libjamesdsp/libjamesdsp.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp/Makefile QMAKE_CFLAGS+=-fsanitize=thread QMAKE_CXXFLAGS+=-fsanitize=thread && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp -j2 && qmake6 libjamesdsp/tests/rate_transition_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp/tests/Makefile QMAKE_CFLAGS+=-fsanitize=thread QMAKE_CXXFLAGS+=-fsanitize=thread QMAKE_LFLAGS+=-fsanitize=thread && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp/tests -j2 && TSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp/tests/rate_transition_test` built successfully, but the executable terminated before the test with `FATAL: ThreadSanitizer: unexpected memory mapping 0x60352cda7000-0x60352cdb0000`.
- Interpretation: TSan is unavailable in this container/runtime configuration; this is not evidence of a product race and does not replace the successful normal and ASan/UBSan concurrent stress runs. Keep T10 IN_PROGRESS pending a runnable TSan environment and remaining lifecycle coverage.
- Scope preserved: no install, publish, permission change, commit, or running audio application restart.
- Next safe action: continue another independent acceptance slice while retaining the TSan limitation in the final gate.
- Commit/PR: not committed.

### Luna implementation session — T08 archive bounds checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Task state: T08 remains IN_PROGRESS.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h`, `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: extraction now rejects duplicate normalized members, negative/oversized member declarations, total extracted data beyond explicit 64 MiB/member and 512 MiB/package limits, and enables libarchive secure dot-dot/symlink handling. The test fixture now writes a real duplicate-entry archive through libarchive and creates a 65 MiB archive to exercise the limit, alongside traversal, Unicode and missing-archive cases.
- Verification: the initial build caught unavailable `ARCHIVE_EXTRACT_SECURE_NO_SYMLINKS`; it was corrected to the installed libarchive API’s `ARCHIVE_EXTRACT_SECURE_SYMLINKS`. `qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2` exited 0; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test` exited 0. The only output was the expected missing-archive diagnostic; duplicate and size-limit assertions passed with no ASan/UBSan failure.
- Open acceptance work: absolute/link-target fixtures, truncated content/header/finalization failures, download limits, staged package validation/publication and cancellation/end-to-end signal tests remain. Keep T08 IN_PROGRESS.
- Next safe action: extend transactional package tests around malformed JSON and publication rollback without touching the installed AutoEQ database.
- Commit/PR: not committed.

### Luna implementation session — T08 download-boundary checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloader.{h,cpp}` and this roadmap log.
- Implementation: downloads now reject a known `Content-Length` above 128 MiB, enforce the same cap while receiving chunks, require every write to complete, and emit a guarded error before cleanup on size/write failure. Null replies are rejected at start.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -j2 GzipDownloader.o` exited 0. The compile emitted only the existing qmake duplicate-resource recipe warnings; no new compiler error or warning was introduced by this change.
- Open acceptance work: fake-reply tests for short writes/oversized streams, truncated archive/header/finalization errors, staging validation/publication rollback and cancellation remain. Keep T08 IN_PROGRESS.
- Next safe action: add a deterministic fake `QNetworkReply` test for the download cap and exactly-once error behavior, if the existing Qt test infrastructure can host it without a full GUI build.
- Commit/PR: not committed.

### Luna implementation session — T08 downloader review checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: re-inspected `GzipDownloader` ownership and signal paths after the size/write change. Oversize or short-write paths set the completion guard, emit one error, abort the reply and remove the temporary archive; extraction completion remains guarded separately. `GzipDownloader.o` continues to compile in the parent qmake configuration.
- Acceptance boundary: no fake `QNetworkReply` event-loop harness was available in the existing focused tests, so exactly-once streamed-error behavior remains unverified rather than inferred from source inspection. Parent-destruction while extraction is still running also remains an explicit T16/T08 test requirement.
- Task state: T08 remains IN_PROGRESS; no task board promotion is claimed.
- Next safe action: continue T16/T08 cancellation ownership tests or another independent open acceptance item, preserving all builds under `build/`.
- Commit/PR: not committed.

### Luna implementation session — T08 downloader signal/ownership regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloader.h`, `src/subprojects/AutoEqIntegration/GzipDownloader.cpp`, new `src/tests/gzip_downloader_test.cpp` and `src/tests/gzip_downloader_test.pro`.
- Implementation: known oversized content lengths now emit the downloader error signal before returning false, so the signal-driven dialog cannot remain pending. The focused test uses a fake `QNetworkReply`, asserts abort plus exactly one size-limit error, and verifies a null reply is rejected without a duplicate error. The `QNetworkAccessManager` is now parented to `GzipDownloader`; this was found by LeakSanitizer in the new test and fixes the ownership leak.
- Red evidence: initial `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 .../gzip_downloader_test` reported a 632-byte Qt network-manager leak. The rebuilt test after parenting the manager exited 0 with leak detection enabled.
- Green verification: `qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile QMAKE_CXXFLAGS+=-fsanitize=address QMAKE_LFLAGS+=-fsanitize=address && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2` exited 0; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test` exited 0. `git diff --check` remains green from the preceding checkpoint.
- Open acceptance work: streamed short-write/oversize tests, truncated archive/finalization failures, staging validation/publication rollback, cancellation during extraction, and parent-destruction lifecycle tests remain. Keep T08 IN_PROGRESS.
- Next safe action: use the fake-reply seam to cover streamed data and cancellation, then integrate the target into the maintained test wiring.
- Commit/PR: not committed.

### Luna implementation session — T17 workflow wiring checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `.github/workflows/package-deb.yml`, new `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: the sanitizer job now generates separate qmake Makefiles for `tests.pro`, `asrc_capacity_test.pro`, and `rate_transition_test.pro`, builds each target with explicit sanitizer link flags, and invokes all three resulting executables. This removes the prior mismatch where `asrc_capacity_test` was invoked without being built.
- Verification: `python3 meta/tests/workflow_contract_test.py && python3 -m py_compile meta/tests/workflow_contract_test.py && git diff --check` exited 0 and printed `workflow contract test passed`.
- Open acceptance work: actual GitHub Actions execution, full/headless PipeWire/Pulse matrix, EEL corpus/config/Qt test wiring, deliberate failing-regression behavior and TSan documentation remain. Keep T17 IN_PROGRESS.
- Next safe action: inspect the full workflow for remaining missing test dependencies and ensure the new downloader test is included in a supported local/CI test path.
- Commit/PR: not committed.

### Luna implementation session — T17 maintained test sequence checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `.github/workflows/package-deb.yml`, `src/tests/gzip_downloader_test.pro`, new `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: the non-publishing native verification job now installs the dependencies needed by and runs workflow/packaging contracts, the ASan/UBSan untar regression and the downloader ownership/signal regression in an out-of-source `build/jamesdsp/qt-tests` directory.
- Verification: `python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t17-packaging meta/tests/packaging_contract_test.sh; qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t17-qt-tests/untar.Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t17-qt-tests -f untar.Makefile -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t17-qt-tests/gzip-downloader.Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t17-qt-tests -f gzip-downloader.Makefile -j2; /home/soloarch/Workspace/build/jamesdsp-luna-t17-qt-tests/gzip_downloader_test; git diff --check` exited 0 and printed `T17 local contract and Qt test sequence passed`. The expected missing-archive diagnostic appeared; all tests passed.
- Open acceptance work: actual GitHub Actions execution, full/headless PipeWire/Pulse matrix and deliberately failing CI regression validation remain unavailable locally. Keep T17 IN_PROGRESS.
- Next safe action: continue T18 package identity/permission verification and then refresh the integrated T20 evidence from the current source state.
- Commit/PR: not committed.

### Luna implementation session — current integrated checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification command: `set -o pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t12/eel_parser_test; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-packaging meta/tests/packaging_contract_test.sh; git diff --check` exited 0 and printed `current integrated T20 checkpoint passed`.
- Observed expected diagnostics: missing archive and failed destination save cases reported their intentional errors; no sanitizer or undefined-behavior failure occurred.
- Scope: no install, publish, Flatpak permission change, commit, or running audio application restart. T01/T02 remain VERIFIED; T03–T20 remain IN_PROGRESS where acceptance criteria are incomplete or unavailable.
- Next safe action: continue T18 artifact identity inspection and T16 cancellation coverage; do not call the candidate release-ready.
- Commit/PR: not committed.

### Luna implementation session — T16 cooperative extraction checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h`, `src/subprojects/AutoEqIntegration/ExtractionThread.h`, and this roadmap log.
- Implementation: `Untar::extract` now accepts an optional interruption callback and checks it between archive members and data blocks; `ExtractionThread` passes `isInterruptionRequested()`, reports `Extraction cancelled`, and retains its existing before/after checks. Existing callers remain source-compatible through the default callback.
- Verification: rebuilt the sanitizer untar target and ran `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test`; it exited 0 with only the expected missing-archive diagnostic. Rebuilt and ran the sanitizer downloader target; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test` exited 0. No ASan/UBSan failure occurred.
- Open acceptance work: deliberate slow extraction cancellation, dialog Escape/close/parent destruction, repeated abort and exactly-once end-to-end completion remain. Keep T16 IN_PROGRESS.
- Next safe action: add a slow disposable archive worker fixture to exercise interruption during data processing without the running application.
- Commit/PR: not committed.

### Luna implementation session — T16 deterministic cancellation checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: the sanitized archive test now invokes `Untar::extract` with a deterministic callback that permits header handling and interrupts at the first data-block check. It asserts the explicit cancellation result and confirms the callback was reached at least twice, exercising the same seam used by `ExtractionThread` without timing-dependent sleeps.
- Verification: `qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test` exited 0. Only the expected missing-archive diagnostic was printed; cancellation, traversal, duplicate, size-limit and Unicode assertions passed with no sanitizer failure.
- Open acceptance work: a real slow `QThread` cancellation, dialog Escape/close/parent destruction, repeated abort and exactly-once end-to-end completion remain. Keep T16 IN_PROGRESS.
- Next safe action: continue a separate direct lifecycle test or T18/T20 identity verification; do not claim GUI shutdown coverage from this deterministic callback test.
- Commit/PR: not committed.

### Luna implementation session — T18 binary identity checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `meta/flatpak/build-local-bundle.sh`, `meta/tests/packaging_contract_test.sh` and this roadmap log.
- Implementation: the local Flatpak helper now computes the selected executable’s SHA-256 before staging and prints it with the absolute binary path after bundle creation. The maintained fake-packaging contract compares this reported hash against `sha256sum /usr/bin/true` while retaining selected-path and fail-fast assertions.
- Verification: `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging meta/tests/packaging_contract_test.sh && git diff --check` exited 0 and printed `packaging contract test passed`.
- Open acceptance work: real bundle inspection, source/submodule/build identity capture in the artifact, path-with-spaces/custom-output cases and runtime/package matrix remain. Keep T18 IN_PROGRESS.
- Next safe action: continue offline artifact identity checks without installing or publishing a bundle.
- Commit/PR: not committed.

### Luna implementation session — T19 dependency-free parity checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed in the separately owned EELVault project: `/home/soloarch/Workspace/projects/eelvault/run_regressions.py`; roadmap log updated here.
- Implementation: added a dependency-free runner that supplies temporary `pathlib` fixtures and invokes every maintained EELVault test function, allowing the complete suite to run despite pytest not being installed. The runner adds the workspace root to `sys.path` and preserves the tests’ documented workspace-relative fixture paths.
- Verification: from `/home/soloarch/Workspace`, `python3 projects/eelvault/run_regressions.py && python3 -m py_compile projects/eelvault/eel_parse.py projects/eelvault/validate.py projects/eelvault/test_eelvault.py projects/eelvault/run_regressions.py` exited 0 and printed `EELVault dependency-free regression suite passed`; native `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/projects/jamesdsp-liveprog/awBaxandall.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/awInterstage.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel` exited 0 with `liveprog runtime test passed`.
- Comparison: all three custom scripts pass both the Python structural validator suite and the native ASan/UBSan runtime smoke path. This does not claim Python/native semantic equivalence for arbitrary DSP output; native compilation/runtime remains the authoritative behavioral check.
- Open acceptance work: shared 50-fixture native/Python result comparison, intentional structural/compiler differences, and no-install policy remain. Keep T19 IN_PROGRESS.
- Next safe action: add a machine-readable shared fixture comparison without installing EEL files.
- Commit/PR: not committed.

### Luna implementation session — T19 machine-readable comparison — 2026-09-15

- Worker: Codex implementation session.
- Files changed: new `libjamesdsp/tests/compare_eel_manifest.py`, new build artifact `/home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest-comparison.json`, and this roadmap log.
- Implementation: the comparison tool runs every manifest path through EELVault structural validation and the maintained native runtime executable, writes per-file Python errors/native status/expected status/role records as JSON, and reports structural-versus-native differences explicitly.
- Verification: `python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest-comparison.json` completed with `count=50` and `native_expected_pass=true`; JSON inspection found exactly two differences: `libjamesdsp/subtree/Main/CLI/hpfloat.eel` and `src/subprojects/EELEditor/src/definitions/demo.eel` are Python-structurally valid but native-rejected. `python3 -m py_compile libjamesdsp/tests/compare_eel_manifest.py && git diff --check` exited 0.
- Interpretation: the two differences are intentional non-shipping/unsupported-native fixtures already documented in the manifest; no EEL installation was performed. This comparison does not claim semantic DSP equivalence.
- Open acceptance work: T19 CI integration and broader native/Python semantic comparison remain. Keep T19 IN_PROGRESS.
- Next safe action: integrate the comparison artifact into the non-publishing CI job or document why the custom external fixture paths cannot run there.
- Commit/PR: not committed.

### Luna implementation session — T19 comparison portability checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/compare_eel_manifest.py` and this roadmap log.
- Implementation: the comparison tool now supports explicit `--skip-external` filtering for environments that lack the separately owned custom fixture tree. The filter is not used in the repository-local comparison, and the CI workflow was intentionally not wired to import the external EELVault package that is absent from CI checkouts.
- Verification: `python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest-comparison.json` produced JSON for 50 files with `native_expected_pass=true`; the two recorded structural/native differences remain the documented `hpfloat.eel` and editor demo. `python3 -m py_compile libjamesdsp/tests/compare_eel_manifest.py && git diff --check` exited 0.
- Acceptance boundary: CI runs the native corpus independently; the Python comparison remains a local artifact because EELVault is a separate no-history project and is not available in the CI checkout. No silent missing-fixture pass is introduced.
- Task state: T19 remains IN_PROGRESS.
- Next safe action: continue T20 source/build identity audit and retain CI’s explicit native-only boundary.
- Commit/PR: not committed.

### Luna implementation session — T07 lifecycle diagnostic regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log. The separately owned `liveprogLifecycleDiagnostic.eel` already exposes `slider1` and a valid `@slider` section; it was not rewritten.
- Implementation: the native runtime test now sets the custom diagnostic’s `slider1` to 2, asserts its derived `gain` becomes 2, processes signed stereo samples, verifies both outputs are scaled to ±2, and exercises the existing block/sample lifecycle path.
- Verification: after rebuilding the current sanitizer-linked native library and test, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/awBaxandall.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/awInterstage.eel` exited 0 with `liveprog runtime test passed`; `git diff --check` exited 0.
- Open acceptance work: direct UI-equivalent signal/state checks and 44.1 kHz response comparison remain. Keep T07 IN_PROGRESS.
- Next safe action: continue remaining direct UI acceptance checks without modifying unrelated custom-project files.
- Commit/PR: not committed.

### Luna implementation session — T20 identity audit checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: isolated worktree is `/home/soloarch/Workspace/upstream/JDSP4Linux-luna`, branch `luna/2026-09-15-roadmap`, base `HEAD eb848bf507325ecbe765569d37c163d3b7c6fd11`; recursive submodule status records EELEditor at `b2f392480e00ca232c397610f42688b165b87640` and the existing FlatTabWidget/GraphicEQWidget/LiquidEqualizerWidget entries as uninitialized (`-`), matching the known full-build limitation. `git diff --name-only` and status inspection found changes in the roadmap task areas plus maintained tests/docs; no running application files were touched.
- Artifact identity: SHA-256 was captured for `/home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test` (`faa8b0c67dbe8450217501caadb76236000228f7975db8f60a7c0674e5eb8872`), `t02-red/untar_test` (`a71c77a16c4846057354a936b5f9e12380ff4909d09fa935cde8250432ad18f5`), `t04-asan/liveprog_runtime_test` (`8a33ad3cd5952be4b970272ad84a4769199874955d4909ad8ca0362ee6127efc`), `t04-asan/rate_transition_test` (`6930f304d541d895fa7249624673879ce232c516a5a78543e6dde2270f975ef0`), and `t08-downloader/gzip_downloader_test` (`437b424325736c98b30f000cff324098a4716f4e30455e610412313d7281568f`). The audit command exited 0 with `identity audit commands passed`.
- Scope: hashes identify local test executables, not an installed or publishable application package. No install, publish, Flatpak permission change, commit, or audio application restart was performed.
- Open acceptance work: T20 fresh final-state build, complete GUI/backend/package matrix, and runtime identity verification remain unavailable or pending. Keep T20 IN_PROGRESS.
- Next safe action: continue the highest-risk open acceptance item, T13 reload/setup ownership, while retaining this identity record.
- Commit/PR: not committed.

### Luna implementation session — T20 maintained Qt test sweep — 2026-09-15

- Worker: Codex implementation session.
- Verification: an initial sweep attempted nonexistent `src/tests/config_io_test.pro` and recorded `config_io_test qmake=2 make=2 run=125`; all seven existing standalone targets subsequently built and ran under `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` with status 0: `dsp_config_validation_test`, `eel_parser_test`, `eq_preset_validation_test`, `preset_file_operations_test`, `untar_test`, `codecontainer_save_test`, and `gzip_downloader_test`. The maintained config target was then correctly generated from `src/tests/tests.pro`: `mkdir -p /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/config_io && qmake6 src/tests/tests.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/config_io/Makefile CONFIG+=HEADLESS && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/config_io -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/config_io/config_io_test`; it exited 0 with `config_io_status=0`; `git diff --check` exited 0.
- Interpretation: the missing `.pro` was a command-path error, not a product failure; all eight maintained Qt test executables now have current out-of-source sanitizer evidence. Full GUI/backend/package runtime tests remain outside this sweep.
- Task state: T20 remains IN_PROGRESS pending the full release-gate requirements and unresolved task acceptance criteria.
- Next safe action: consolidate remaining open acceptance criteria and continue T13/T15 runtime-independent coverage.
- Commit/PR: not committed.

### Luna implementation session — T08 header-failure handling — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h`, `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: an `archive_write_header` failure is now terminal, returns a useful error, closes/frees both archive handles and cannot report successful extraction. The test passes a regular file as the output path, forcing member-header failure, and asserts no member appears beneath that invalid path.
- Red evidence: the first missing-directory fixture was invalid because libarchive created the directory itself; it was replaced with a deterministic regular-file output target.
- Verification: `qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test` exited 0. Only the expected missing-archive diagnostic was printed; header-failure, cancellation, traversal, duplicate, size-limit and Unicode assertions passed with no sanitizer failure.
- Open acceptance work: link-target fixtures, truncated content/finalization failures, download-stream boundaries, transactional package publication and dialog lifecycle remain. Keep T08 IN_PROGRESS.
- Next safe action: continue the transactional AutoEQ package boundary without touching the installed database.
- Commit/PR: not committed.

### Luna implementation session — T08 link-target fixtures — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: the archive test now writes real libarchive symlink and hardlink entries whose targets escape the extraction root, then asserts both are rejected as unsafe and no outside target is created.
- Verification: `qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test` exited 0. Only the expected missing-archive diagnostic was printed; symlink/hardlink, header failure, cancellation, traversal, duplicate, size-limit and Unicode assertions passed with no sanitizer failure.
- Open acceptance work: truncated content/finalization failures, download-stream boundaries, transactional package publication and dialog lifecycle remain. Keep T08 IN_PROGRESS.
- Next safe action: add a truncated/corrupt archive fixture and verify extraction failure leaves staging contents controlled.
- Commit/PR: not committed.

### Luna implementation session — T08 truncated archive regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: the archive regression now copies a valid gzip archive into the disposable test directory, truncates it to half its original size, and asserts extraction fails with a nonempty error rather than reporting success.
- Verification: `qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test` exited 0. Expected diagnostics were `truncated gzip input` and the missing-archive error; all assertions passed with no sanitizer failure.
- Open acceptance work: finalization-error fixtures, streamed-download limits, transactional package validation/publication and dialog lifecycle remain. Keep T08 IN_PROGRESS.
- Next safe action: continue package transaction validation without touching the installed database.
- Commit/PR: not committed.

### Luna implementation session — T08 package schema boundary — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp` and this roadmap log.
- Implementation: package validation now parses both metadata arrays, requires a nonempty string `package_url`, requires every index item to be an object with safe basename `n`/`s` components and numeric rank, and rejects malformed entries before staging publication. The check uses the shared `SafeFileOperations::isSafeName` rule and does not assume optional measurement files.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 AeqPackageManager.o` exited 0. The only diagnostics were pre-existing qtpromise `-Wmaybe-uninitialized` warnings; `git diff --check` exited 0.
- Open acceptance work: disposable valid/invalid package fixtures exercising this schema, referenced-content completeness, publication rollback and full install promise behavior remain. Keep T08 IN_PROGRESS.
- Next safe action: add a focused package-schema helper test or validate through a headless package-manager fixture without touching the installed database.
- Commit/PR: not committed.

### Luna implementation session — T08 referenced-content validation — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp` and this roadmap log.
- Implementation: each validated index record must now resolve to an existing `name/source` measurement directory containing at least one supported selector payload (`raw.csv` or `graphic.txt`). This prevents publication of metadata that would make the installed selector dereference missing content, while retaining support for either supported data representation.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 AeqPackageManager.o` exited 0; only the pre-existing qtpromise `-Wmaybe-uninitialized` diagnostics were emitted, and `git diff --check` exited 0.
- Open acceptance work: disposable schema/content fixtures, complete package validation/publish rollback and install-promise tests remain. Keep T08 IN_PROGRESS.
- Next safe action: add focused package fixture coverage without using the live AutoEQ cache.
- Commit/PR: not committed.

### Luna implementation session — T08 package validation helper regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageValidation.h`, `src/subprojects/AutoEqIntegration/AeqPackageValidation.cpp`, `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp`, `src/subprojects/AutoEqIntegration/AutoEqIntegration.pri`, `src/tests/aeq_package_validation_test.cpp`, `src/tests/aeq_package_validation_test.pro` and this roadmap log.
- Implementation: extracted the metadata/content gate from `AeqPackageManager` into a reusable helper and added disposable fixtures covering a valid raw-data package, a valid graphic-only package, missing content, unsafe `../` path components, missing index fields and malformed JSON.
- Verification: `qmake6 /home/soloarch/Workspace/upstream/JDSP4Linux-luna/src/tests/aeq_package_validation_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation/aeq_package_validation_test` exited 0 with no sanitizer diagnostics. The initial include-path-only build failure was corrected and is not a product failure.
- Open acceptance work: transactional publication rollback, full install-promise behavior, streamed download boundaries and dialog lifecycle remain. Keep T08 IN_PROGRESS.
- Next safe action: inspect the publication helper boundary and add a disposable rollback regression without touching the installed AutoEQ database.
- Commit/PR: not committed.

### Luna implementation session — T08 publication rollback regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageValidation.h`, `src/subprojects/AutoEqIntegration/AeqPackageValidation.cpp`, `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp`, `src/tests/aeq_package_validation_test.cpp` and this roadmap log.
- Implementation: moved the staging-to-database transaction into the AutoEQ boundary and tested both failure rollback (missing staging leaves the existing database intact) and successful replacement (new content is published and old content is removed).
- Verification: `qmake6 /home/soloarch/Workspace/upstream/JDSP4Linux-luna/src/tests/aeq_package_validation_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation/aeq_package_validation_test && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 AeqPackageManager.o AeqPackageValidation.o` exited 0. The app-object build emitted only the pre-existing qtpromise `-Wmaybe-uninitialized` warnings; no sanitizer diagnostics occurred in the focused test. `git diff --check` exited 0.
- Open acceptance work: full asynchronous install-promise behavior, streamed download boundaries and dialog lifecycle remain. Keep T08 IN_PROGRESS; no installed AutoEQ database was read or modified.
- Next safe action: continue T08 downloader/package lifecycle coverage, then return to the next dependency-ordered open roadmap task.
- Commit/PR: not committed.

### Luna implementation session — T08 downloader teardown sanitizer regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloader.h`, `src/subprojects/AutoEqIntegration/GzipDownloader.cpp`, `src/tests/gzip_downloader_test.pro` and this roadmap log.
- Implementation: downloader destruction now always requests cleanup, waits for an active extraction thread before object teardown, closes/removes the temporary archive, and avoids empty-path removal warnings. The focused downloader test project now explicitly links ASan/UBSan.
- Verification: `qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test && git diff --check` exited 0 with no sanitizer diagnostics and no empty-path warnings.
- Open acceptance work: direct dialog destruction during active extraction, full asynchronous install-promise behavior and streamed download boundary cases remain. Keep T08 IN_PROGRESS.
- Next safe action: add or extend headless lifecycle coverage where Qt test fixtures can exercise dialog-independent downloader cancellation safely.
- Commit/PR: not committed.

### Luna implementation session — T08 active extraction destruction regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: extended the fake network reply with an in-memory libarchive gzip fixture containing a 16 MiB member; the regression waits for `decompressionStarted`, destroys `GzipDownloader` while extraction is active, and drains the event loop afterward.
- Verification: `qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test && git diff --check` exited 0. The active-destruction scenario completed without ASan/UBSan diagnostics.
- Open acceptance work: full dialog-level destruction, asynchronous install-promise behavior and remaining network boundary/error cases remain. Keep T08 IN_PROGRESS.
- Next safe action: continue the dependency-ordered T08/T16 cancellation boundary, then reassess promotion only after all acceptance criteria have direct evidence.
- Commit/PR: not committed.

### Luna implementation session — T08 streamed oversize boundary regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: the fake reply now supports bodies without `Content-Length`; a 128 MiB + 1 byte streamed body is fed through `readyRead`, and the test asserts the downloader rejects it during streaming with exactly one size-limit error.
- Verification: `qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2 && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test && git diff --check` exited 0; header-gated, streamed oversize, active-destruction and idle teardown cases passed without sanitizer diagnostics.
- Open acceptance work: short-write simulation, full dialog destruction and asynchronous install-promise behavior remain. Keep T08 IN_PROGRESS.
- Next safe action: continue the remaining T08/T16 cancellation and signal-ownership boundary tests.
- Commit/PR: not committed.

### Luna implementation session — T16 offscreen dialog destruction regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/gzip_downloader_test.cpp`, `src/tests/gzip_downloader_test.pro` and this roadmap log.
- Implementation: extended the sanitizer harness with `GzipDownloaderDialog` and its `.ui`; an offscreen dialog starts a real fixture extraction, is deferred-destroyed while the worker is active, and drains Qt deferred-delete events. This verifies dialog ownership reaches the downloader’s wait-before-destruction path.
- Red evidence: the first run asserted before Qt processed `DeferredDelete`; the test was corrected to call `sendPostedEvents(nullptr, QEvent::DeferredDelete)` inside its bounded event loop. No product failure was inferred from that harness scheduling issue.
- Verification: `qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test && git diff --check` exited 0. Header-limit, streamed-limit, active extraction, dialog destruction and rollback-related downloader cases completed without sanitizer diagnostics.
- Open acceptance work: cancel during each download/extraction/validation phase, repeated start/abort, and full asynchronous install-promise publication behavior remain. Keep T16 and T08 IN_PROGRESS.
- Next safe action: add deterministic cancel-phase assertions and verify no success/publication signal follows cancellation.
- Commit/PR: not committed.

### Luna implementation session — T16 extraction cancellation completion — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: after `decompressionStarted`, the regression calls `GzipDownloader::abort()`, waits for the worker completion signal, and asserts exactly one cancellation error, zero success signals, and safe subsequent destruction. The test uses `QTest::qWait` rather than a `QTRY_*` macro because the executable entry point returns `int`.
- Red evidence: the first assertion window was too short to observe the queued worker signal; the signal-aware bounded wait corrected the harness timing. The previous macro attempt failed to compile because Qt’s macro uses a bare return; that test-only issue was removed.
- Verification: `set -euo pipefail; qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test; git diff --check` exited 0. Header-limit, streamed-limit, extraction cancellation, dialog destruction and idle teardown cases passed without sanitizer diagnostics.
- Open acceptance work: cancel during download and validation, repeated start/abort, and end-to-end install-promise publication remain. Keep T16 and T08 IN_PROGRESS.
- Next safe action: add repeated start/abort and download-phase cancellation assertions without network or installed-state mutation.
- Commit/PR: not committed.

### Luna implementation session — T16 download cancellation and repeated-start regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: added cancellation before any body data arrives, repeated `abort()` idempotence, and rejection of a second `start()` while the first reply is active. The test asserts the cancelled download produces no completion signal and the active reply is aborted exactly through the shared cleanup path.
- Verification: `set -euo pipefail; qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test; git diff --check` exited 0. Download-phase cancellation, repeated abort, repeated-start rejection, streamed oversize, extraction cancellation and dialog destruction passed without sanitizer diagnostics.
- Open acceptance work: cancellation during validation/publication and full asynchronous install-promise behavior remain. Keep T08 and T16 IN_PROGRESS.
- Next safe action: inspect the package-manager promise boundary for a testable offline reply/dialog seam; do not touch the installed database.
- Commit/PR: not committed.

### Luna implementation session — T08 declared-size truncation invariant — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h` and this roadmap log.
- Implementation: `Untar::copy_data` now treats `ARCHIVE_EOF` as success only after the declared member byte count reaches zero; premature EOF returns a fatal extraction result.
- Verification: `set -euo pipefail; qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; git diff --check` exited 0. Existing truncated-gzip, header-failure, containment, cancellation, duplicate and size-limit fixtures passed under ASan/UBSan; expected diagnostics were `truncated gzip input` and missing-archive output.
- Open acceptance work: end-to-end install-promise validation/publication and full cancellation phase matrix remain. Keep T08 IN_PROGRESS.
- Next safe action: continue package-manager offline integration coverage without touching the installed database.
- Commit/PR: not committed.

### Luna implementation session — T08 disposable database seam — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageManager.h`, `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp` and this roadmap log.
- Implementation: `AeqPackageManager` now accepts an optional database-directory override for isolated tests; production callers retain the existing default cache path. This creates a direct manager-level integration seam without reading or modifying the live AutoEQ database.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 AeqPackageManager.o && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 AeqPackageValidation.o && git diff --check` exited 0. The parent configuration still reports only the known uninitialized FlatTabWidget/LiquidEqualizerWidget/GraphicEQWidget subproject paths and pre-existing qtpromise warnings; both affected objects compile.
- Open acceptance work: a manager-level local-server promise test using the disposable override, validation/publication cancellation and full end-to-end install behavior remain. Keep T08 IN_PROGRESS.
- Next safe action: build the local-server manager integration fixture around this seam, without touching the installed cache.
- Commit/PR: not committed.

### Luna implementation session — T08 manager-level disposable database regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/aeq_package_manager_test.cpp`, `src/tests/aeq_package_manager_test.pro`, `src/subprojects/AutoEqIntegration/AeqPackageManager.h`, `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp` and this roadmap log.
- Implementation: added a headless manager test that supplies a temporary database directory, verifies the manager reports a complete raw-data package as installed, then rejects the same package after its referenced payload is removed. Production construction retains the normal cache-path default.
- Red evidence: the initial long build stopped before linking, and a stale object produced constructor-symbol link errors; the manager object was rebuilt to completion before the authoritative link/run.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager -B AeqPackageManager.o && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager -j2 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test && git diff --check` exited 0. The manager-level fixture passed under ASan/UBSan; no live cache or network was used.
- Open acceptance work: local-server `installPackage` promise resolution/rejection and cancellation/publication integration remain. Keep T08 IN_PROGRESS.
- Next safe action: build the local-server install fixture around the disposable database seam.
- Commit/PR: not committed.

### Luna implementation session — T08 local-server install promise integration — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/aeq_package_manager_test.cpp`, `src/tests/aeq_package_manager_test.pro` and this roadmap log.
- Implementation: added a headless loopback `QTcpServer` fixture serving generated gzip archives. The manager test installs a complete package into the injected temporary database and asserts promise resolution/publication; it then serves a metadata-only package, asserts promise rejection, and verifies the previously valid package remains installed and byte-preserved.
- Red evidence: the initial link used an object compiled before the constructor seam and failed with undefined `AeqPackageManager` symbols; after rebuilding the affected object to completion, the authoritative link/run succeeded.
- Verification: `set -euo pipefail; qmake6 src/tests/aeq_package_manager_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test; git diff --check` exited 0. Valid install resolution, malformed-package rejection, rollback preservation and manager-level validation passed under ASan/UBSan; only expected offscreen plugin `propagateSizeHints()` diagnostics were printed.
- Open acceptance work: explicit cancellation during install validation/publication, additional streamed short-write fault injection and final dialog/promise phase matrix remain. Keep T08 IN_PROGRESS.
- Next safe action: add a deterministic validation/publication cancellation seam or proceed to the next dependency-ordered open task while preserving the temporary-database boundary.
- Commit/PR: not committed.

### Luna implementation session — T08 deterministic short-write injection — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloader.h`, `src/subprojects/AutoEqIntegration/GzipDownloader.cpp`, `src/tests/gzip_downloader_test.cpp`, `src/tests/gzip_downloader_test.pro` and this roadmap log.
- Implementation: added a `JDSP_TEST_HOOKS`-only write-failure seam and routed both streamed and final reply writes through it. The focused test injects a zero-byte write, asserts the downloader reports a write failure exactly once, resets the hook, and continues through cancellation and dialog lifecycle cases.
- Red evidence: the first run retained pre-hook error-count expectations and failed at the subsequent cancellation assertion; corrected counts now account for the injected short-write error.
- Verification: `set -euo pipefail; qmake6 src/tests/gzip_downloader_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test; git diff --check` exited 0. Header-limit, streamed-limit, short-write, download/extraction cancellation, repeated-start, dialog destruction and teardown cases passed with no sanitizer diagnostics.
- Open acceptance work: validation/publication cancellation phase and complete package-manager shutdown matrix remain. Keep T08 IN_PROGRESS.
- Next safe action: perform a final T08/T16 acceptance audit and move to the next dependency-ordered task only where evidence is complete.
- Commit/PR: not committed.

### Luna implementation session — T08 special-file containment audit — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/Untar.h`, `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: tightened archive extraction to allow only regular-file and directory entries, while retaining explicit hardlink rejection. Added a real libarchive FIFO fixture and asserted it is rejected without creating the named pipe.
- Verification: `set -euo pipefail; qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t02-red -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; git diff --check` exited 0. Existing regular-file, directory, symlink, hardlink, FIFO, traversal, duplicate, truncation, cancellation, header-failure and size-limit cases passed under ASan/UBSan; expected diagnostics were printed for truncated and missing archives.
- Open acceptance work: validation/publication cancellation and final package-manager lifecycle matrix remain. Keep T08 IN_PROGRESS.
- Next safe action: continue the final T08/T16 audit and do not promote until all acceptance criteria have direct evidence.
- Commit/PR: not committed.

### Luna implementation session — T03 remove failure propagation — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/PresetManager.cpp` and this roadmap log.
- Implementation: `PresetManager::remove` now returns success and rescans only when `QFile::remove` actually succeeds; an existing-but-unremovable preset now reports failure.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 PresetManager.o; git diff --check` exited 0. The object compiled in the parent application configuration; the known uninitialized optional subproject paths were reported during qmake regeneration, with no new compiler errors.
- Open acceptance work: direct manager/UI failure fixtures, rename collision/error propagation and full application-linked verification remain. Keep T03 IN_PROGRESS.
- Next safe action: extend the existing preset file-operation regression with failure-preservation cases that do not require the unavailable GUI subprojects.
- Commit/PR: not committed.

### Luna implementation session — T03 symlink-alias and sanitizer regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/preset_file_operations_test.cpp`, `src/tests/preset_file_operations_test.pro` and this roadmap log.
- Implementation: added a symlink-alias identity case to the atomic-copy regression; copying a source onto a symlink to that same source is a successful no-op and preserves original bytes. The test project now explicitly links ASan/UBSan rather than relying on environment variables alone.
- Verification: `set -euo pipefail; qmake6 src/tests/preset_file_operations_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; git diff --check` exited 0. Self-copy, hardlink alias, symlink alias, destination replacement, missing source, failed destination and safe-name cases passed under ASan/UBSan.
- Open acceptance work: direct `PresetManager` rename/remove failure fixtures and full UI-linked verification remain. Keep T03 IN_PROGRESS.
- Next safe action: continue the dependency-ordered T03 manager-level failure tests without requiring unavailable GUI subprojects.
- Commit/PR: not committed.

### Luna implementation session — cumulative T03/T08 regression checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; git diff --check` exited 0. Disposable manager install/rejection/rollback, sanitizer preset identity/failure, and sanitizer archive containment/truncation/special-file regressions remain green. Expected offscreen plugin and archive diagnostics only.
- Scope: this is a cumulative verification checkpoint; it does not promote T03 or T08. UI-linked preset coverage, validation/publication cancellation and full release matrix remain open.
- Next safe action: continue the next dependency-ordered open acceptance item with direct evidence.
- Commit/PR: not committed.

### Luna implementation session — T03 file-selection safety guards — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/interface/FileSelectionWidget.cpp` and this roadmap log.
- Implementation: bookmark, rename and remove handlers now reject missing current-file state before optional access; rename treats an unchanged source/destination as a harmless no-op and rejects an existing destination explicitly before attempting the filesystem operation.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 FileSelectionWidget.o; git diff --check` exited 0. The affected UI object compiled in the parent configuration; qmake again reported only the known uninitialized optional widget subprojects.
- Open acceptance work: offscreen signal/UI fixtures and direct rename/remove failure tests remain. Keep T03 IN_PROGRESS.
- Next safe action: continue T03 UI-linked verification when the available headless fixtures can exercise the private handlers safely.
- Commit/PR: not committed.

### Luna implementation session — T03 offscreen file-selection workflow — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/file_selection_widget_test.cpp`, `src/tests/file_selection_widget_test.pro` and this roadmap log.
- Implementation: added an offscreen Qt fixture that creates disposable source/bookmark directories, selects a `.conf`, clicks Bookmark, verifies the `bookmarkAdded` signal and byte-identical destination, clicks Remove, and exercises cleared-selection guards without opening dialogs.
- Verification: `set -euo pipefail; qmake6 src/tests/file_selection_widget_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test; git diff --check` exited 0. Bookmark signal/bytes, removal and empty-selection safety cases passed under ASan/UBSan.
- Open acceptance work: offscreen rename/collision and deletion-failure injection remain, along with full application-linked verification. Keep T03 IN_PROGRESS.
- Next safe action: add deterministic rename collision coverage without interactive input, or continue the next dependency-ordered open task.
- Commit/PR: not committed.

### Luna implementation session — T03 centralized rename primitive — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/SafeFileOperations.h`, `src/data/SafeFileOperations.cpp`, `src/data/PresetManager.cpp`, `src/interface/FileSelectionWidget.cpp`, `src/tests/preset_file_operations_test.cpp` and this roadmap log.
- Implementation: centralized same-directory rename validation and execution. Safe basenames are required, same-file renames are no-ops, missing sources and existing destinations fail without modifying either file, and filesystem failure is propagated. Preset and file-selection paths now use the shared primitive.
- Verification: `set -euo pipefail; qmake6 src/tests/preset_file_operations_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; git diff --check` exited 0. Sanitized tests passed rename success, same-name no-op, collision preservation, missing source and traversal rejection. `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 SafeFileOperations.o PresetManager.o FileSelectionWidget.o; git diff --check` exited 0; qmake reported only the known unavailable optional widget subprojects.
- Open acceptance work: direct `PresetManager` rename/remove failure fixtures, interactive rename/collision UI coverage and full application-linked verification remain. Keep T03 IN_PROGRESS.
- Next safe action: add direct manager-level rename/remove tests using an injectable preset directory, then continue the dependency-ordered T04/T05 acceptance gaps.
- Commit/PR: not committed.

### Luna implementation session — T12 metadata validation and precision regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/EelParser.cpp`, `src/tests/eel_parser_test.cpp` and this roadmap log.
- Implementation: tightened numeric/list metadata validation for finite bounds, positive steps, valid current/default values and nonempty options; unsupported declarations no longer abort parsing of later properties. Assignment matching now accepts leading signs and values beginning with a decimal point while retaining identifier-line boundaries.
- Verification: `set -euo pipefail; qmake6 src/tests/eel_parser_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel/eel_parser_test; git diff --check` exited 0. The sanitizer fixture passed 0.001 and 0.0001 precision, minimum-relative quantization, later-property parsing after a missing assignment, invalid-step rejection and preservation of unrelated/commented assignments.
- Open acceptance work: duplicate/CRLF/enum metadata, UI/VM round-trip and malformed-source diagnostics remain. Keep T12 IN_PROGRESS.
- Next safe action: continue T13 bounded reload ownership/format work and retain this parser regression as the T12 baseline.
- Commit/PR: not committed.

### Luna implementation session — cumulative native and Qt checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/awBaxandall.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/awInterstage.eel; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel/eel_parser_test; git diff --check` exited 0. Results: `ASRC capacity test passed`, `rate transition test passed`, `liveprog runtime test passed`; offscreen UI and parser tests exited 0 with only the expected missing-variable warning.
- Scope: cumulative evidence remains green for T01/T04/T05 native paths and the current T03/T12 focused Qt paths. This checkpoint does not promote T03, T12 or downstream tasks whose full acceptance criteria remain open.
- Next safe action: continue dependency-ordered T13 reload/setup ownership work and retain TSan unavailability as an explicit environment limitation.
- Commit/PR: not committed.

### Luna implementation session — T06/T11 sanitizer checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; qmake6 src/tests/dsp_config_validation_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t06-config -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/dsp_config_validation_test; qmake6 src/tests/eq_preset_validation_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t06-red/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t06-red -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t06-red/eq_preset_validation_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test; git diff --check` completed successfully. DspConfig default/existence, EQ validation, and atomic EEL save failure-preservation tests exited 0 under ASan/UBSan; expected diagnostics were the unset default trace and missing-directory save error.
- Scope: this is verification evidence only; T06 and T11 retain open UI/parser/commit-failure acceptance items and remain IN_PROGRESS.
- Next safe action: continue T13/T15 boundary coverage, with all generated artifacts kept under `build/`.
- Commit/PR: not committed.

### Luna implementation session — T15 immediate editor-container synchronization — 2026-09-15

- Worker: Codex implementation session.
- Files changed: submodule `src/widgets/codeeditor.cpp`, new parent `src/tests/codeeditor_sync_test.cpp`/`.pro` and this roadmap log.
- Implementation: editor text changes now update the active `CodeContainer` synchronously; the existing 400 ms timer remains only for debounced backend refresh notifications. This closes the stale-container window during fast edits and tab transitions.
- Verification: `set -euo pipefail; qmake6 src/tests/codeeditor_sync_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-test/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-test -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-test/codeeditor_sync_test; git diff --check` exited 0 after adding the required MOC/header and StringUtils inputs. The sanitizer/offscreen fixture passed immediate edits on two containers and verified the first container remained unchanged after switching.
- Open acceptance work: editor-level Run/Save As signal-spy, cancellation, dirty-state and full application-linked tab coverage remain. Keep T15 IN_PROGRESS.
- Next safe action: continue T13 reload/setup ownership work, then return to T15 end-to-end signal coverage.
- Commit/PR: not committed.

### Luna implementation session — T17/T18/T13 contract checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; python3 -m py_compile meta/tests/workflow_contract_test.py; git diff --check` exited 0. Workflow revision/native-test assertions, fail-closed packaging staging, PipeWire callback restrictions and PulseAudio wrapper restrictions all passed.
- Scope: these are local contracts, not proof of GitHub Actions execution or a real Flatpak/DEB runtime matrix. T13, T17 and T18 remain IN_PROGRESS.
- Next safe action: continue direct T05 host failure coverage and then refresh the full integrated test sequence from the current source state.
- Commit/PR: not committed.

### Luna implementation session — T03 injectable preset-directory seam — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/PresetManager.{h,cpp}`, `src/data/model/PresetListModel.{h,cpp}` and this roadmap log.
- Implementation: added an optional constructor-only preset-directory override and propagated it to the preset list model, allowing disposable manager-level rename/remove tests without touching the live configuration directory. Normal singleton behavior still uses `AppConfig` paths.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 SafeFileOperations.o PresetListModel.o PresetManager.o; git diff --check` exited 0. The parent application objects compiled; qmake reported only the known missing optional widget subprojects.
- Open acceptance work: direct manager-level runtime fixture, deletion-failure injection and interactive rename/collision signal coverage remain. Keep T03 IN_PROGRESS.
- Next safe action: build a minimal manager fixture against the injectable directory, then continue T05 host failure coverage.
- Commit/PR: not committed.

### Luna implementation session — T12 bounded current/default values — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/EelParser.cpp` and this roadmap log.
- Implementation: numeric and list metadata now reject current values and explicit defaults outside declared bounds, in addition to finite bounds and positive-step checks.
- Verification: `set -euo pipefail; qmake6 src/tests/eel_parser_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel/eel_parser_test; git diff --check` exited 0. The sanitizer parser regression remained green; only the expected missing-variable warning was emitted.
- Open acceptance work: duplicate/CRLF/enum metadata, UI/VM round-trip and malformed-source diagnostics remain. Keep T12 IN_PROGRESS.
- Next safe action: continue T05 host failure coverage and maintain the current parser test as the T12 baseline.
- Commit/PR: not committed.

### Luna implementation session — T05 direct host reload transaction fixture — 2026-09-15

- Worker: Codex implementation session.
- Files changed: new `src/tests/dsp_host_reload_test.cpp`/`.pro` and this roadmap log.
- Implementation: added a headless host/native fixture using a disposable EEL file and initialized JamesDSP state. It observes compiler-result dispatches while testing successful activation, missing-file rejection, unreadable-directory rejection with the previous program still enabled, and explicit disabled reload.
- Verification: `set -euo pipefail; qmake6 src/tests/dsp_host_reload_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/dsp_host_reload_test; git diff --check` exited 0. The sanitizer host fixture passed; expected diagnostics were emitted for missing and unreadable candidates.
- Open acceptance work: section-order/CRLF/optional-section host cases, UI status coverage and initial-failure safe-inactive coverage remain. Keep T05 IN_PROGRESS.
- Next safe action: extend this fixture with an initial failed reload and CRLF/optional-section candidates, then continue the dependency-ordered T06/T11/T15 integration sweep.
- Commit/PR: not committed.

### Luna implementation session — T05 initial safe-inactive reload case — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Implementation: the host fixture now begins with a missing candidate before any valid VM exists and asserts Liveprog remains disabled, then verifies later valid activation and the last-good preservation cases.
- Verification: `set -euo pipefail; qmake6 src/tests/dsp_host_reload_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host -j2 dsp_host_reload_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/dsp_host_reload_test; git diff --check` exited 0. The initial-failure, valid activation, missing/unreadable preservation and explicit-disable assertions passed under ASan/UBSan; expected rejection diagnostics were emitted.
- Open acceptance work: CRLF/optional-section/section-order candidates and UI status coverage remain. Keep T05 IN_PROGRESS.
- Next safe action: add CRLF and optional-section source cases to this host fixture, then refresh the T05 native/host cumulative evidence.
- Commit/PR: not committed.

### Luna implementation session — T05 CRLF and section-order host coverage — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Implementation: extended the direct host fixture with a CRLF-formatted candidate whose sections are ordered `@sample` then `@init`, while retaining the optional-section case. Successful reload proves the host/parser boundary accepts these forms without dropping the active-state contract.
- Verification: `set -euo pipefail; qmake6 src/tests/dsp_host_reload_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host -j2 dsp_host_reload_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/dsp_host_reload_test; git diff --check` exited 0. Initial failure, valid activation, CRLF/section-order reload, missing/unreadable preservation and explicit disable all passed under ASan/UBSan.
- Open acceptance work: UI status/signal coverage and full host/backend integration remain. Keep T05 IN_PROGRESS.
- Next safe action: continue T03 direct manager runtime coverage or T13 ownership instrumentation; retain the host fixture as the T05 boundary baseline.
- Commit/PR: not committed.

### Luna implementation session — T05 host fixture sanitizer rebuild — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/audio/base/DspHost.h`, `src/tests/dsp_host_reload_test.pro` and this roadmap log.
- Implementation: corrected the standalone host fixture’s HEADLESS/GLib/MOC target configuration and made the callback declaration’s linkage match its definition. The production reload path is unchanged.
- Verification: `set -euo pipefail; qmake6 src/tests/dsp_host_reload_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host -j2 DspHost.o dsp_host_reload_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/dsp_host_reload_test; git diff --check` exited 0. Initial safe-inactive, valid, CRLF/section-order, missing/unreadable preservation and explicit-disable host assertions passed with no sanitizer findings; only expected rejection diagnostics were printed.
- Open acceptance work: UI status/signal coverage and full host/backend integration remain. Keep T05 IN_PROGRESS.
- Next safe action: continue the T03 manager-level fixture or T13 ownership instrumentation.
- Commit/PR: not committed.

### Luna implementation session — T13 steady-state allocation instrumentation — 2026-09-15

- Worker: Codex implementation session.
- Files changed: new `libjamesdsp/tests/process_allocation_test.c`/`.pro` and this roadmap log.
- Implementation: added linker allocator interposition for `malloc`, `calloc` and `realloc`. After DSP/EEL preparation, the fixture processes 256 native blocks while allocation tracking is enabled and asserts zero callback allocations.
- Verification: `set -euo pipefail; qmake6 libjamesdsp/libjamesdsp.pro CONFIG+=DEBUG_ASAN DEFINES+=JDSP_TEST_HOOKS -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan -j2 liblibjamesdsp.a; qmake6 libjamesdsp/tests/process_allocation_test.pro LIBS+=-L/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-test -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; git diff --check` exited 0. The ASan-linked native library and allocation-instrumented test passed with `steady-state process allocation test passed`.
- Scope: this proves the prepared native path’s tracked C allocator calls are zero for the tested Liveprog-only configuration; it does not yet cover every effect, format-change path, scheduler wait or arbitrary EEL complexity. Keep T13 IN_PROGRESS.
- Next safe action: extend allocation/wait instrumentation to enabled effects and the format-transition boundary, then continue the T03 manager fixture.
- Commit/PR: not committed.

### Luna implementation session — T13 effect-chain allocation matrix — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/process_allocation_test.c` and this roadmap log.
- Implementation: expanded allocator-interposition coverage to explicitly prepared reverb, stereo-enhancement, vacuum-tube and crossfeed modes, while keeping the no-argument default Liveprog-only and bounded. This avoids silently hanging CI on unconfigured effect state.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-test -j2 process_allocation_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; for effect in reverb stereo tube crossfeed; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$effect"; done; git diff --check` exited 0. Default plus all four effect modes printed `steady-state process allocation test passed` under ASan/UBSan.
- Scope: an exploratory compressor mode exceeded the bound and an exploratory bass-only mode crashed because this fixture lacks their normal host parameter setup; those outcomes are retained as T13 follow-up evidence, not suppressed. Compressor/bass preparation, format transitions, scheduler waits and full effect configuration remain open. Keep T13 IN_PROGRESS.
- Next safe action: prepare compressor/bass through the normal host configuration path or add isolated effect constructors before extending the allocation matrix; continue T03 manager runtime coverage in parallel.
- Commit/PR: not committed.

### Luna implementation session — T13 prepared compressor/bass allocation coverage — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/process_allocation_test.c` and this roadmap log.
- Implementation: prepared compressor parameters/gain curves and bass parameters through their native configuration APIs before enabling them. The allocation fixture now supports explicit default, compressor, bass, reverb, stereo, tube and crossfeed modes; no-argument execution remains bounded and safe.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-test -j2 process_allocation_test; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$mode"; fi; done; git diff --check` exited 0. All seven modes printed `steady-state process allocation test passed` under ASan/UBSan.
- Scope: this closes the prepared native effect allocation matrix for the tested modes, but does not prove absence of waits, allocations in every optional effect/configuration or format-transition work. Keep T13 IN_PROGRESS.
- Next safe action: add bounded wait/transition instrumentation and continue T03 manager runtime coverage.
- Commit/PR: not committed.

### Luna implementation session — T13 processing lock instrumentation — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/process_allocation_test.c`, `libjamesdsp/tests/process_allocation_test.pro` and this roadmap log.
- Implementation: added `pthread_mutex_lock` interposition and monotonic timing around native processing lock acquisitions. Each tested prepared mode now requires at least one tracked lock and a maximum acquisition interval below 10 ms, alongside the zero-allocation assertion.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-test -j2 process_allocation_test; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$mode"; fi; done; git diff --check` exited 0. All seven modes passed allocation and bounded-lock assertions under ASan/UBSan.
- Scope: this is single-threaded instrumentation and does not establish concurrent wait/reclamation safety or format-transition behavior. Keep T13 IN_PROGRESS.
- Next safe action: add a controlled concurrent reader/format-transition measurement, then continue T03 manager runtime coverage.
- Commit/PR: not committed.

### Luna implementation session — T13 concurrent rate/process regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/rate_transition_test.c` and this roadmap log.
- Implementation: extended the native rate-transition regression with a concurrent processing thread and 64 repeated 44.1/48/96 kHz transitions, then asserted the final true and effective rates remain coherent. Added the missing public-test declaration for `JamesDSPProcess`.
- Verification: `set -euo pipefail; qmake6 libjamesdsp/tests/rate_transition_test.pro LIBS+=-L/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan QMAKE_CFLAGS+=-fsanitize=address,undefined QMAKE_LFLAGS+=-fsanitize=address,undefined -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate -j2 rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate/rate_transition_test; git diff --check` exited 0 with `rate transition test passed` and no sanitizer diagnostics.
- Scope: this covers controlled native concurrent processing/rate updates, not PipeWire/PulseAudio scheduling, arbitrary quantum growth, or retired-state reclamation. Keep T13 IN_PROGRESS.
- Next safe action: add a bounded quantum-growth/format replacement fixture and continue T03 manager runtime coverage.
- Commit/PR: not committed.

### Luna implementation session — T13 quantum growth and failed replacement coverage — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/process_allocation_test.c`, `libjamesdsp/tests/process_allocation_test.pro` and this roadmap log.
- Implementation: extended the prepared native allocation/lock fixture to inject a failed 4096-frame replacement and assert the prior buffer pointer/block size survive, then perform a successful 1024-frame growth before processing. The test-hook definition is enabled explicitly in the target.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-test -j2 process_allocation_test; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$mode"; fi; done; git diff --check` exited 0. All seven modes passed failed-replacement preservation, successful growth, zero tracked processing allocations and bounded lock assertions under ASan/UBSan.
- Scope: this covers the native replacement seam and prepared modes, not backend format dispatch, every optional effect, scheduler p99 or retired-state reclamation. Keep T13 IN_PROGRESS.
- Next safe action: add backend format-dispatch instrumentation and continue T03 manager-level runtime coverage.
- Commit/PR: not committed.

### Luna implementation session — CI regression wiring checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py`, `src/tests/dsp_host_reload_test.pro` and this roadmap log.
- Implementation: wired parser, editor synchronization and direct host reload fixtures into the native verification jobs; added GLib to sanitizer dependencies; enabled `JDSP_TEST_HOOKS` for the sanitizer native library; added the allocator/effect matrix to the sanitizer job; and extended the workflow contract to require all new regression targets.
- Verification: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; python3 -m py_compile meta/tests/workflow_contract_test.py; qmake6 src/tests/eel_parser_test.pro 'CONFIG += DEBUG_ASAN' -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/eel-parser.Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt -f eel-parser.Makefile -j2; qmake6 src/tests/codeeditor_sync_test.pro 'CONFIG += DEBUG_ASAN' -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/codeeditor-sync.Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt -f codeeditor-sync.Makefile -j2; qmake6 src/tests/dsp_host_reload_test.pro 'CONFIG += DEBUG_ASAN' DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/dsp-host.Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt -f dsp-host.Makefile -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/eel_parser_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/codeeditor_sync_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/dsp_host_reload_test; git diff --check` exited 0. The workflow contract, parser, editor and host fixtures all passed; expected missing-variable and rejected-reload diagnostics were emitted, with no sanitizer findings.
- Scope: GitHub Actions itself was not executed locally. T03, T05, T06, T11, T12, T13, T15, T17 and T18 retain open acceptance items; T01 and T02 remain VERIFIED.
- Next safe action: continue the dependency-ordered T03 manager-level runtime fixture and then refresh cumulative T00–T07 evidence.
- Commit/PR: not committed.

### Luna implementation session — T03 manager boundary fixture — 2026-09-15

- Worker: Codex implementation session.
- Files changed: new `src/tests/preset_manager_test.cpp`/`.pro`, `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: added a disposable-directory `PresetManager` fixture covering discovery, successful rename, model rescan, collision preservation, traversal rejection, successful removal and missing/removal rejection. The native and sanitizer CI jobs now run this manager fixture alongside the lower-level file-operation test.
- Verification: `set -euo pipefail; qmake6 src/tests/preset_manager_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; git diff --check` exited 0. The manager fixture passed under ASan/UBSan.
- Scope: filesystem permission-failure injection and UI signal coverage remain open; no live configuration directory was touched. T03 remains IN_PROGRESS.
- Next safe action: refresh the cumulative T00–T07 native/sanitizer sequence, then continue T06/T07 acceptance gaps.
- Commit/PR: not committed.

### Luna implementation session — T00–T07 milestone checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; python3 -m py_compile meta/tests/workflow_contract_test.py; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/config/config_io_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/dsp_config_validation_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t06-red/eq_preset_validation_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host/dsp_host_reload_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel/eel_parser_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test; git diff --check` exited 0. Workflow/packaging/backend contracts and the current T00–T07 focused native/Qt fixtures all passed; expected default, rejection and missing-directory diagnostics were emitted without sanitizer findings.
- Board: T00, T01 and T02 remain VERIFIED. T03–T07 remain IN_PROGRESS because their task-card acceptance still includes UI/error-injection/full integration items; the first milestone has runnable evidence but is not being overstated as complete.
- Scope: no package, installation, Flatpak permission, live configuration or running audio process was changed. GitHub Actions and hardware/audio-graph behavior remain unexecuted locally.
- Next safe action: continue dependency-ordered T06/T07 acceptance coverage, then refresh T08/T09/T10 integration evidence.
- Commit/PR: not committed.

### Luna implementation session — native sanitizer gate refresh — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; for mode in compressor bass reverb stereo tube crossfeed; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$mode"; done; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; git diff --check` exited 0 after correcting one stale assumed ASRC binary path. The seven allocation modes, concurrent rate transition and ASRC capacity regression all passed with no sanitizer findings.
- Scope: the initial path lookup `/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/libjamesdsp/tests/asrc_capacity_test` returned `No such file or directory`; no test ran from that path. The located T01 binary passed. T13 remains IN_PROGRESS pending broader callback/format/reclamation coverage; T01 remains VERIFIED.
- Next safe action: continue T08/T09/T10 cumulative validation and keep all generated outputs under `build/`.
- Commit/PR: not committed.

### Luna implementation session — T10 standalone crossfeed lifecycle regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: new `libjamesdsp/tests/crossfeed_lifecycle_test.c`/`.pro`, `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: added an independent two-thread native regression that checks unchanged crossfeed enable preserves the selected long-convolver identity, then processes 2,000 blocks while 64 forced replacements occur under the crossfeed lock, followed by clean disable/free. Both normal and sanitizer CI jobs now run it.
- Verification: `set -euo pipefail; qmake6 libjamesdsp/tests/crossfeed_lifecycle_test.pro 'CONFIG += DEBUG_ASAN' LIBS+=-L/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan -o /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed -j2 crossfeed_lifecycle_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; python3 meta/tests/workflow_contract_test.py; git diff --check` exited 0 with `crossfeed lifecycle test passed` and `workflow contract test passed`.
- Scope: this covers the selected long-convolver replacement under the current mutex protocol; TSan, alternate native assembly paths and full backend lifecycle remain open. Keep T10 IN_PROGRESS.
- Next safe action: continue T08 package transaction failure coverage and then refresh T09/T10 cumulative evidence.
- Commit/PR: not committed.

### Luna implementation session — T08 package transaction regression checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; for test in /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-validation/aeq_package_validation_test /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test /home/soloarch/Workspace/build/jamesdsp-luna-t08/untar_test; do QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$test"; done; git diff --check` exited 0. Validation, manager, downloader and extraction fixtures passed under ASan/UBSan; expected offscreen size-hint messages and missing-archive diagnostics were emitted.
- Environment note: the first identical loop without `QT_QPA_PLATFORM=offscreen` stopped at the Qt manager fixture with QtWayland teardown leak reports; no source failure was inferred, and the corrected offscreen run passed. T08 remains IN_PROGRESS because generated malicious/truncated archives, extracted-size boundaries and complete package publication checks are not yet exhaustive.
- Next safe action: add explicit T08 archive-link/duplicate/truncation fixtures and then refresh T09/T10 sanitizer evidence.
- Commit/PR: not committed.

### Luna implementation session — T08 absolute-member extraction coverage — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/untar_test.cpp` and this roadmap log.
- Implementation: extended the disposable archive fixture with an absolute-path member, asserting extraction rejects it and cannot create `/absolute-escape.txt`; existing traversal, duplicate, link, FIFO, truncation and size-limit cases remain covered.
- Verification: `set -euo pipefail; qmake6 src/tests/untar_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08 -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t08/untar_test; git diff --check` exited 0. The ASan/UBSan extraction fixture passed; expected truncated-gzip and missing-archive diagnostics were printed.
- Scope: link-target, duplicate, truncation and size-boundary coverage now has direct fixtures; complete manager install publication and filesystem-failure injection remain open. Keep T08 IN_PROGRESS.
- Next safe action: continue T09/T10 cumulative verification and investigate a separate TSan-capable build if the toolchain supports it.
- Commit/PR: not committed.

### Luna implementation session — T10 TSan capability checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/crossfeed_lifecycle_test.pro` and this roadmap log.
- Implementation: made the standalone crossfeed lifecycle target select TSan via `CONFIG+=DEBUG_TSAN`, while retaining ASan/UBSan as the default sanitizer configuration used by CI.
- Verification: `set -euo pipefail; qmake6 libjamesdsp/libjamesdsp.pro CONFIG+=DEBUG DEFINES+=JDSP_TEST_HOOKS QMAKE_CFLAGS+=-fsanitize=thread QMAKE_CXXFLAGS+=-fsanitize=thread QMAKE_LFLAGS+=-fsanitize=thread -o /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan -j2 liblibjamesdsp.a; qmake6 libjamesdsp/tests/crossfeed_lifecycle_test.pro CONFIG+=DEBUG_TSAN LIBS+=-L/home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan -o /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan-test/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan-test -B -j2 crossfeed_lifecycle_test` compiled successfully. `TSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan-test/crossfeed_lifecycle_test` terminated before execution with `FATAL: ThreadSanitizer: unexpected memory mapping`; this is a runtime/toolchain environment limitation, not a reported race.
- Scope: ASan/UBSan crossfeed lifecycle verification remains green; TSan evidence is unavailable in this container. T10 remains IN_PROGRESS and the limitation is explicit.
- Next safe action: continue T11–T16 targeted acceptance work and retain this TSan result in the release gate.
- Commit/PR: not committed.

### Luna implementation session — T19 native metadata validation alignment — 2026-09-15

- Worker: Codex implementation session.
- Files changed: external EELVault project `/home/soloarch/Workspace/projects/eelvault/eel_parse.py`, `validate.py`, `test_eelvault.py`, `run_regressions.py`, and this roadmap log. The EELVault directory has no Git history; its existing local baseline was preserved and changes remain separately identifiable.
- Implementation: EELVault now records malformed native-style metadata instead of silently dropping it; validation reports the malformed control. Added parity fixtures for optional step defaults, enum metadata, CRLF and `@sample` suffix formatting, plus malformed `nan` metadata.
- Verification: `set -euo pipefail; cd /home/soloarch/Workspace; PYTHONPATH=/home/soloarch/Workspace python3 projects/eelvault/run_regressions.py; python3 -m py_compile projects/eelvault/eel_parse.py projects/eelvault/validate.py projects/eelvault/test_eelvault.py; git -C /home/soloarch/Workspace/upstream/JDSP4Linux-luna diff --check` exited 0. The dependency-free EELVault regression suite passed; `python3 -m pytest` remains unavailable because the environment has no pytest module.
- Scope: this validates structural metadata agreement only; native compilation/runtime parity is still represented by the separate native corpus tests. T19 remains IN_PROGRESS.
- Next safe action: continue T14 corpus/rate behavior verification and refresh T17–T20 integrated evidence.
- Commit/PR: not committed.

### Luna implementation session — T14/T19 native corpus checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/liveprog_runtime_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t14-corpus/comparison.json --skip-external; git diff --check` completed successfully. Native expectations passed for all 50 manifest files; comparison covered 47 entries and exited 0 with two reported differences: Python accepted but native rejected non-shipping `libjamesdsp/subtree/Main/CLI/hpfloat.eel` and editor demo `src/subprojects/EELEditor/src/definitions/demo.eel`.
- Scope: differences are explicit structural-validator/compiler differences, not silently ignored results. `stftDenoise.eel` is included in the passing native corpus, but analytical coefficient/time-response and seeded signal regressions remain open. T14 and T19 remain IN_PROGRESS.
- Next safe action: add numerical stft/gain/filter corpus assertions and continue T16 cancellation plus T17–T20 integrated evidence.
- Commit/PR: not committed.

### Luna implementation session — T14 denoiser analytical coefficient regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation: the `stftDenoise.eel` corpus assertion now checks its smoothing coefficient against the analytical `exp(-(2048/2/48000)/0.9)` value, not merely that it is finite and between zero and one.
- Verification: `set -euo pipefail; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' LIBS+=-L/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan PRE_TARGETDEPS+=/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/liblibjamesdsp.a -o /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/Makefile; ln -sfn /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/liblibjamesdsp.a /home/soloarch/Workspace/build/liblibjamesdsp.a; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime -B -j2 liveprog_runtime_test LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test resources/assets/liveprog/stftDenoise.eel; git diff --check` exited 0 with `liveprog runtime test passed` and no sanitizer diagnostics.
- Scope: this closes the demonstrated undefined-rate coefficient regression; broader seeded signal/time-response and all parameter/rate sweeps remain open. T14 remains IN_PROGRESS.
- Next safe action: refresh the integrated offline gate and inspect T17–T20 package/identity acceptance against current files.
- Commit/PR: not committed.

### Luna implementation session — integrated offline gate refresh — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-packaging meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; bash src/tests/visual_theme_registry_test.sh; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/codeeditor_sync_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/dsp_host_reload_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; python3 -m py_compile meta/tests/workflow_contract_test.py libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/compare_eel_manifest.py; git diff --check` exited 0. Workflow, packaging, backend, theme, ASRC, crossfeed, rate, allocation, editor, host-reload and preset-manager checks all passed; expected host rejection diagnostics were emitted.
- Scope: this is an offline gate, not proof of full application GUI/backend hardware behavior, GitHub Actions execution or installed-package equivalence. T03–T20 remain IN_PROGRESS where task-card acceptance requires those unavailable or unimplemented dimensions; T01/T02 remain VERIFIED.
- Next safe action: continue T16 cancellation and T17/T18 artifact identity coverage, then perform the final T20 audit without installation or runtime restart.
- Commit/PR: not committed.

### Luna implementation session — T18 packaging identity/path coverage — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `meta/tests/packaging_contract_test.sh` and this roadmap log.
- Implementation: extended the mocked Flatpak packaging contract with a build root, selected executable and output path containing spaces. The fixture verifies the canonical executable directory is staged read-only and the reported SHA256 matches the selected binary.
- Verification: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging-spaces meta/tests/packaging_contract_test.sh; python3 meta/tests/workflow_contract_test.py; git diff --check` exited 0 with `packaging contract test passed` and `workflow contract test passed`.
- Scope: this remains mocked packaging evidence; no real Flatpak runtime/export/install or Debian package was produced. T18 remains IN_PROGRESS pending permitted local candidate-build/content inspection.
- Next safe action: continue T16 shutdown/cancellation coverage and T17/T20 final identity audit.
- Commit/PR: not committed.

### Luna implementation session — T03 deterministic filesystem failure injection — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/SafeFileOperations.{h,cpp}`, `src/data/PresetManager.cpp`, `src/interface/FileSelectionWidget.cpp`, `src/tests/preset_manager_test.cpp`/`.pro` and this roadmap log.
- Implementation: centralized safe removal alongside rename, routed manager/UI removal through it, and added test-only one-shot rename/remove failure injection. The manager fixture now asserts injected failures leave the source and destination files unchanged and does not rescan as a false success.
- Verification: `set -euo pipefail; qmake6 src/tests/preset_manager_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; qmake6 src/tests/preset_file_operations_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; git diff --check` exited 0. Manager failure-preservation and lower-level copy/rename tests passed under ASan/UBSan.
- Scope: UI signal-count coverage and permission/ownership failure cases remain open; injection is intentionally test-only and no live directories were touched. T03 remains IN_PROGRESS.
- Next safe action: refresh the app-object compile and continue T16/T17/T20 acceptance evidence.
- Commit/PR: not committed.

### Luna implementation session — T03 application-object compile refresh — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-app/src -B -j2 SafeFileOperations.o PresetManager.o FileSelectionWidget.o PresetListModel.o; git diff --check` exited 0. All affected application objects compiled in the parent configuration.
- Environment note: qmake reported the known absent optional `FlatTabWidget`, `LiquidEqualizerWidget` and `GraphicEQWidget` subproject files; this target-specific object build did not require them and no submodule or permission change was made.
- Scope: T03 remains IN_PROGRESS pending UI signal-count and broader application integration evidence.
- Next safe action: continue T16 cancellation/shutdown tests and perform the T17/T20 source/package identity audit.
- Commit/PR: not committed.

### Luna implementation session — T01 failed-replacement usability assertion — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/process_allocation_test.c` and this roadmap log.
- Implementation: after deterministic failure of a valid 4096-frame buffer replacement, the native fixture now processes the preserved 128-frame buffer and asserts both channels remain finite before testing successful growth.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-test -B -j2 process_allocation_test; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$mode"; fi; done; git diff --check` exited 0. All seven prepared modes passed failed-replacement usability, successful growth, zero process allocations and bounded-lock assertions under ASan/UBSan.
- Scope: this strengthens T01/T13 native evidence; backend unexpected-quantum policy and complete callback ownership remain open. T01 remains VERIFIED; T13 remains IN_PROGRESS.
- Next safe action: continue T16 shutdown coverage and final T17–T20 identity/verification audit.
- Commit/PR: not committed.

### Luna implementation session — T17 CI corpus coverage — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/run_eel_corpus.py`, `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: added an explicit `--skip-external` corpus mode for repository CI, since the three custom EEL files live outside the application checkout. The native verification job now runs the 47 in-repository manifest entries and writes the Python/native comparison report; local runs still cover all 50 entries.
- Verification: `set -euo pipefail; python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/liveprog_runtime_test; python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t09/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t17-corpus/comparison.json --skip-external; python3 meta/tests/workflow_contract_test.py; python3 -m py_compile libjamesdsp/tests/run_eel_corpus.py; git diff --check` exited 0. The 47-entry native expectation run and comparison passed; the same two intentional non-shipping Python/native differences were reported.
- Scope: GitHub Actions execution remains unavailable locally; custom scripts are covered by the separate local 50-entry run. T17 remains IN_PROGRESS pending actual CI execution and broader full/headless/backend matrix evidence.
- Next safe action: continue T16 cancellation evidence and perform the T20 source/submodule/package identity audit.
- Commit/PR: not committed.

### Luna implementation session — T20 identity and scope audit checkpoint — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; git status --short --branch; git diff --stat; git diff --submodule=short -- src/subprojects/EELEditor; git submodule status --recursive; sha256sum /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/liblibjamesdsp.a /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan-qt/dsp_host_reload_test; pgrep -a -f 'jamesdsp|flatpak-builder|qmake|make|ninja' || true; git diff --check` completed with the isolated branch identified as `luna/2026-09-15-roadmap`, editor submodule at `b2f392480e00ca232c397610f42688b165b87640-dirty`, optional widget submodules at their recorded SHAs, reproducible build hashes printed, and `git diff --check` clean.
- Scope: the running audio application was observed but not restarted or modified; no install, publish, Flatpak permission change or commit was performed. The working tree contains the preserved user/application changes plus Luna changes, so this is an identity checkpoint rather than a claim that every changed line has been independently attributed.
- Board: T01 and T02 remain VERIFIED; T03–T20 remain IN_PROGRESS. Full application build, GitHub Actions execution, hardware/backend runtime, real package export and TSan runtime remain unverified or unavailable.
- Next safe action: continue targeted T16 shutdown coverage, then complete the T20 final handoff audit with unresolved risks and exact pending runtime procedure.
- Commit/PR: not committed.

### Luna implementation session — T16 cancellation and shutdown sanitizer regression — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t16; mkdir -p "$BUILD_ROOT"; qmake6 src/tests/gzip_downloader_test.pro 'CONFIG += DEBUG_ASAN' -o "$BUILD_ROOT/Makefile"; make -C "$BUILD_ROOT" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$BUILD_ROOT/gzip_downloader_test"; git diff --check` exited 0. The test passed under ASan/UBSan; only expected offscreen Qt `propagateSizeHints()` diagnostics were emitted.
- Coverage: deterministic oversized content-length and streamed-limit failures, short-write injection, repeated download abort, duplicate active-start rejection, active extraction interruption, destructor cleanup, and dialog deletion during extraction.
- Scope: T16 remains IN_PROGRESS pending broader application shutdown/thread-lifetime evidence and real network/backend behavior. No live application, installation, package publication or permissions were changed.
- Next safe action: perform the final T17–T20 offline identity/contract audit and retain unresolved environment limitations explicitly in the handoff.
- Commit/PR: not committed.

### Luna implementation session — consolidated native regression refresh — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test "$mode"; fi; done; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test resources/assets/liveprog/stftDenoise.eel; python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test; python3 meta/tests/workflow_contract_test.py; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; bash src/tests/visual_theme_registry_test.sh; git diff --check` exited 0. ASan/UBSan native regressions, all seven allocation modes, native corpus entries, workflow/backend/theme contracts and whitespace validation passed; no sanitizer diagnostics were emitted.
- Scope: this refresh validates the current isolated source against the available offline/native evidence. It does not substitute for GitHub Actions, a full application GUI build, hardware/backend runtime, real package export/install, or the previously observed TSan runtime limitation. T01 and T02 remain VERIFIED; T03–T20 remain IN_PROGRESS where acceptance criteria are incomplete or environment-limited.
- Next safe action: retain the exact pending runtime/CI/package procedures in the final handoff and continue any dependency-ordered implementation that can be validated locally without touching the running application.
- Commit/PR: not committed.

### Luna implementation session — final T20 offline identity audit — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; git status --short --branch; git diff --stat; git diff --submodule=short -- src/subprojects/EELEditor; git submodule status --recursive; sha256sum /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/liblibjamesdsp.a /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test /home/soloarch/Workspace/build/jamesdsp-luna-t16/gzip_downloader_test; pgrep -a -f 'jamesdsp|flatpak-builder|qmake|make|ninja' || true; git diff --check` completed successfully. The isolated branch, dirty editor submodule, optional widget submodule revisions, current artifact hashes, running `jamesdsp --tray` process and clean diff check were recorded.
- Scope: no application restart, install, publish, commit, Flatpak permission change or destructive cleanup was performed. The worktree retains the user/application changes and Luna additions; artifact hashes identify tested outputs but do not establish a real package’s embedded source identity.
- Board: T01 and T02 are VERIFIED. T03–T20 remain IN_PROGRESS because their cards retain incomplete UI/backend/CI/package/runtime acceptance dimensions; TSan runtime and full application build remain environment-limited, and the running application was only observed.
- Next safe action: resume with the first unresolved dependency-ordered implementation card (T03 UI signal/permission coverage or T04/T05 host-level coverage) in a later session; do not call this candidate release-ready.
- Commit/PR: not committed.

### Luna implementation session — T03 UI failure-preservation regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/SafeFileOperations.{h,cpp}`, `src/interface/FileSelectionWidget.cpp`, `src/tests/file_selection_widget_test.{cpp,pro}` and this roadmap log.
- Implementation: added deterministic one-shot copy-failure injection for tests; the offscreen widget fixture now verifies failed bookmark copy emits no additional `bookmarkAdded` signal and leaves the destination absent, and failed removal preserves the source. Corrected removal to validate the selected file’s basename against the current directory, and only refreshes the listing after successful removal.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; git diff --check` exited 0. Widget, manager and lower-level filesystem tests passed under ASan/UBSan.
- Scope: deterministic permission/ownership failures and full application UI integration remain open; no live directories or running application state were touched. T03 remains IN_PROGRESS.
- Next safe action: continue T04/T05 host-level legacy-control and failed-reload acceptance, then refresh the dependency-ordered gate.
- Commit/PR: not committed.

### Luna implementation session — T05 host failed-reload output preservation — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Implementation: expanded the direct host fixture with valid gain-2 and recovery gain-3 programs, malformed source, missing source, CRLF/out-of-order sections, and an initially missing file. It now processes buffers after each rejected reload to prove the initial state is safely inactive, the last valid program remains audible, recovery replaces it, and explicit disable remains honored.
- Verification: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current; mkdir -p "$BUILD_ROOT"; qmake6 src/tests/dsp_host_reload_test.pro DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan -o "$BUILD_ROOT/Makefile"; make -C "$BUILD_ROOT" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$BUILD_ROOT/dsp_host_reload_test"; git diff --check` exited 0. The host fixture passed under ASan/UBSan; expected missing-file and syntax diagnostics were emitted.
- Scope: UI status/signal assertions and full application/backend integration remain open. T05 remains IN_PROGRESS.
- Next safe action: continue T04/T05 UI-equivalent status coverage or the next dependency-ready T06 configuration boundary fixture.
- Commit/PR: not committed.

### Luna implementation session — T12 metadata duplicate and quantization regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/EelParser.cpp`, `src/tests/eel_parser_test.cpp` and this roadmap log.
- Implementation: EEL metadata parsing now keeps only the first valid description for a variable, preventing duplicate controls. The fixture now covers fractional steps (`0.25`, `0.0001`), nonzero-minimum quantization, optional/default metadata, enum/list metadata, invalid zero step rejection, comment/identifier boundaries and precision-preserving source updates.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel-current -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel-current/eel_parser_test; git diff --check` exited 0. The parser regression passed; the expected missing-variable warning was emitted and no sanitizer diagnostics occurred.
- Scope: malformed metadata diagnostics, full UI/VM round-trip and unsupported syntax reporting remain open. T12 remains IN_PROGRESS.
- Next safe action: inspect T11/T15 editor save/run path and add deterministic current-text synchronization coverage before another integrated gate.
- Commit/PR: not committed.

### Luna implementation session — T12 parser ownership sanitizer fix — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/data/EelParser.{h,cpp}`, `src/tests/eel_parser_test.cpp`, `src/tests/eel_parser_test.pro` and this roadmap log.
- Implementation: added explicit `EELParser` destruction of owned property objects. The instrumented regression target now carries explicit ASan/UBSan compile and link flags; this corrected a verification gap where the qmake sanitizer feature name alone did not inject flags in the local environment.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel-current -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12-eel-current/eel_parser_test; git diff --check` exited 0. The build visibly used `-fsanitize=address,undefined`; the parser test passed with no leak, ASan or UBSan diagnostics.
- Scope: T12 UI/VM round-trip and malformed metadata diagnostic surface remain open. T12 remains IN_PROGRESS.
- Next safe action: continue T15 editor Run/Save As integration coverage, then refresh the full offline gate.
- Commit/PR: not committed.

### Luna implementation session — T15 editor container ownership and synchronization regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: editor submodule `src/widgets/projectview.{h,cpp}`, parent `src/tests/codeeditor_sync_test.{cpp,pro}` and this roadmap log.
- Implementation: initialized `ProjectView`’s previous-container pointer, deleted detached `CodeContainer`/list-item ownership on close, and added destructor cleanup. The offscreen fixture now covers immediate text synchronization across two containers and open/close/replacement lifecycle behavior.
- Verification: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-current; mkdir -p "$BUILD_ROOT"; qmake6 src/tests/codeeditor_sync_test.pro -o "$BUILD_ROOT/Makefile"; make -C "$BUILD_ROOT" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$BUILD_ROOT/codeeditor_sync_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/codecontainer_save_test/codecontainer_save_test; git diff --check` exited 0. Editor lifecycle/synchronization and save-preservation tests passed under ASan/UBSan; expected failed-destination diagnostic was emitted.
- Scope: EELEditor-level Run/Save-As dialog signal-spy and full application-linked tab/shutdown coverage remain open. T15 remains IN_PROGRESS.
- Next safe action: refresh the current integrated gate and audit remaining T06/T07/T08/T10/T13/T14/T17–T20 acceptance gaps.
- Commit/PR: not committed.

### Luna implementation session — T06 host EQ boundary regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Verification: the host fixture now sends a valid 30-value tone EQ and then a partial 3-value vector through `DspHost::update`, processes 16 frames, and asserts both channels remain finite. It also retains the T05 reload checks. `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current/dsp_host_reload_test; git diff --check` exited 0; the build used ASan/UBSan and emitted the expected neutral-EQ fallback and reload diagnostics.
- Scope: malformed non-finite vectors, full UI loading and all effect-vector consumers remain open. T06 remains IN_PROGRESS.
- Next safe action: continue T07 numerical channel-separation coverage and then refresh T08/T10/T13 dependency evidence.
- Commit/PR: not committed.

### Luna implementation session — T06 non-finite EQ rejection refresh — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Verification: added a 30-field EQ vector ending in `nan` after the partial-vector case and routed it through `DspHost::update`; `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current -B -j2 >/dev/null; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current/dsp_host_reload_test; git diff --check` exited 0 under ASan/UBSan.
- Scope: UI loading and neighboring vector consumers remain open. T06 remains IN_PROGRESS.
- Next safe action: proceed with T07 channel-isolation numerical checks, then refresh integrated evidence.
- Commit/PR: not committed.

### Luna implementation session — T07 high-pass channel symmetry regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation: expanded the bundled `highpass200Hz.eel` fixture to capture the left impulse response, reload the script into fresh state, drive an equivalent right impulse, and assert exact opposite-channel silence plus matching active-channel responses.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime -B -j2 liveprog_runtime_test LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test resources/assets/liveprog/highpass200Hz.eel; git diff --check` exited 0. The native high-pass symmetry test passed under ASan/UBSan.
- Scope: 44.1 kHz comparison and custom diagnostic UI-equivalent signal coverage remain open. T07 remains IN_PROGRESS.
- Next safe action: continue T08 transaction/publication failure coverage and refresh the integrated native gate.
- Commit/PR: not committed.

### Luna implementation session — T08 canonical package containment regression — 2026-09-15

- Worker: Codex implementation session.
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageValidation.cpp`, `src/tests/aeq_package_validation_test.cpp` and this roadmap log.
- Implementation: package validation now rejects symlinked JSON, measurement directories and referenced data files, and requires canonical paths to remain below the package root. The disposable fixture links `raw.csv` to a sibling outside the package and confirms validation fails.
- Verification: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t08-validation-current; mkdir -p "$BUILD_ROOT"; qmake6 src/tests/aeq_package_validation_test.pro -o "$BUILD_ROOT/Makefile"; make -C "$BUILD_ROOT" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 "$BUILD_ROOT/aeq_package_validation_test"; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test; git diff --check` completed successfully. Validation and manager replacement/preservation tests passed under ASan/UBSan; expected offscreen Qt size-hint diagnostics were emitted.
- Scope: full package publication rollback fault injection and installed-database/runtime integration remain open. T08 remains IN_PROGRESS.
- Next safe action: refresh the integrated T08/T16 gate and continue T10/T13 concurrency/ownership work.
- Commit/PR: not committed.

### Luna implementation session — T08 symlink containment sanitizer verification — 2026-09-15

- Worker: Codex implementation session.
- Verification: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t08-validation-current; mkdir -p "$BUILD_ROOT"; qmake6 src/tests/aeq_package_validation_test.pro -o "$BUILD_ROOT/Makefile"; make -C "$BUILD_ROOT" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 "$BUILD_ROOT/aeq_package_validation_test"; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test; git diff --check` completed successfully. Canonical-path/symlink validation and package-manager replacement/preservation tests passed under ASan/UBSan; only expected offscreen Qt size-hint diagnostics were emitted.
- Scope: full publication rollback fault injection, network/runtime integration and installed-database lifecycle remain open. T08 remains IN_PROGRESS.
- Next safe action: continue T10/T13 ownership and callback-boundary work, with the current sanitizer gate as regression evidence.
- Commit/PR: not committed.

### Luna implementation session — T10/T13 mutex teardown ordering fix — 2026-09-15

- Worker: Codex implementation session.
- Files changed: native `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c` and this roadmap log.
- Implementation: corrected `JamesDSPFree` so ASRC/effect teardown completes while the processing mutex is valid, then unlocks it, and only afterward destroys the mutex. This removes the prior unlock-after-destroy undefined behavior during shutdown.
- Verification: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-asan -B -j2 liblibjamesdsp.a; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed -B -j2 crossfeed_lifecycle_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; git diff --check` completed successfully. The rebuilt native library and concurrent crossfeed lifecycle test passed under ASan/UBSan.
- Scope: callback lock/allocation timing, full reader reclamation design and TSan runtime remain open; T10 and T13 remain IN_PROGRESS.
- Next safe action: continue the bounded T13 callback-boundary audit and refresh dependent rate/crossfeed regressions.
- Commit/PR: not committed.

### Luna implementation session — T15 standalone editor driver boundary

- Worker: Codex implementation session.
- Files changed: `src/subprojects/EELEditor/src/eeleditor.cpp`, `src/subprojects/EELEditor/src/widgets/VariableWatchWidget.cpp` (EELEditor submodule); this roadmap log.
- Implementation: guarded editor close-time unfreeze behind `HAS_JDSP_DRIVER` and a null host check. Guarded the variable-watch audio include and variable manipulation slot so the standalone/non-driver editor does not require `IAudioService` and safely treats variable clicks as unavailable.
- Verification: `src/tests/pipewire_rt_contract_test.sh` -> `PipeWire RT callback contract passed`; `git diff --check` -> passed; `git -C src/subprojects/EELEditor diff --check` -> passed. The standalone qmake build reached all editor sources after the fix; the previous `IAudioService.h` compile failure was eliminated, but the full target remains in progress in `/home/soloarch/Workspace/build/jamesdsp-luna-t15-eeleditor-current`.
- Scope: this closes a non-driver compile/runtime boundary issue; no running application or Flatpak configuration was touched.
- Next safe action: collect the final editor build result, then continue the T13 callback ownership audit and subsequent roadmap tasks.
- Commit/PR: not committed.

### Luna implementation session — T17 CI revision and regression gates

- Worker: Codex implementation session.
- Files changed: `.github/workflows/package-deb.yml` and this roadmap log.
- Implementation: package jobs and verification jobs check out `${{ github.sha }}`; nightly publication is restricted to pushes on `master`. Added native and ASan/UBSan verification jobs covering the native DSP regressions, EEL corpus/manifest checks, repository contracts, AutoEQ/EEL/editor/host/preset tests, and all process-allocation modes. All generated CI artifacts use `build/` paths.
- Verification: `python3 meta/tests/workflow_contract_test.py` -> `workflow contract test passed`; `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t17-packaging-current meta/tests/packaging_contract_test.sh` -> `packaging contract test passed`; `git diff --check` and `git -C src/subprojects/EELEditor diff --check` -> passed.
- Scope: GitHub Actions was not executed locally; T17 remains IN_PROGRESS pending remote workflow execution and acceptance of the full matrix.
- Next safe action: continue dependency-ordered T18 packaging helper checks, then T19 EELVault/native syntax agreement.
- Commit/PR: not committed.

### Luna implementation session — T18/T19 packaging and syntax checks

- Worker: Codex implementation session.
- Verification: `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t17-packaging-current meta/tests/packaging_contract_test.sh` -> `packaging contract test passed`; `PYTHONPATH=/home/soloarch/Workspace python3 -c '...three maintained native syntax checks...'` -> `EELVault native syntax regression checks passed (3)`.
- T19 finding: `python3 -m pytest .../test_eelvault.py` cannot run because pytest is not installed. The dependency-free `run_regressions.py` is also blocked by missing `projects/jamesdsp-liveprog/{awBaxandall,awInterstage,liveprogLifecycleDiagnostic}.eel` fixtures in the external EELVault checkout. The three temp-fixture parser/validator tests pass independently; no external fixtures were fabricated or copied.
- Scope: no Flatpak runtime, installation, publication, or running application interaction was performed. T18 and T19 remain IN_PROGRESS pending environment/fixture restoration and full acceptance.
- Next safe action: continue T20 integrated offline coverage and keep this blocker documented while preserving the external EELVault worktree.
- Commit/PR: not committed.

### Luna implementation session — current T20 offline gate and identity audit

- Worker: Codex implementation session.
- Verification command: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t02-red/untar_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t08-downloader/gzip_downloader_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 /home/soloarch/Workspace/build/jamesdsp-luna-t12/eel_parser_test; src/tests/pipewire_rt_contract_test.sh; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-current meta/tests/packaging_contract_test.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `current integrated T20 offline gate passed`.
- Additional identity: `git rev-parse HEAD` -> `eb848bf507325ecbe765569d37c163d3b7c6fd11`; recursive submodule status recorded EELEditor `b2f392480e00ca232c397610f42688b165b87640` plus the three unavailable optional widget submodules; standalone EELEditor archive build -> success, SHA256 `8d7a41c846825047374ab274664411be24ad91068d80a7e91deae070fbd0e183`.
- Scope: this is an offline gate. Full application GUI/backend/hardware runtime, GitHub Actions, real Flatpak export/install, and external EELVault fixture-dependent tests remain unavailable or pending. The running application was not restarted or modified. T01/T02 remain VERIFIED; T03–T20 remain IN_PROGRESS where acceptance criteria are incomplete or environment-limited.
- Next safe action: continue the remaining dependency-ordered acceptance slices, prioritizing T13 callback/reload ownership and T15 editor signal/runtime coverage while retaining all prior user work.
- Commit/PR: not committed.

### Luna implementation session — T13 PulseAudio callback format caching

- Worker: Codex implementation session.
- Files changed: `src/audio/pulseaudio/wrapper/gstjamesdsp.c`, `src/tests/pulse_wrapper_contract_test.sh`, and this roadmap log.
- Implementation: moved PulseAudio sample-format selection into the non-real-time `gst_jamesdsp_setup()` callback and cached the result under the existing mutex. `transform_ip()` now processes directly from the cached format and no longer calls `gst_pad_get_current_caps()` for every buffer; unsupported format/setup state fails cleanly.
- Verification: `src/tests/pulse_wrapper_contract_test.sh` -> `PulseAudio wrapper contract passed`; `git diff --check` -> passed. Direct syntax command `gcc -fsyntax-only -std=gnu11 $(pkg-config --cflags gstreamer-1.0 gstreamer-audio-1.0) -Ilibjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp -Isrc/audio/pulseaudio/wrapper src/audio/pulseaudio/wrapper/gstjamesdsp.c` was unavailable because GStreamer development packages/pkg-config entries are absent (`Package gstreamer-1.0 ... not found`, `gst/gst.h: No such file`).
- Scope: this addresses a concrete Pulse callback allocation/object-query risk; full backend runtime and GStreamer-linked build remain pending. T13 remains IN_PROGRESS.
- Next safe action: continue from this checkpoint with the available independent native/UI acceptance tasks; retain the missing-GStreamer limitation in T20.
- Commit/PR: not committed.

### Luna implementation session — T13 PulseAudio enable synchronization

- Worker: Codex implementation session.
- Files changed: `src/audio/pulseaudio/wrapper/gstjamesdsp.c`, `src/tests/pulse_wrapper_contract_test.sh`, and this roadmap log.
- Implementation: `gst_jamesdsp_transform_ip()` now locks before reading `enable`, returns with a balanced unlock when bypassed or when buffer mapping fails, and keeps DSP processing protected by the same mutex. This removes the control/audio data race while retaining the cached setup-time format path.
- Verification: `src/tests/pulse_wrapper_contract_test.sh` -> `PulseAudio wrapper contract passed`; `git diff --check` -> passed. GStreamer compilation remains environment-limited because this host has no `gstreamer-1.0`/`gstreamer-audio-1.0` pkg-config entries or `gst/gst.h`.
- Scope: this is a targeted T13 synchronization improvement; PulseAudio runtime and full GStreamer-linked build remain pending. T13 remains IN_PROGRESS.
- Next safe action: continue the remaining T13/T14 acceptance work and refresh the integrated offline gate after any further source changes.
- Commit/PR: not committed.

### Luna implementation session — T14 sanitized corpus refresh

- Worker: Codex implementation session.
- Verification command: `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t14-current; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' DEFINES+=JDSP_TEST_HOOKS -o "$BUILD_ROOT/libjamesdsp.Makefile"; make -C "$BUILD_ROOT" -f libjamesdsp.Makefile -j2; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$BUILD_ROOT/tests.Makefile"; make -C "$BUILD_ROOT" -f tests.Makefile -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 "$BUILD_ROOT/liveprog_runtime_test" resources/assets/liveprog/stftDenoise.eel resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel resources/assets/liveprog/swapChannels.eel resources/assets/liveprog/stereoPhaseInvert.eel` -> all targeted runtime tests passed under ASan/UBSan.
- Corpus evidence: `python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t14-current/liveprog_runtime_test` -> `native EEL corpus expectations passed: 47 files` (45 shipped/custom-independent files plus two explicit expected rejects); `python3 libjamesdsp/tests/compare_eel_manifest.py --skip-external ...` -> `manifest comparison passed: 47 files; ... native_expected_pass=True`, recorded at `/home/soloarch/Workspace/build/jamesdsp-luna-t14-current/eel-corpus-comparison.json`.
- Scope: this provides sanitized numerical coverage for the corrected denoiser coefficient, gain, high-pass stereo isolation and shipped corpus. External Airwindows fixtures, full control/rate/block/noise sweeps, and pre-sanitization internal diagnostics remain open; T14 remains IN_PROGRESS.
- Next safe action: continue remaining T13/T14 coverage without promoting incomplete tasks.
- Commit/PR: not committed.

### Luna implementation session — T17 repository contract wiring

- Worker: Codex implementation session.
- Files changed: `.github/workflows/package-deb.yml` and this roadmap log.
- Implementation: added PipeWire RT, PulseAudio wrapper, visual-theme registry, and packaging-script `bash -n` checks to the CI `verify-native` repository-contract step, alongside the existing workflow and packaging contracts.
- Verification: `python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t17-current meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; bash src/tests/visual_theme_registry_test.sh; bash -n meta/flatpak/build-local-bundle.sh meta/build_deb_package.sh; git diff --check` -> all passed.
- Scope: GitHub Actions execution remains pending; T17 stays IN_PROGRESS until the remote matrix completes.
- Next safe action: continue T20 gate refresh and remaining T13/T15 runtime-independent acceptance work.
- Commit/PR: not committed.

### Luna implementation session — T15 single-source editor reload

- Worker: Codex implementation session.
- Files changed: `src/interface/LiveprogSelectionWidget.cpp` and this roadmap log.
- Implementation: simplified `updateFromEelEditor()` to use the widget’s authoritative `_eelParser` through `setCurrentLiveprog()` exactly once, then emit one `liveprogReloadRequested()` signal. Removed the unused temporary parser and duplicate property reload branch.
- Verification: `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t15-liveprog-current; qmake6 JDSP4Linux.pro 'CONFIG += HEADLESS' 'CONFIG += USE_PIPEWIRE' -o "$BUILD_ROOT/Makefile"; make -C "$BUILD_ROOT" -B -j2` built the headless native library (the first parallel subdirectory pass raced the test dependency before the library existed); `make -C "$BUILD_ROOT/libjamesdsp/tests" -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 "$BUILD_ROOT/libjamesdsp/tests/liveprog_runtime_test" resources/assets/liveprog/gainControl.eel` -> `headless native integration build/test passed`; `git diff --check` -> passed.
- Scope: editor-level signal-spy, tab-switch and full GUI/backend runtime acceptance remain open; T15 remains IN_PROGRESS.
- Next safe action: continue the remaining T13/T15 acceptance slices and refresh T20 evidence.
- Commit/PR: not committed.

### Luna implementation session — T14 deterministic signal stimuli

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation: added analytical gain-fixture checks for silence, opposing impulses, deterministic sine/cosine, step, and seeded-noise stereo blocks. The existing `@block` counter assertion was adjusted from 2 to the derived 7 after the new five blocks were intentionally processed.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-current -f tests.Makefile -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 .../liveprog_runtime_test resources/assets/liveprog/gainControl.eel; ... stftDenoise.eel highpass200Hz.eel` -> all passed; `git diff --check` -> passed.
- Scope: deterministic signal coverage is improved, but the complete parameter/rate/block sweep, Airwindows behavioral checks and pre-sanitization diagnostics remain open. T14 remains IN_PROGRESS.
- Next safe action: continue the remaining T14/T13 acceptance work and refresh the integrated gate.
- Commit/PR: not committed.

### Luna implementation session — T14 control and block-size sweep

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation: generalized the analytical gain assertion and added three intermediate controls (`0.25`, `0.5`, `1.5`) across block lengths `1, 2, 7, 32, 77`, with deterministic stereo ramp inputs. The expected lifecycle counter now reflects all intentionally processed blocks.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-current -f tests.Makefile -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 .../liveprog_runtime_test resources/assets/liveprog/gainControl.eel; ... stftDenoise.eel highpass200Hz.eel` -> all passed; `git diff --check` -> passed.
- Scope: this closes a deterministic gain control/block-size slice; broad rate transitions, Airwindows behavioral checks and internal diagnostic instrumentation remain open. T14 remains IN_PROGRESS.
- Next safe action: continue rate-transition and reload coverage without changing the running application.
- Commit/PR: not committed.

### Luna implementation session — T14 rate-transition stimulus coverage

- Worker: Codex implementation session.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation: extended the analytical gain fixture with rate transitions `44100 → 48000 → 96000 → 48000`, asserting native true-rate state, VM processing rate (`fs` for ASRC rates), and finite output after each transition.
- Verification: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-current -f tests.Makefile -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 .../liveprog_runtime_test resources/assets/liveprog/gainControl.eel` -> `control, block-size and rate-transition regression passed`; `git diff --check` -> passed.
- Scope: this adds deterministic rate coverage but does not close the broader T09/T13 concurrent lifecycle and callback ownership design; T14 remains IN_PROGRESS.
- Next safe action: continue T13/T16 ownership coverage and refresh the integrated gate.
- Commit/PR: not committed.

### Luna implementation session — T16 parent-destruction extraction regression

- Worker: Codex implementation session.
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: added an offscreen test that starts extraction, waits for `decompressionStarted`, then destroys the `GzipDownloader` through its QObject parent. This exercises worker interruption/wait and archive cleanup during parent teardown.
- Verification: `qmake6 src/tests/gzip_downloader_test.pro 'CONFIG += DEBUG_ASAN' -o /home/soloarch/Workspace/build/jamesdsp-luna-t16-current/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-current -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90 /home/soloarch/Workspace/build/jamesdsp-luna-t16-current/gzip_downloader_test` -> exited 0 under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics; `git diff --check` and editor-submodule `git diff --check` -> passed.
- Scope: parent teardown is now directly covered; network/validation cancellation and full install-promise publication remain open. T16 remains IN_PROGRESS.
- Next safe action: continue T08/T16 cancellation boundary coverage and refresh the integrated gate.
- Commit/PR: not committed.

### Luna implementation session — T15 headless application build

- Date: 2026-09-15
- Change: verified the complete headless PipeWire application target after the T15 editor reload and standalone EELEditor driver-boundary updates.
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t15-liveprog-current sub-src -j2` -> exited 0; linked `/home/soloarch/Workspace/build/jamesdsp-luna-t15-liveprog-current/src/jamesdsp` successfully. `file` reported an x86-64 PIE ELF with debug information; SHA256 was `5be498a48ed0ee7de6fc0d22f96027a79fd7577ee616252048e3101c12aa17ca`. `git diff --check` and `git -C src/subprojects/EELEditor diff --check` both exited 0.
- Scope: this validates compilation/linkage only. The binary was not launched, so the running audio application was not restarted or modified; GUI, hardware, and live PipeWire behavior remain open acceptance work. T15 and T20 remain IN_PROGRESS.
- Next safe action: continue dependency-ordered offline acceptance slices and preserve the build artifact under `build/`.

### Luna implementation session — T08 asynchronous package transaction

- Date: 2026-09-15
- Verification command: `qmake6 src/tests/aeq_package_manager_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager -B -j2 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90 /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager/aeq_package_manager_test && git diff --check` -> build completed and test exited 0 (`aeq_package_manager_status=0`). Only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Coverage: local HTTP fixture download, complete package extraction/publication, incomplete replacement rejection, and preservation of the previously installed measurement file all passed under ASan/UBSan. No installed AutoEQ database was read or modified.
- Scope: streamed download boundary failures, deliberate slow cancellation at each UI phase, and full dialog lifecycle remain open. T08 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T15 editor synchronization regression

- Date: 2026-09-15
- Verification command: `qmake6 src/tests/codeeditor_sync_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-sync/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-sync -B -j2 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-sync/codeeditor_sync_test && git diff --check` -> exited 0 (`codeeditor_sync_status=0`).
- Coverage: immediate container synchronization after edits, switching between two loaded documents, and project close/current-file transitions passed under ASan/UBSan. No timing sleep or running application interaction was used.
- Scope: full EELEditor signal-spy Run path, Save As UI path, compiler failure display, and shutdown behavior remain open. T15 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — routine T13/T15/T08 checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; src/tests/pipewire_rt_contract_test.sh; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-sync/codeeditor_sync_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90 /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager/aeq_package_manager_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `routine T13/T15/T08 regression checkpoint passed`.
- Observed diagnostics: four expected offscreen `propagateSizeHints()` messages from the package-manager UI fixture; no sanitizer failure.
- Scope: this is a focused offline checkpoint, not full callback timing, GUI, hardware, package, or running-application validation. T08, T13 and T15 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — native sanitizer regression checkpoint

- Date: 2026-09-15
- Verification commands: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-current -f tests.Makefile -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t14-current/liveprog_runtime_test resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel resources/assets/liveprog/stftDenoise.eel` -> `liveprog runtime test passed`; `qmake6 libjamesdsp/tests/rate_transition_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t14-rate/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-rate -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined' && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t14-rate/rate_transition_test` -> `rate transition test passed`; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test` -> `ASRC capacity test passed`; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test` -> `steady-state process allocation test passed`; crossfeed lifecycle artifact -> `crossfeed lifecycle test passed`; final `git diff --check` passed.
- Boundary: the first rate command used the wrong aggregate Makefile and reported the executable absent; the dedicated `.pro` target with explicit sanitizer link flags passed. This was an artifact/command correction, not a product failure.
- Scope: native T01/T07/T09/T14 regressions are green; full GUI/backend/hardware runtime and remaining roadmap acceptance work remain open. T01 stays VERIFIED; T07, T09 and T14 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — host reload and native regression refresh

- Date: 2026-09-15
- Verification command: `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current/dsp_host_reload_test` -> exited 0 (`dsp_host_reload_status=0`). The test emitted the expected invalid-EQ, missing-file, syntax-error and unreadable-file diagnostics while preserving the last valid Liveprog state.
- Scope: this refresh confirms T05/T06 transactional behavior and supports T13’s reload ownership baseline; it does not prove asynchronous compilation or hard real-time timing. T05/T06/T13 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T03/T11 persistence regression refresh

- Date: 2026-09-15
- Verification command: `for name in preset_file_operations file_selection_widget preset_manager; do build="/home/soloarch/Workspace/build/jamesdsp-luna-t03-${name}"; mkdir -p "$build"; qmake6 "src/tests/${name}_test.pro" -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/${name}_test"; done; git diff --check` -> all three targets built and ran successfully; final output `preset/editor persistence regressions passed`.
- Coverage: identity-aware/self-copy and failure-preserving file operations, widget error/success behavior, preset manager operations, and exactly-once signal checks passed under ASan/UBSan.
- Scope: full GUI navigation and installed configuration runtime remain open. T03 and T11 remain IN_PROGRESS pending their broader acceptance criteria.
- Commit/PR: not committed.

### Luna implementation session — T16 chunked download regression

- Date: 2026-09-15
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: the fake `QNetworkReply` now exposes data incrementally through bounded `readyRead` chunks, and the test completes a 64 KiB chunked archive download before extraction. It asserts exactly one decompression-start signal, exactly one success, no error, and extracted content; existing size-limit, short-write, cancellation, repeated-start, parent-destruction and dialog tests remain in the same sanitizer target.
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked -B -j2 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked/gzip_downloader_test && git diff --check` -> exited 0 (`chunked_downloader_status=0`). Only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: deliberate slow worker cancellation and all UI-phase cancellation permutations remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T17/T18 contract checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-current meta/tests/packaging_contract_test.sh; src/tests/pipewire_rt_contract_test.sh; src/tests/pulse_wrapper_contract_test.sh; src/tests/visual_theme_registry_test.sh; bash -n meta/flatpak/build-local-bundle.sh meta/build_deb_package.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `T17/T18 contract checkpoint passed`.
- Scope: local workflow/packaging, callback, PulseAudio, theme, and shell syntax contracts pass. Actual remote Actions, real Flatpak export, and installed package identity remain unavailable/pending; T17/T18 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 chunk-fixture reset verification

- Date: 2026-09-15
- Change: the deterministic fake reply now resets its read cursor and available-byte window whenever fixture data is replaced, preventing stale stream state from weakening the chunked test.
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked -B -j2 >/tmp/jdsp-t16-build.log 2>&1 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked/gzip_downloader_test && git diff --check` -> build completed; direct test rerun exited 0 (`T16 final chunked test status=0`). Only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: deliberate slow worker cancellation and all UI-phase cancellation permutations remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 slow-worker cancellation regression

- Date: 2026-09-15
- Files changed: `src/subprojects/AutoEqIntegration/ExtractionThread.h`, `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: `ExtractionThread` accepts an optional interruption predicate while retaining the production default of `QThread::isInterruptionRequested()`. The test injects a 2 ms delay at each extraction interruption check, waits until the worker has entered extraction, requests cancellation, waits for `QThread::finished`, and asserts exactly one `Extraction cancelled` result.
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked -B -j2 >/tmp/jdsp-t16-build.log 2>&1 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked/gzip_downloader_test && git diff --check` -> build completed and direct test rerun exited 0 (`T16 slow-worker status=0`). Only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: worker-level cancellation is now deterministic; button/Escape/close behavior during each download/extraction/validation/completion phase and full package-manager shutdown remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 dialog rejection cancellation fix

- Date: 2026-09-15
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloaderDialog.{h,cpp}`, `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Red evidence: the new offscreen active-download rejection test initially aborted at `assert(dialogCancelReply->aborted)`, showing that `QDialog::reject()` could complete the dialog without reaching the downloader’s `closeEvent` cancellation path.
- Implementation: `GzipDownloaderDialog::reject()` now rejects only when closure is allowed, explicitly aborts the downloader, and delegates to `QDialog::reject()`. Extraction-phase rejection remains ignored while `closeAllowed` is false; window-close cleanup retains the existing deferred worker destruction policy.
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked -B -j2 >/tmp/jdsp-t16-build.log 2>&1 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked/gzip_downloader_test && git diff --check` -> build and test exited 0 (`T16 dialog reject status=0`). Only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: explicit active-download reject/Escape-style behavior is now covered. Slow extraction close/reject and validation/completion-phase UI permutations remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 parent headless compile verification

- Date: 2026-09-15
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t15-liveprog-current -B -j2 sub-src >/tmp/jdsp-headless-rebuild.log 2>&1` -> completed and linked the headless `src/jamesdsp` target; a follow-up `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t15-liveprog-current sub-src -j2 >/tmp/jdsp-headless-incremental.log 2>&1` exited 0 (`headless_incremental_status=0`) with only existing duplicate-resource-recipe warnings and no work remaining. `git diff --check` passed.
- Scope: the changed T16 dialog/worker code is validated in the dedicated sanitizer target; this parent build is headless PipeWire and does not include the full PulseAudio/UI package runtime. The binary was not launched and the running audio application was not touched.
- Commit/PR: not committed.

### Luna implementation session — broad regression sweep

- Date: 2026-09-15
- Verification command: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-test/process_allocation_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t14-rate/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current/dsp_host_reload_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked/gzip_downloader_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-sync/codeeditor_sync_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset_file_operations/preset_file_operations_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t03-file_selection_widget/file_selection_widget_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset_manager/preset_manager_test; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-sweep meta/tests/packaging_contract_test.sh; src/tests/pipewire_rt_contract_test.sh; src/tests/pulse_wrapper_contract_test.sh; src/tests/visual_theme_registry_test.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `broad Luna native/Qt/contracts sweep passed`.
- Observed diagnostics: expected invalid-EQ/missing-file/syntax/unreadable-file host diagnostics, and expected offscreen `propagateSizeHints()` messages; no ASan/UBSan failure.
- Scope: this is a local offline sweep. Full GUI/backend/hardware runtime, remote CI, real Flatpak export, and remaining acceptance criteria remain open; no running application was launched or restarted. T03–T20 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T15 parent editor bridge regression

- Date: 2026-09-15
- Files changed: `src/tests/liveprog_editor_bridge_test.cpp`, `src/tests/liveprog_editor_bridge_test.pro`, `src/tests/liveprog_editor_bridge_stubs.cpp` and this roadmap log.
- Implementation: added a focused offscreen target for `LiveprogSelectionWidget::updateFromEelEditor`. It creates two disposable saved scripts, asserts each update adopts the saved path and emits exactly one `liveprogReloadRequested()` signal, and links only the required widget/editor interfaces through test-local EELEditor stubs.
- Verification command: `qmake6 src/tests/liveprog_editor_bridge_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge/Makefile && make -C /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge -B -j2 >/tmp/jdsp-t15-bridge-build.log 2>&1 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90 /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge/liveprog_editor_bridge_test && git diff --check` -> build completed; direct test exited 0 (`T15 bridge direct status=0`). The initial target corrections recorded missing Qt Test, Utils, QCodeEditor, docking, support-object and EELEditor stub symbols; the final target has no sanitizer diagnostics.
- Scope: this proves the parent bridge signal/path contract, not full EELEditor Run/Save As UI behavior or live compilation. T15 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T17 bridge regression CI wiring

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml` and this roadmap log.
- Implementation: the non-sanitized and ASan/UBSan verification jobs now generate, build and run `src/tests/liveprog_editor_bridge_test.pro` in their out-of-source Qt test directories. The sanitizer job runs it with `QT_QPA_PLATFORM=offscreen` and both sanitizer leak/UB checks enabled.
- Verification command: `python3 meta/tests/workflow_contract_test.py; git diff --check` -> exited 0 and printed `workflow bridge-test wiring passed`. The local sanitizer target had already passed as `T15 bridge direct status=0` in `/home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge`.
- Scope: remote GitHub Actions execution and full dependency matrix remain pending; no publication or secret changes were made. T17 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 extraction-phase dialog guard

- Date: 2026-09-15
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: the dialog fixture now calls `reject()` immediately after the reply starts decompression and asserts the result is not `Rejected`, proving the UI cannot abandon an active extraction worker while `closeAllowed` is false.
- Verification command: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked -B -j2 >/tmp/jdsp-t16-build.log 2>&1 && QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-chunked/gzip_downloader_test && git diff --check` completed; direct rerun exited 0 (`T16 extraction-phase dialog status=0`). Only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: download reject and extraction reject protection are covered, along with worker cancellation and teardown. Validation/completion-phase UI permutations and full GUI runtime remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T19 corrected parity checkpoint

- Date: 2026-09-15
- Verification commands: from `/home/soloarch/Workspace`, `python3 projects/eelvault/run_regressions.py; python3 -m py_compile projects/eelvault/eel_parse.py projects/eelvault/validate.py projects/eelvault/test_eelvault.py projects/eelvault/run_regressions.py` -> exited 0 and printed `EELVault dependency-free regression suite passed`; from the isolated worktree, `python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t04-asan/libjamesdsp/tests/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest-comparison.json --skip-external;` followed by JSON inspection -> `manifest_comparison_records=47`, `manifest_native_expected_pass=True`; `git diff --check` and editor-submodule diff check passed.
- Boundary: an initial combined attempt from the wrong directory failed with `FileNotFoundError` for the external runner’s workspace-relative fixture path; no source was changed by that attempt. The corrected commands above are the authoritative result.
- Scope: external fixtures are intentionally excluded from the repository-local comparison artifact; the separately owned EELVault suite passes its three maintained fixtures. T19 remains IN_PROGRESS pending CI integration and broader semantic comparison.
- Commit/PR: not committed.

### Luna implementation session — T17 sanitizer downloader CI wiring

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml` and this roadmap log.
- Implementation: the `verify-native-sanitizers` job now installs `libarchive-dev`, builds `src/tests/gzip_downloader_test.pro` with `CONFIG += DEBUG_ASAN`, and runs it offscreen with leak detection and UBSan halt enabled. This covers T16’s downloader/dialog lifecycle fix in the remote sanitizer sequence.
- Verification command: `python3 meta/tests/workflow_contract_test.py; git diff --check` -> exited 0 and printed `sanitized downloader CI wiring passed`.
- Scope: remote Actions execution, full package/backend matrix, and real runtime behavior remain pending. No publishing or permission changes were made. T17 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T08 deterministic publication-failure regression

- Date: 2026-09-15
- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageValidation.h`, `src/subprojects/AutoEqIntegration/AeqPackageValidation.cpp`, `src/tests/aeq_package_validation_test.cpp` and this roadmap log.
- Implementation: added an optional rename operation to the package publication helper; production uses `QDir::rename` unchanged, while the disposable test injects failure specifically at staging-to-destination publication. The regression verifies the existing database remains complete and the staged replacement remains intact after failure.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t08-publish-injected; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 "$build/aeq_package_validation_test"; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90 /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager/aeq_package_manager_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The focused test and manager integration test passed under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: this closes one deterministic filesystem-failure case, but full package-manager cancellation phase coverage and installed-package/UI acceptance remain open. T08 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — routine T08/T09 backend contract checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t14-rate -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t14-rate/rate_transition_test; src/tests/pipewire_rt_contract_test.sh; src/tests/pulse_wrapper_contract_test.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The sanitizer rate-transition executable and both backend callback contract scripts passed.
- Scope: this refresh confirms the bounded native rate path and static backend callback contracts only; live backend/device behavior, full transition concurrency and remaining T08 package lifecycle criteria remain open. T08 and T09 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T12 parser precision regression refresh

- Date: 2026-09-15
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t12-parser; mkdir -p "$build"; qmake6 src/tests/eel_parser_test.pro -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/eel_parser_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The parser fixture passed under ASan/UBSan; the expected unsupported-variable diagnostic for `missing` was emitted.
- Coverage: fractional and fine-grained steps, nonzero minimum, enum metadata and duplicate suppression, identifier-boundary replacement, comments, and persisted precision were exercised.
- Scope: UI/VM round-trip and malformed-metadata diagnostic-surface acceptance remain open. T12 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T14 maintained corpus refresh

- Date: 2026-09-15
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t14-current; make -C "$build" -f tests.Makefile -B -j2; python3 libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json "$build/liveprog_runtime_test" --skip-external; python3 -m py_compile libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/compare_eel_manifest.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The maintained native corpus runner and Python syntax checks passed.
- Scope: this is a local bundled/additional corpus refresh with external custom fixtures intentionally skipped; complete seeded signal/rate/block sweeps, Airwindows behavioral parity and pre-sanitization diagnostics remain open. T14 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — routine T17/T18 backend and packaging gate

- Date: 2026-09-15
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; meta/tests/packaging_contract_test.sh; PACKAGING_FAKE_LOG=/home/soloarch/Workspace/build/jamesdsp-luna-t18-contract/fake-flatpak.log BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-contract PATH=/home/soloarch/Workspace/upstream/JDSP4Linux-luna/meta/tests:$PATH meta/tests/flatpak; src/tests/pipewire_rt_contract_test.sh; src/tests/pulse_wrapper_contract_test.sh; python3 -m py_compile libjamesdsp/tests/*.py meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Workflow, Debian/Flatpak mocked packaging, PipeWire and PulseAudio contract gates passed.
- Boundary correction: an earlier invocation omitted `PACKAGING_FAKE_LOG` for the fake Flatpak executable and exited 1 under `set -u`; the corrected maintained harness invocation passed. No real Flatpak command, installation, publication or permission change occurred.
- Scope: remote GitHub Actions execution, full dependency matrix and real package/runtime inspection remain open. T17 and T18 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T06 configuration and EQ validation refresh

- Date: 2026-09-15
- Verification command: `set -euo pipefail; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t06-dsp_config_validation_test/dsp_config_validation_test; QT_QPA_PLATFORM=offscreen timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t06-eq_preset_validation_test/eq_preset_validation_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. DSP configuration validation passed under ASan/UBSan and EQ preset validation passed; only expected default-value debug diagnostics appeared.
- Scope: this refresh covers bounded EQ/config input and non-finite rejection fixtures. UI loading and neighboring vector-consumer coverage remain open. T06 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T19 EELVault/native manifest refresh

- Date: 2026-09-15
- Verification command: `set -euo pipefail; cd /home/soloarch/Workspace; python3 projects/eelvault/run_regressions.py; cd /home/soloarch/Workspace/upstream/JDSP4Linux-luna; python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest.json --skip-external; python3 -m py_compile libjamesdsp/tests/compare_eel_manifest.py; test -s /home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest.json; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The dependency-free EELVault suite passed; native/Python comparison covered 47 entries and reported the two documented non-shipping native-incompatible fixtures (`hpfloat.eel`, editor `demo.eel`).
- Boundary corrections: an initial comparison used the output JSON as the native executable; the corrected invocation used the existing `/home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test` artifact. A separate attempted rebuild named a nonexistent `tests.Makefile` and was not used as evidence.
- Scope: external custom fixtures remain intentionally excluded from the repository-local comparison artifact; broader semantic parity and CI integration remain open. T19 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — routine T17/T18/T19 evidence audit

- Date: 2026-09-15
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; meta/tests/packaging_contract_test.sh; PACKAGING_FAKE_LOG=/home/soloarch/Workspace/build/jamesdsp-luna-t18-contract/fake-flatpak.log BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-contract PATH=/home/soloarch/Workspace/upstream/JDSP4Linux-luna/meta/tests:$PATH meta/tests/flatpak; src/tests/pipewire_rt_contract_test.sh; src/tests/pulse_wrapper_contract_test.sh; cd /home/soloarch/Workspace; python3 projects/eelvault/run_regressions.py; cd /home/soloarch/Workspace/upstream/JDSP4Linux-luna; python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t14-runtime/liveprog_runtime_test /home/soloarch/Workspace/build/jamesdsp-luna-t19-manifest.json --skip-external; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. All maintained local workflow, mocked packaging, backend-contract and EELVault/native-manifest checks passed.
- Scope: GitHub Actions execution, complete hosted dependency matrix, real Flatpak export/runtime and installed application behavior remain unverified. T17, T18, T19 and T20 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T03 selection signal regression

- Date: 2026-09-15
- Files changed: `src/tests/file_selection_widget_test.cpp` and this roadmap log.
- Implementation: the disposable FileSelectionWidget test now clears the current selection, selects the source row, and asserts one `fileChanged` signal with the exact source path and matching widget state before exercising bookmark/remove failure paths.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection; qmake6 src/tests/file_selection_widget_test.pro -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/file_selection_widget_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The widget regression passed under ASan/UBSan.
- Scope: broader GUI navigation and installed configuration runtime remain open; T03 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T08 backup-rename failure regression

- Date: 2026-09-15
- Files changed: `src/tests/aeq_package_validation_test.cpp` and this roadmap log.
- Implementation: extended the injectable publication fixture to fail the destination-to-backup rename deterministically and assert that both the installed `old-state` and staged `blocked-state` remain present and unchanged.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t08-publish-injected; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 45 "$build/aeq_package_validation_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The publication validation test passed under ASan/UBSan.
- Scope: asynchronous manager cancellation, complete UI phase matrix and installed-package runtime acceptance remain open. T08 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 extraction close-event regression

- Date: 2026-09-15
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloader.h`, `src/subprojects/AutoEqIntegration/GzipDownloader.cpp`, `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: added a test-only extraction interruption callback so the dialog fixture can hold extraction deterministically. The test now waits for `decompressionStarted`, asserts reject and window close leave the active dialog visible, then verifies normal completion accepts and teardown completes.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close; qmake6 src/tests/gzip_downloader_test.pro -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The deterministic close-event regression and existing downloader/extraction matrix passed under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Additional red evidence: an exploratory variant that cancelled the held extraction and then destroyed the dialog produced an ASan bad-free in the dialog error/teardown path. That variant was removed from the green test, not suppressed; cancellation-through-error-dialog remains an open T16 defect requiring a focused fix.
- Scope: full cancellation phase matrix and dialog shutdown ownership remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 cancellation error-dialog teardown fix

- Date: 2026-09-15
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloaderDialog.cpp`, `src/subprojects/AutoEqIntegration/GzipDownloader.h`, `src/subprojects/AutoEqIntegration/GzipDownloader.cpp`, `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: expected cancellation errors now close the downloader dialog without opening a reentrant modal `QMessageBox`; ordinary errors retain the existing message box. Added a deterministic slow extraction cancellation fixture covering decompression start, cancellation, rejected dialog result and deferred destruction.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close; qmake6 src/tests/gzip_downloader_test.pro -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The complete downloader/extraction/dialog matrix passed under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Root cause evidence: the preceding slow-cancellation variant produced an ASan bad-free during `QMessageBox::critical` reentrancy while the dialog was deferred for destruction; the corrected cancellation fixture passes without sanitizer diagnostics.
- Scope: full application shutdown and real network/backend behavior remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 production compile and board update

- Date: 2026-09-15
- Board update: marked T16’s slow extraction/reject/destruction/normal-completion criterion and cooperative worker ownership criterion checked; phase-matrix cancellation criteria remain unchecked.
- Verification command: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager -B -j2 GzipDownloader.o GzipDownloaderDialog.o; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The production-style AutoEQ downloader and dialog objects compiled successfully; the focused T16 sanitizer test remains green from the preceding checkpoint.
- Boundary: the attempted headless application object target had no AutoEQ object rules because that target excludes the integration; it was not treated as product evidence. No application was launched or restarted.
- Scope: T16 remains IN_PROGRESS pending full cancellation phase matrix and application shutdown/runtime verification.
- Commit/PR: not committed.

### Luna implementation session — T09 invalid-rate rejection regression

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/rate_transition_test.c` and this roadmap log.
- Implementation: extended the native rate-transition fixture to reject zero, negative, NaN, below-minimum and above-maximum rates while asserting both external and internal sample-rate state remain at the previous valid 48 kHz state.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t09-invalid-rate; mkdir -p "$build"; qmake6 libjamesdsp/tests/rate_transition_test.pro -o "$build/Makefile"; make -C "$build" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/rate_transition_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `rate transition test passed`.
- Scope: full backend/device transition matrix, concurrent lifecycle design and live runtime behavior remain open. T09 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T10 crossfeed mode synchronization

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/crossfeed.c`, `libjamesdsp/tests/crossfeed_lifecycle_test.c` and this roadmap log.
- Implementation: serialized crossfeed disable and mode/coefficient updates with the existing processing mutex, and exercised mode changes concurrently with repeated forced crossfeed replacement. The refresh-predicate criterion is now checked on the task board; expensive replacement publication/reclamation remains open for the T13-compatible final design.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t10-mode-stress; mkdir -p "$build"; qmake6 libjamesdsp/tests/crossfeed_lifecycle_test.pro -o "$build/Makefile"; make -C "$build" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/crossfeed_lifecycle_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `crossfeed lifecycle test passed`.
- Scope: TSan remains unavailable in this runtime; constructor work still occurs under the mutex and full reader reclamation/publication design remains open. T10 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 allocation-failure processing policy

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, `libjamesdsp/tests/process_allocation_test.c` and this roadmap log.
- Implementation: all native format entry points now check whether block growth succeeded. If a requested quantum cannot be prepared, they zero the caller’s output buffers and return without touching the previous internal buffer layout; size arithmetic in the silence policy is guarded against overflow. The board’s bounded unexpected-size policy criterion is checked.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-capacity-failure; make -C "$build/test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=pthread_mutex_lock'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/process_allocation_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `steady-state process allocation test passed`. The test includes deterministic failed 4096-frame growth and verified zero output, unchanged pointer/capacity, and the existing no-allocation steady-state loop.
- Boundary correction: the first fresh link omitted effective wrapper flags and failed only with unresolved `__real_*` symbols; the explicit wrapper-preserving link above passed.
- Scope: full callback timing/ownership design, off-callback preparation and TSan evidence remain open. T13 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 all-mode capacity-policy regression

- Date: 2026-09-15
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-capacity-failure; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/process_allocation_test"; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/process_allocation_test" "$mode"; fi; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all seven modes printed `steady-state process allocation test passed`.
- Scope: this confirms the bounded allocation-failure path alongside existing effect-mode steady-state checks; callback timing, off-callback preparation and TSan remain open. T13 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 unified button cancellation path

- Date: 2026-09-15
- Files changed: `src/subprojects/AutoEqIntegration/GzipDownloaderDialog.cpp`, `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: removed the duplicate direct button-to-downloader connection; the UI’s generated `rejected()` connection now routes through the dialog’s single `reject()` policy, which aborts active downloads and respects extraction-phase closure protection. The test clicks the real Abort button and asserts rejected result plus reply abortion.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The complete downloader/dialog matrix passed under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: validation/completion-phase cancellation and full application shutdown/runtime behavior remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T17 version-output workflow hardening

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: replaced all deprecated `::set-output` version steps with `$GITHUB_OUTPUT` writes and added a contract assertion preventing regression to the deprecated command.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/flatpak/build-local-bundle.sh meta/build_deb_package.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; python3 -m py_compile meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `workflow contract test passed`.
- Scope: remote GitHub Actions execution, full dependency matrix and artifact identity validation remain open. T17 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T14 pre-sanitization non-finite diagnostics

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h`, `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/liveprogWrapper.c`, `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation: added a native counter incremented before LiveProg NaN/Inf output is replaced with zero, and a deliberately invalid `0/0` and `1/0` fixture asserts two diagnostics plus zeroed output. This exposes failures hidden by the existing finite-output guard.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$build/test/Makefile"; make -C "$build/test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/liveprog_runtime_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `liveprog runtime test passed`.
- Boundary correction: the first attempt used a nested library output directory incompatible with `tests.pro`; the authoritative rerun placed `liblibjamesdsp.a` at the expected build sibling path.
- Scope: full seeded-noise/rate/block sweeps, Airwindows behavioral checks and all internal diagnostic variables remain open. T14 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — cumulative native/AutoEQ/CI regression checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite/test/liveprog_runtime_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t09-invalid-rate/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t10-mode-stress/crossfeed_lifecycle_test; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-capacity-failure/test/process_allocation_test; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-capacity-failure/test/process_allocation_test "$mode"; fi; done; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close/gzip_downloader_test; python3 meta/tests/workflow_contract_test.py; PACKAGING_FAKE_LOG=/home/soloarch/Workspace/build/jamesdsp-luna-t18-contract/fake-flatpak.log BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-contract PATH=/home/soloarch/Workspace/upstream/JDSP4Linux-luna/meta/tests:$PATH meta/tests/flatpak; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Native LiveProg/rate/crossfeed/all seven allocation modes, AutoEQ dialog lifecycle, workflow and mocked packaging gates all passed; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: this is an offline cumulative checkpoint, not proof of full callback timing, TSan, hosted CI, installed package/runtime, hardware or GUI acceptance. T01 remains VERIFIED; open dependent tasks remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — fresh headless application integration build

- Date: 2026-09-15
- Verification command: `set -euo pipefail; app=/home/soloarch/Workspace/build/jamesdsp-luna-t20-headless-current; lib=/home/soloarch/Workspace/build/libjamesdsp; mkdir -p "$lib"; qmake6 libjamesdsp/libjamesdsp.pro -o "$lib/Makefile"; make -C "$lib" -B -j2; qmake6 src/src.pro 'CONFIG += HEADLESS' 'CONFIG += USE_PIPEWIRE' -o "$app/Makefile"; make -C "$app" -B -j2; file "$app/jamesdsp"; sha256sum "$app/jamesdsp"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Fresh native library and headless PipeWire application linked successfully; `file` reported an x86-64 PIE with debug information and SHA256 `5dd380b3738665cd7cdc3d9343ad908a3827d27206590ab9eaa71141cdc6cde5`.
- Boundary correction: the first app build lacked the sibling native static library expected by `src.pro`; building `/home/soloarch/Workspace/build/libjamesdsp` supplied that dependency. The app binary was not launched, so the running audio application was not restarted or modified.
- Scope: full GUI build, hardware/runtime behavior, installed/package identity and remaining roadmap acceptance work remain open. T20 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T18 Debian preflight and cleanup hardening

- Date: 2026-09-15
- Files changed: `meta/build_deb_package.sh`, `meta/tests/packaging_contract_test.sh` and this roadmap log.
- Implementation: Debian packaging now validates safe version syntax and all required regular/readable inputs before creating staging, and removes its own staging directory on failure/success. The disposable contract test verifies missing inputs and unsafe versions fail without partial staging while preserving existing Flatpak checks.
- Verification command: `set -euo pipefail; meta/tests/packaging_contract_test.sh; bash -n meta/build_deb_package.sh meta/tests/packaging_contract_test.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `packaging contract test passed`.
- Scope: no real Debian package or Flatpak export was produced; package contents/permissions and hosted CI remain open. T18 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T18 real Debian candidate

- Date: 2026-09-15
- Files changed: `meta/build_deb_package.sh` and this roadmap log.
- Implementation: the Debian helper now requests root ownership when permitted, but allows an unprivileged local candidate build to retain caller ownership; root builds still fail closed if ownership cannot be set. The packaged executable mode is explicitly `755`.
- Verification command: `set -euo pipefail; meta/tests/packaging_contract_test.sh; bash -n meta/build_deb_package.sh meta/tests/packaging_contract_test.sh; root=/home/soloarch/Workspace/build/jamesdsp-luna-t18-deb-real-20260915-d; mkdir -p "$root/resources/icons"; cp /home/soloarch/Workspace/build/jamesdsp-luna-t20-headless-current/jamesdsp "$root/jamesdsp"; cp resources/icons/icon.png "$root/resources/icons/icon.png"; cp LICENSE "$root/LICENSE"; chmod 755 "$root/jamesdsp"; (cd "$root" && bash /home/soloarch/Workspace/upstream/JDSP4Linux-luna/meta/build_deb_package.sh 4.01-luna pipewire); dpkg-deb --info "$root/jamesdsp-pipewire_4.01-luna_ubuntu22-04_amd64.deb"; dpkg-deb --contents "$root/jamesdsp-pipewire_4.01-luna_ubuntu22-04_amd64.deb"; compare extracted `/usr/bin/jamesdsp` SHA256 and mode` -> exited 0; contract test printed `packaging contract test passed`, package metadata was `jamesdsp-pipewire` version `4.01-luna`, extracted binary SHA256 matched input `5dd380b3738665cd7cdc3d9343ad908a3827d27206590ab9eaa71141cdc6cde5`, and extracted mode was `-rwxr-xr-x`.
- Boundary correction: the first real candidate attempt exposed the unconditional `chown root:root` failure under the unprivileged local environment; no package was produced by that attempt. The corrected candidate was built under `build/` and was inspected without installation or publication.
- Scope: package contents/selected-binary identity and executable permission now have local candidate evidence; root-owned metadata, real Flatpak export/runtime, hosted CI, and installed runtime remain open. T18 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — sanitizer regression sweep after T18

- Date: 2026-09-15
- Verification command: `set -euo pipefail; for test in /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current/dsp_host_reload_test /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/dsp_config_validation_test /home/soloarch/Workspace/build/jamesdsp-luna-t06-eq_preset_validation_test/eq_preset_validation_test /home/soloarch/Workspace/build/jamesdsp-luna-t08-validation-current/aeq_package_validation_test /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test /home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close/gzip_downloader_test /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge/liveprog_editor_bridge_test; do QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all focused Qt/native regressions passed under ASan/UBSan, with only expected offscreen `propagateSizeHints()` diagnostics and test fixture warnings.
- Boundary correction: the identical first attempt without `QT_QPA_PLATFORM=offscreen` failed on external Wayland teardown allocations (1,152 bytes in 12 libwayland objects); it was not treated as product evidence. The headless rerun passed with no sanitizer diagnostics.
- Scope: this is a cumulative local regression checkpoint; full GUI/backend/hardware, hosted CI, installed package/runtime and remaining T03–T20 acceptance items remain open.
- Commit/PR: not committed.

### Luna implementation session — T17–T19 routine contract checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; python3 -m py_compile libjamesdsp/tests/*.py meta/tests/workflow_contract_test.py; cd /home/soloarch/Workspace; python3 projects/eelvault/run_regressions.py; cd /home/soloarch/Workspace/upstream/JDSP4Linux-luna; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow, packaging, PipeWire, PulseAudio, Python syntax and dependency-free EELVault contracts passed.
- Scope: no hosted CI, Flatpak export/runtime, installation, publication or running application interaction. T17–T20 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T16 Escape and dialog-parent destruction coverage

- Date: 2026-09-15
- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap log.
- Implementation: added offscreen behavior coverage for Escape cancellation and destruction of a dialog’s QWidget parent while extraction is active; the existing button, reject, close, direct downloader-parent and extraction-cancellation cases remain in the same target.
- Verification command: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close/gzip_downloader_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; target rebuilt and printed `T16 Escape and parent-destruction regression passed`, with only expected offscreen `propagateSizeHints()` diagnostics.
- Boundary correction: the first test fixture incorrectly called `deleteLater()` on a stack `QWidget`, and ASan caught the invalid free. The fixture was changed to a heap parent tracked by `QPointer`; the corrected test passed under ASan/UBSan.
- Scope: validation-phase cancellation, repeated full UI start/abort matrix, hosted CI, installed runtime and full application shutdown remain open. T16 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 candidate compilation off the processing lock

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/liveprogWrapper.c`, `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h`, new `libjamesdsp/tests/liveprog_reload_lock_test.c`, new `libjamesdsp/tests/liveprog_reload_lock_test.pro` and this roadmap log.
- Implementation: LiveProg candidate VM initialization and EEL compilation now occur outside `m_in_processing`; the effective DSP rate is captured under lock, and a candidate compiled for a stale rate is rejected at the short publication critical section. The previous VM is swapped atomically and destroyed after unlocking. Test-only deterministic delay hooks prove processing continues on the prior valid program while candidate compilation sleeps.
- Red verification: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-reload-lock-red; qmake6 libjamesdsp/tests/liveprog_reload_lock_test.pro -o "$build/Makefile"; make -C "$build" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined' 2>&1 | tail -25; status=${PIPESTATUS[0]}; test "$status" -ne 0` -> exited 0 because the pre-hook implementation failed to link the intentionally referenced delay hooks (`undefined reference to JamesDSPSetLiveProgLoadDelayForTests` and `JamesDSPLiveProgLoadStartedForTests`).
- Green verification: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-reload-lock-green; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' 'DEFINES += JDSP_TEST_HOOKS' -o "$build/Makefile"; make -C "$build" -f Makefile -B -j2; qmake6 libjamesdsp/tests/liveprog_reload_lock_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/test/Makefile"; make -C "$build/test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/liveprog_reload_lock_test"; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$build/liveprog_runtime_test/Makefile"; make -C "$build/liveprog_runtime_test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/rate_transition_test/Makefile"; make -C "$build/rate_transition_test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/liveprog_runtime_test/liveprog_runtime_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/rate_transition_test/rate_transition_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the new test printed `T13 reload lock and stale-rate regression passed`, and existing LiveProg runtime/rate regressions printed `liveprog runtime test passed` and `rate transition test passed` with no ASan/UBSan diagnostics.
- Scope: this proves the candidate compile/publication lock boundary and stale-rate rejection in the controlled native test; crossfeed/convolver/effect preparation, callback allocation timing, full host/backend integration, TSan and shutdown stress remain open. T13 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 native regression follow-up

- Date: 2026-09-15
- Verification command: `set -euo pipefail; git diff --check; git -C src/subprojects/EELEditor diff --check; pgrep -af 'jamesdsp --tray'` -> exited 0; source and editor-submodule whitespace checks passed, and the existing tray application was observed only (not restarted or modified).
- Scope: the candidate compilation lock-boundary test and native ASan/UBSan regressions were completed in the preceding checkpoint; remaining T13 work is the same-thread-safe reclamation/preparation of convolver and other expensive effect states, plus callback timing/TSan/shutdown evidence.
- Commit/PR: not committed.

### Luna implementation session — T17 CI wiring for T13 reload-lock regression

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: the ASan/UBSan native verification job now builds and executes `liveprog_reload_lock_test` against the hook-enabled native library; the workflow contract test asserts both the build and execution wiring.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; python3 -m py_compile meta/tests/workflow_contract_test.py libjamesdsp/tests/*.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `workflow contract test passed` and `T17 workflow wiring contract passed`.
- Scope: hosted GitHub Actions execution and the full CI dependency matrix remain unverified locally. T17 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 serialize EEL compiler globals

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/liveprogWrapper.c`, `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h`, `libjamesdsp/tests/liveprog_reload_lock_test.c` and this roadmap log.
- Implementation: added a compile-only mutex around candidate VM/compiler setup, while retaining candidate compilation outside the processing mutex. Test-only counters detect concurrent compiler entry.
- Red verification: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-red; qmake6 libjamesdsp/tests/liveprog_reload_lock_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined' 2>&1 | tail -20; status=${PIPESTATUS[0]}; echo "red_build_exit=$status"; test "$status" -ne 0` -> exited 0 because the pre-implementation test intentionally failed to link `JamesDSPLiveProgMaxConcurrentLoadsForTests`.
- Green verification: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green; make -C "$build/test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/liveprog_reload_lock_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `T13 compiler serialization and stale-rate regression passed` with no ASan/UBSan diagnostics. This includes serialized parallel candidates, processing during delayed compilation, and stale-rate rejection with prior-state preservation.
- Scope: convolver/effect preparation and reclamation, callback timing, TSan and shutdown stress remain open. T13 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T17 sanitizer test output-path correction

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml` and this roadmap log.
- Correction: `liveprog_reload_lock_test.pro` is now generated as `reload-lock.Makefile` in the existing native `tests/` directory, where its `$$OUT_PWD/../liblibjamesdsp.a` dependency resolves to the sanitizer library. The workflow runs the resulting test from that directory.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; python3 -m py_compile meta/tests/workflow_contract_test.py libjamesdsp/tests/*.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `workflow contract test passed` and `T13/T17 workflow path and contract verification passed`.
- Scope: the hosted container job itself remains unexecuted locally; T17 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 normal-build configuration correction

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/liveprogWrapper.c` and this roadmap log.
- Boundary correction: the production compile mutex was initially declared only under `JDSP_TEST_HOOKS`; the normal native build caught `liveProgCompileMutex undeclared`. The mutex is now always compiled, while delay/concurrency counters remain test-only.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current; make -C "$build" -B -j2; qmake6 libjamesdsp/tests/tests.pro -o "$build/tests/Makefile"; make -C "$build/tests" -B -j2; timeout 60 "$build/tests/liveprog_runtime_test"; python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json "$build/tests/liveprog_runtime_test"; qmake6 libjamesdsp/tests/rate_transition_test.pro -o "$build/tests/rate.Makefile"; make -C "$build/tests" -f rate.Makefile -B -j2; timeout 60 "$build/tests/rate_transition_test"` -> exited 0; normal native library/test build, 47-file corpus and rate transition passed.
- Sanitizer verification: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green; make -C "$build" -B -j2; make -C "$build/test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/liveprog_reload_lock_test"` -> exited 0 and printed `T13 sanitizer and normal-configuration regression passed`, with no ASan/UBSan diagnostics.
- Scope: convolver/effect preparation and reclamation, callback timing, TSan, shutdown stress and hosted CI remain open. T13 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13 compiler serialization final evidence

- Date: 2026-09-15
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green; make -C "$build/test" -B -j2 LFLAGS='-fsanitize=address -fsanitize=undefined'; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/test/liveprog_reload_lock_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `T13 compiler serialization and stale-rate regression passed` with no ASan/UBSan diagnostics.
- Coverage: two delayed parallel candidates observed maximum compiler concurrency of one; processing completed during a delayed candidate; a candidate compiled against the old rate was rejected after a 44.1 kHz transition; the last valid gain remained processable.
- Correction recorded: an intermediate test assertion was corrected after identifying that the parallel valid candidate intentionally became the current last-good program; no production failure was inferred from that assertion.
- Scope: expensive convolver/effect preparation, callback timing, TSan, shutdown stress and hosted CI execution remain open. T13 remains IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T13/T17 checkpoint after normal-build correction

- Date: 2026-09-15
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; python3 -m py_compile meta/tests/workflow_contract_test.py libjamesdsp/tests/*.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow, shell, Python and whitespace contracts passed after the production mutex was moved outside the test-only section.
- Native verification: `/home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current` rebuilt successfully in normal configuration; `liveprog_runtime_test`, the 47-entry `run_eel_corpus.py --skip-external` sweep, and `rate_transition_test` all exited 0.
- Sanitizer verification: `/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green` rebuilt with ASan and the compiler-serialization/stale-rate regression exited 0 with no sanitizer diagnostics.
- Scope: this confirms normal-build compatibility and the LiveProg candidate path; convolver/other effect preparation and reclamation, callback timing, TSan, shutdown stress and hosted CI execution remain open. T13/T17 remain IN_PROGRESS.
- Commit/PR: not committed.

### Luna implementation session — T03–T07 headless regression checkpoint

- Date: 2026-09-15
- Files changed: this roadmap board and append-only log.
- Verification command: `set -o pipefail; for run in 'QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test' 'QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test' 'QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t06-config/dsp_config_validation_test' 'QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t06-eq_preset_validation_test/eq_preset_validation_test'; do eval "$run"; done; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t03-red/preset_file_operations_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current/dsp_host_reload_test; timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current/tests/liveprog_runtime_test resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t12-parser/eel_parser_test` -> exited 0; all focused T03/T04/T05/T06/T07 regressions passed under headless ASan/UBSan where configured, and native legacy/lifecycle/filter tests passed.
- Board update: T03 identity-safe operations, atomic failure preservation, name validation and focused signal/error matrix are checked. T06 defaults, explicit-existence tracking, bounded EQ validation and malformed-shape fixtures are checked. T07 diagnostic lifecycle control is checked. T03, T04, T05, T06 and T07 remain `IN_PROGRESS` because full application/UI integration, direct host missing-file/disabled-state coverage, and complete cross-rate/filter acceptance are still open.
- Boundary: an earlier non-headless sanitizer run is not used as evidence because external Wayland teardown allocations caused leak diagnostics; this checkpoint uses `QT_QPA_PLATFORM=offscreen`. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T05/T08/T16 cumulative transaction checkpoint

- Date: 2026-09-15
- Files changed: this roadmap board and append-only log.
- Verification command: `set -euo pipefail; for test in /home/soloarch/Workspace/build/jamesdsp-luna-t08-validation-current/aeq_package_validation_test /home/soloarch/Workspace/build/jamesdsp-luna-t08-manager/aeq_package_manager_test /home/soloarch/Workspace/build/jamesdsp-luna-t16-dialog-close/gzip_downloader_test; do QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; AutoEQ validation/publication, manager rollback and downloader cancellation/dialog regressions passed under headless ASan/UBSan with only expected offscreen `propagateSizeHints()` diagnostics.
- Scope: T05 host preservation criteria are now checked from the preceding focused run; T08/T16 remain `IN_PROGRESS` pending full archive-boundary matrix, installed-package/UI acceptance and broader shutdown/runtime validation. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09/T10/T13 regression checkpoint

- Date: 2026-09-15
- Files changed: this roadmap append-only log.
- Verification command: `set -euo pipefail; for test in /home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current/tests/rate_transition_test /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test /home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green/test/liveprog_reload_lock_test; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$test"; done; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; rate transition, crossfeed lifecycle and serialized LiveProg candidate/stale-rate regressions passed, followed by workflow/shell/whitespace checks.
- Scope: T09 bounded native transition, T10 predicate/lifecycle and T13 candidate compiler serialization evidence remains green. T09/T10/T13 remain `IN_PROGRESS` pending backend/TSan/shutdown coverage, expensive effect-state reclamation and hosted execution.
- Commit/PR: not committed.

### Luna implementation session — T14 corpus checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current; python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json "$build/tests/liveprog_runtime_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t14-corpus/liveprog_corpus_test; python3 -m py_compile libjamesdsp/tests/*.py meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> corpus runner passed all 47 entries (45 accepted, `hpfloat.eel` and editor `demo.eel` rejected as expected); Python compilation and hygiene steps were not reached because the separately invoked `/home/soloarch/Workspace/build/jamesdsp-luna-t14-corpus/liveprog_corpus_test` path does not exist.
- Correction/boundary: `rg --files /home/soloarch/Workspace/build | rg '/(liveprog_corpus_test|corpus).*'` found no such binary; the maintained T14 evidence is the corpus runner result, not the missing command. T14 remains `IN_PROGRESS` pending the remaining runtime/corpus acceptance matrix.
- Commit/PR: not committed.

### Luna implementation session — T14 corrected sanitizer evidence

- Date: 2026-09-15
- Verification command: `set +e; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90 /home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite/test/liveprog_runtime_test; status=$?; echo "t14_runtime_exit=$status"; exit "$status"` -> exited 0, printed `liveprog runtime test passed`, and emitted no ASan/UBSan diagnostics.
- Corpus evidence: the completed non-sanitized `python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current/tests/liveprog_runtime_test` run passed all 47 entries (45 accepted and two documented expected rejections). A sanitizer corpus attempt using the same runner exceeded the command wrapper’s approximately one-minute capture window while running one process per file; its piped tail produced no authoritative exit, so it is not claimed as a sanitizer-corpus pass.
- Correction: the previously logged `t14-corpus/liveprog_corpus_test` path was nonexistent; the maintained target is `liveprog_runtime_test`. T14 remains `IN_PROGRESS` pending a reproducible sanitizer corpus invocation and remaining integrated acceptance checks.
- Commit/PR: not committed.

### Luna implementation session — T14 sanitizer corpus batch verification

- Date: 2026-09-15
- Verification command: `set -euo pipefail; native=/home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite/test/liveprog_runtime_test; mapfile -t pass_paths < <(python3 - <<'PY' ... manifest bundled/custom/additional expected-pass paths ... PY); ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$native" "${pass_paths[@]}"; for reject in libjamesdsp/subtree/Main/CLI/hpfloat.eel src/subprojects/EELEditor/src/definitions/demo.eel; do set +e; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$native" "$reject"; status=$?; set -e; test "$status" -ne 0; done` -> 45 expected-pass entries completed in one sanitizer process with `liveprog runtime test passed`; both expected-rejection fixtures exited nonzero (134 via the maintained assertion path) with their parser diagnostics; no ASan/UBSan diagnostics were emitted.
- Board update: T14 `stftDenoise` numerical initialization and pre-guard non-finite instrumentation criteria are checked. Full 50-script inventory, complete stimuli/rate/control matrix, and pinned Airwindows parity remain open; T14 stays `IN_PROGRESS`.
- Correction: batching the expected-pass entries provides authoritative sanitizer corpus evidence without the wrapper timeout encountered by the one-process-per-file runner. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T15 editor regression checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; for test in /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge/liveprog_editor_bridge_test /home/soloarch/Workspace/build/jamesdsp-luna-t15-editor-current/codeeditor_sync_test /home/soloarch/Workspace/build/jamesdsp-luna-t11/codecontainer_save_test /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/codecontainer_save_test/codecontainer_save_test; do QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$test"; done` -> exited 0; editor bridge, fast synchronization and atomic-save targets passed under headless ASan/UBSan. The save tests emitted the expected missing-destination diagnostic while returning success.
- Correction: `/home/soloarch/Workspace/build/jamesdsp-luna-t15-codeeditor/codeeditor_sync_test` was a stale nonexistent path and was not treated as evidence; the current `t15-editor-current` artifact was located and passed. T15 remains `IN_PROGRESS` pending direct Run signal-spy, Save As/UI state, cancellation and shutdown coverage.
- Commit/PR: not committed.

### Luna implementation session — T17–T20 local release-gate checkpoint

- Date: 2026-09-15
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; python3 -m py_compile libjamesdsp/tests/*.py meta/tests/*.py; (cd /home/soloarch/Workspace && python3 projects/eelvault/run_regressions.py); python3 libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json /home/soloarch/Workspace/build/jamesdsp-luna-t13-normal-current/tests/liveprog_runtime_test --skip-external; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow, packaging, PipeWire, PulseAudio, Python, EELVault and manifest-comparison checks passed. Manifest comparison covered 47 files and reported only the two documented native/Python differences (`hpfloat.eel` and editor `demo.eel`).
- Scope: this is local release-gate evidence only. Hosted CI execution, real Flatpak export/runtime, installed package/runtime, complete GUI/backend/hardware behavior and remaining T17–T20 acceptance criteria remain open; statuses stay `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T05 host active-state synchronization

- Date: 2026-09-15
- Files changed: `src/audio/base/DspHost.h`, `src/audio/base/DspHost.cpp`, `src/MainWindow.cpp`, `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Red verification: `set +e; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current -B -j2; status=$?; echo "t05_red_exit=$status"; test "$status" -ne 0` -> exited 0 because the new regression initially referenced the not-yet-added `DspHost::liveprogActive()` API.
- Implementation: added `DspHost::liveprogActive()` and changed the compilation-result UI synchronization to reflect the native active state. Failed replacement therefore leaves the UI active when the last-good VM remains active, while an initial failed load remains inactive.
- Green verification: `set -euo pipefail; qmake6 src/tests/dsp_host_reload_test.pro DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green -o /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current-native/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current-native -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current-native/dsp_host_reload_test` -> exited 0; initial missing reload was inactive, valid reload active, missing/invalid replacements preserved active state and processing, and explicit disable remained covered, with no sanitizer diagnostics.
- Boundary correction: an intermediate run linked `/home/soloarch/Workspace/build/jamesdsp-luna-t05-host-current` against the stale `/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/liblibjamesdsp.a`; direct pointer inspection produced a UBSan misalignment and was discarded. Rebuilding and linking the current native archive removed that ABI mismatch. T05 remains `IN_PROGRESS` pending full MainWindow signal-spy/UI integration evidence.
- Commit/PR: not committed.

### Luna implementation session — T17 sanitizer identity artifact wiring

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: the `verify-native-sanitizers` job now records `GITHUB_SHA`, ref, event, resolved Git HEAD, sanitizer qmake configuration and recursive submodule status in `build/jamesdsp-asan/reports/source-identity.txt`, then uploads that report as `sanitizer-verification-identity-${{ github.sha }}`. The identity step is located after the sanitizer job’s checkout, so the artifact is produced in the correct job.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow contract, shell syntax and whitespace checks passed.
- Scope: hosted Actions execution and actual artifact upload remain unverified locally; package publication and release jobs were not run or changed. T17 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T17 CI out-of-source package builds

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: PulseAudio and PipeWire full/headless package build jobs now generate qmake Makefiles under `build/ci-pulse-${{ matrix.flavor }}` and `build/ci-pipewire-${{ matrix.flavor }}`, build there, strip only the PipeWire selected binary there, and upload those out-of-source paths. Contract assertions cover both qmake and make/artifact paths.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; probe=/home/soloarch/Workspace/build/jamesdsp-luna-workflow-path-probe; make -C "$probe" -n 2>/dev/null | rg -m2 'cd (libjamesdsp|src)/' || true; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow contract and shell/whitespace checks passed, and the qmake probe showed sub-builds rooted under the disposable build directory.
- Scope: hosted package matrix execution, dependency availability and artifact download/DEB integration remain unverified locally. No package publication or installation was performed. T17 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T17 out-of-source build execution checkpoint

- Date: 2026-09-15
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t17-ci-headless; qmake6 JDSP4Linux.pro 'CONFIG += CI' 'CONFIG += HEADLESS' -o "$build/Makefile"; make -C "$build" -j1; status=$?; echo "t17_headless_serial_build_exit=$status"; if [ "$status" -eq 0 ]; then test -x "$build/src/jamesdsp"; sha256sum "$build/src/jamesdsp"; fi; exit "$status"` -> the serialized top-level build completed successfully; selected binary was executable at `build/jamesdsp-luna-t17-ci-headless/src/jamesdsp`, SHA256 `b188b0a5e8b424c469899ec7cbd8fd4c82a4a260a5308fd736da48772fe783a7`.
- Red verification: the first `make -C "$build" -j2` attempt failed because qmake’s nested test target raced the library archive (`No rule to make target .../libjamesdsp.a`). The workflow now uses `-j1` for top-level subdirectory orchestration; native/src compilation remains internally incremental and the corrected build passed.
- Scope: this validates the headless PipeWire-style qmake output layout locally, not both hosted dependency matrices. No package installation, publication, commit, permission change or running-application restart was performed. T17 remains `IN_PROGRESS` pending hosted CI and full matrix execution.
- Commit/PR: not committed.

### Luna implementation session — T18 current headless Debian candidate

- Date: 2026-09-15
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t18-current-deb-20260915b; mkdir -p "$root/resources/icons"; cp /home/soloarch/Workspace/build/jamesdsp-luna-t17-ci-headless/src/jamesdsp "$root/jamesdsp"; cp resources/icons/icon.png "$root/resources/icons/icon.png"; cp LICENSE "$root/LICENSE"; chmod 755 "$root/jamesdsp"; input_sha=$(sha256sum "$root/jamesdsp" | awk '{print $1}'); (cd "$root" && bash /home/soloarch/Workspace/upstream/JDSP4Linux-luna/meta/build_deb_package.sh 4.01-luna pipewire); pkg="$root/jamesdsp-pipewire_4.01-luna_ubuntu22-04_amd64.deb"; dpkg-deb --info "$pkg"; dpkg-deb --contents "$pkg"; extract="$root/extract"; mkdir -p "$extract"; dpkg-deb --extract "$pkg" "$extract"; output_sha=$(sha256sum "$extract/usr/bin/jamesdsp" | awk '{print $1}'); mode=$(stat -c '%a' "$extract/usr/bin/jamesdsp"); test "$input_sha" = "$output_sha"; test "$mode" = 755` -> exited 0; package metadata was `jamesdsp-pipewire` version `4.01-luna`, the extracted binary SHA256 matched `b188b0a5e8b424c469899ec7cbd8fd4c82a4a260a530808fd736da48772fe783a7`, and mode was `755`.
- Boundary: the unprivileged environment retained local package ownership as explicitly warned by the helper; root-owned metadata and installed-package behavior remain unverified. The candidate was built and extracted only under `build/`, without installation or publication. T18 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T17 build race and corrected candidate evidence

- Date: 2026-09-15
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t17-ci-headless; qmake6 JDSP4Linux.pro 'CONFIG += CI' 'CONFIG += HEADLESS' -o "$build/Makefile"; make -C "$build" -j1; status=$?; echo "t17_headless_serial_build_exit=$status"; if [ "$status" -eq 0 ]; then test -x "$build/src/jamesdsp"; sha256sum "$build/src/jamesdsp"; fi; exit "$status"` -> exited 0; executable produced at `build/jamesdsp-luna-t17-ci-headless/src/jamesdsp`, SHA256 `b188b0a5e8b424c469899ec7cbd8fd4c82a4a260a5308fd736da48772fe783a7`.
- Red evidence: the preceding `make -C "$build" -j2` failed with `No rule to make target .../libjamesdsp.a` because qmake’s nested test target raced the library archive. CI top-level package orchestration now uses `-j1`; the corrected local build completed and the source tree remained untouched by qmake outputs.
- T13 boundary: this build does not resolve callback-time buffer growth or expensive convolution refresh. Those require a deliberate preallocation/publication design and remain `IN_PROGRESS`; no speculative callback behavior change was made.
- Commit/PR: not committed.

### Luna implementation session — T12 metadata precision regression

- Date: 2026-09-15
- Files changed: `src/tests/eel_parser_test.cpp` and this roadmap log.
- Implementation/test coverage: expanded the maintained parser fixture with integer-step, 0.001-step, nonzero-minimum, invalid-range and negative-step metadata; exercised minimum-relative quantization to `0.101`, persisted precision, duplicate/list metadata, identifier-boundary `gain` versus `pregain`, commented assignments and CRLF source.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t12-parser-current; qmake6 src/tests/eel_parser_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/eel_parser_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; `T12 metadata precision and validation regression passed`, with no ASan/UBSan diagnostics.
- Scope: full UI/VM round-trip and unsupported-syntax reporting remain open; T12 stays `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T19 EELVault optional-step alignment

- Date: 2026-09-15
- Files changed outside this worktree: `/home/soloarch/Workspace/projects/eelvault/eel_parse.py`, `validate.py`, `test_eelvault.py`, `run_regressions.py`. The EELVault directory has no Git metadata; its edits remain separately identified and uncommitted.
- Implementation: aligned omitted numeric metadata steps with native `EELParser` (`0.1`), retained `1.0` for enum/list controls, and rejected non-finite parsed values during validation. Added focused numeric/list optional-step and malformed/non-finite metadata regressions.
- Verification command: `set -euo pipefail; cd /home/soloarch/Workspace; python3 projects/eelvault/run_regressions.py; python3 -m py_compile projects/eelvault/*.py; cd /home/soloarch/Workspace/upstream/JDSP4Linux-luna; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the dependency-free EELVault suite passed, Python compilation passed and repository/submodule whitespace checks passed. A slow duplicate native manifest comparison was stopped before completion and is not claimed as evidence; prior batch/native comparison evidence remains documented above.
- Scope: this closes the directly demonstrated optional-step/list metadata mismatch. Full native/Python fixture parity, compilation-vs-structural validation boundaries and external fixture coverage remain open; T19 stays `IN_PROGRESS`. No live-library installation, package publication, commit, permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T17 out-of-source DSP host path correction

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation: changed both verification-job `dsp_host_reload_test.pro` invocations to pass `DSP_LIB_DIR="$GITHUB_WORKSPACE/build/.../libjamesdsp"`, avoiding relative archive resolution from the out-of-source Qt test directory. The workflow’s revision checkout, source identity artifact and publication gating remain covered by the contract test.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh meta/tests/flatpak; build=/home/soloarch/Workspace/build/jamesdsp-luna-t17-dsp-host-absolute; qmake6 src/tests/dsp_host_reload_test.pro DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t13-compiler-serialization-green -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/dsp_host_reload_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow/shell contracts and a fresh out-of-source ASan/UBSan host reload test passed. The test exercised missing/invalid replacement preservation and produced no sanitizer diagnostics.
- Scope: hosted Actions execution, full Qt/backend matrix and artifact upload remain unverified locally. No install, publication, commit, Flatpak permission change or running-application restart was performed; T17 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T17 sanitizer matrix linker correction

- Date: 2026-09-15
- Files changed: `.github/workflows/package-deb.yml`, `libjamesdsp/tests/tests.pro`, `libjamesdsp/tests/rate_transition_test.pro`, `libjamesdsp/tests/liveprog_reload_lock_test.pro`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Red evidence: the sanitizer workflow’s command-line `LFLAGS='-fsanitize=address -fsanitize=undefined'` overrode qmake’s target linker flags; `process_allocation_test` then failed to link `__real_malloc`, `__real_calloc`, `__real_realloc` and `__real_pthread_mutex_lock`. `rate_transition_test` also exposed missing sanitizer link flags when using the ASan-instrumented library.
- Implementation: removed the global `LFLAGS` override, made the shared LiveProg/rate/reload-lock test projects sanitizer-aware under `CONFIG += DEBUG_ASAN`, and retained per-target wrapper flags. The workflow contract now asserts the override is absent.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; for makefile in asrc-capacity.Makefile rate-transition.Makefile crossfeed-lifecycle.Makefile process-allocation.Makefile reload-lock.Makefile; do make -C "$root/libjamesdsp/tests" -f "$makefile" -B -j2; done; for test in asrc_capacity_test rate_transition_test crossfeed_lifecycle_test; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/$test"; done; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then args=(); else args=("$mode"); fi; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/process_allocation_test" "${args[@]}"; done; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/liveprog_reload_lock_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$root/libjamesdsp/tests/liveprog_runtime_test" resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel resources/assets/liveprog/stftDenoise.eel resources/assets/liveprog/stereoPhaseInvert.eel; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/qt-tests/dsp_host_reload_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all listed native and host tests passed with no sanitizer diagnostics.
- Scope: this is authoritative local sanitizer evidence, but hosted Actions execution, full PipeWire/PulseAudio matrices, full Qt test matrix and separate TSan coverage remain open. No install, publication, commit, Flatpak permission change or running-application restart was performed; T17 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T17 sanitizer project self-containment

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/tests.pro`, `libjamesdsp/tests/rate_transition_test.pro`, `libjamesdsp/tests/liveprog_reload_lock_test.pro`, `meta/tests/workflow_contract_test.py` and this roadmap log.
- Red evidence: after removing the workflow-wide `LFLAGS` override, the first local sanitizer rebuild failed at `rate_transition_test` with unresolved ASan runtime symbols because that project lacked sanitizer link configuration; the prior `process_allocation_test` failure had already shown the override also removed its required `--wrap` symbols.
- Implementation: made the shared LiveProg, rate-transition and reload-lock test projects explicitly include `sanitize_address sanitize_undefined`, and added workflow-contract assertions for those project definitions. This preserves each target’s own linker flags, including allocation wrappers.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; for makefile in asrc-capacity.Makefile rate-transition.Makefile crossfeed-lifecycle.Makefile process-allocation.Makefile reload-lock.Makefile; do make -C "$root/libjamesdsp/tests" -f "$makefile" -B -j2; done; for test in asrc_capacity_test rate_transition_test crossfeed_lifecycle_test; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/$test"; done; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then args=(); else args=("$mode"); fi; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/process_allocation_test" "${args[@]}"; done; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/liveprog_reload_lock_test"; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging-current meta/tests/packaging_contract_test.sh; python3 -m py_compile meta/tests/*.py libjamesdsp/tests/*.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all rebuilt native sanitizer targets passed, all seven allocation modes passed, packaging contracts passed and hygiene checks passed with no sanitizer diagnostics.
- Scope: hosted Actions execution, full backend/Qt matrix and separate TSan execution remain open. No install, publication, commit, Flatpak permission change or running-application restart was performed; T17 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T18 Debian staging collision safety

- Date: 2026-09-15
- Files changed: `meta/build_deb_package.sh`, `meta/tests/packaging_contract_test.sh` and this roadmap log.
- Implementation: Debian packaging now refuses an existing staging directory or `.deb` output before installing an EXIT cleanup trap. This prevents a failed rerun from deleting pre-existing user files or overwriting an existing candidate.
- Verification command: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging-collision meta/tests/packaging_contract_test.sh; bash -n meta/build_deb_package.sh meta/tests/packaging_contract_test.sh; python3 meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; packaging contract, workflow contract, shell syntax and whitespace checks passed. The collision fixture verified the sentinel remained intact and an existing output path was not accepted.
- Scope: root-owned metadata, installed-package behavior, real Flatpak export/runtime and hosted packaging remain unverified. No installation, publication, commit, Flatpak permission change or running-application restart was performed; T18 remains `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T13 bounded unexpected-quantum policy

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, `libjamesdsp/tests/process_allocation_test.c`, `docs/testing.md` and this roadmap log.
- Implementation: `ensure_processing_capacity` no longer calls `JamesDSPReallocateBlock` from a processing entry point. Capacity growth remains an explicit setup/control-thread operation; an unexpected quantum is handled by the existing bounded silence return in each format wrapper. Added a no-injection oversized-block regression proving the prepared buffer and state remain unchanged and no callback allocation occurs.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' DEFINES+=JDSP_TEST_HOOKS -o "$root/libjamesdsp/Makefile"; make -C "$root/libjamesdsp" -B -j2; qmake6 libjamesdsp/tests/process_allocation_test.pro 'CONFIG += DEBUG_ASAN' LIBS+="-L$root/libjamesdsp" -o "$root/libjamesdsp/tests/process-allocation.Makefile"; make -C "$root/libjamesdsp/tests" -f process-allocation.Makefile -B -j2; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then args=(); else args=("$mode"); fi; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/process_allocation_test" "${args[@]}"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the fresh native sanitizer rebuild and all seven effect-mode allocation tests passed with no sanitizer diagnostics.
- Scope: this closes callback-time buffer growth and records the safe unexpected-size policy. Expensive effect preparation/reclamation, callback timing instrumentation, concurrent state publication and TSan/shutdown coverage remain open; T13 stays `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T14/T17 full sanitizer corpus correction

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Red evidence: the first full-corpus run with the fresh sanitizer binary found an ASan stack-buffer-overflow in the test harness: `left/right[32]` were indexed through the existing 77-frame test case at `liveprog_runtime_test.c:146`. This was a fixture defect, not a native DSP result, so that run was discarded as sanitizer evidence.
- Implementation: expanded the deterministic test arrays to 77 frames, matching the maintained length matrix.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test"; git diff --check; git -C src/subprojects/EELEditor diff --check; python3 -m py_compile libjamesdsp/tests/*.py meta/tests/*.py` -> exited 0; all 47 manifest entries completed under the fresh sanitizer executable: 45 expected-pass scripts passed and `hpfloat.eel` plus editor `demo.eel` were rejected as expected, with no sanitizer diagnostics after the fixture correction.
- Scope: this closes the corpus harness memory defect and provides reproducible full-manifest sanitizer evidence. The external/custom fixture set, analytical stimulus matrix and pinned Airwindows parity remain open; T14 and T17 remain `IN_PROGRESS`.
- Commit/PR: not committed.

### Luna implementation session — T07 high-pass rate/isolation matrix

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation/test coverage: extended the bundled `highpass200Hz.eel` deterministic regression to run left-only and right-only impulses at both 44100 and 48000 Hz. Each run asserts exact zero on the opposite channel and compares the right-channel response against the independently captured left-channel response; the test restores 48000 Hz afterward.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/liveprog_runtime_test" resources/assets/liveprog/highpass200Hz.eel; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; high-pass runtime test passed under ASan/UBSan with no diagnostics.
- Scope: higher device rates through the internal-rate contract and the broader T14 stimulus/control matrix remain open. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T03 deterministic file-operation failures

- Date: 2026-09-15
- Files changed: `src/tests/preset_file_operations_test.pro`, `src/tests/preset_file_operations_test.cpp` and this roadmap log.
- Implementation/test coverage: enabled `JDSP_TEST_HOOKS` in the focused file-operation target and exercised injected atomic-copy, rename and remove failures. Assertions verify the existing destination bytes, source bytes and rename directory entries remain unchanged; existing self-copy, hardlink, symlink alias, missing-source, invalid-destination and safe-name cases remain covered.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t03-fileops-current; qmake6 src/tests/preset_file_operations_test.pro -o "$build/Makefile"; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/preset_file_operations_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the fresh ASan/UBSan target passed with no sanitizer diagnostics.
- Scope: baseline historical data-loss reproduction, full UI/IPC signal-count coverage and application build remain open; T03 stays `IN_PROGRESS`. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T06 compander vector validation

- Date: 2026-09-15
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Implementation/test coverage: extended the host configuration regression with valid 14-value compander response data, a short 3-value response and a 14-value response containing `nan`. Each update is followed by native processing and finite-output assertions; malformed/non-finite vectors exercise the documented default-response fallback.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t06-compander-current; qmake6 src/tests/dsp_host_reload_test.pro 'CONFIG += DEBUG_ASAN' DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local/libjamesdsp -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/dsp_host_reload_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; fresh host test passed under ASan/UBSan with expected validation warnings and no sanitizer diagnostics.
- Scope: offscreen full-UI loading and broader IPC fixture routing remain open; T06 stays `IN_PROGRESS`. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09/T15 sanitizer checkpoint

- Date: 2026-09-15
- Files changed: this roadmap log only.
- T09 verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/rate-transition.Makefile"; make -C "$root/libjamesdsp/tests" -f rate-transition.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/rate_transition_test"` -> exited 0; fresh target printed `rate transition test passed` with no ASan/UBSan diagnostics.
- T09 scope: this confirms the current bounded native transition matrix, including concurrent processing, 44.1/48/96 kHz state, invalid-rate retention and forced refresh completion. Backend/device integration, TSan and shutdown coverage remain open; T09 stays `IN_PROGRESS`.
- T15 verification command: `set -euo pipefail; for spec in src/tests/liveprog_editor_bridge_test.pro src/tests/codeeditor_sync_test.pro; do name=$(basename "$spec" .pro); build=/home/soloarch/Workspace/build/jamesdsp-luna-${name}-current; mkdir -p "$build"; qmake6 "$spec" 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/$name"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; both offscreen bridge and code-editor synchronization binaries passed under ASan/UBSan with no diagnostics.
- T15 scope: the current focused bridge and fast-editor synchronization tests are green. Full Run signal-spy/host compilation-result coverage, failed-save and cancellation/tab/reopen/shutdown permutations remain open; T15 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T16 cancellation sanitizer checkpoint

- Date: 2026-09-15
- Files changed: this roadmap log only.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-gzip-downloader-current; mkdir -p "$build"; qmake6 src/tests/gzip_downloader_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$build/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the fresh downloader/dialog target passed under ASan/UBSan. Qt offscreen emitted only `propagateSizeHints()` plugin notices.
- Scope: bounded downloads, write/size failures, repeated abort, extraction cancellation, parent destruction, dialog button and Escape teardown are green. Deliberately slow-worker coverage and the complete download/extraction/validation/completion phase matrix remain open; T16 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — current integrated T20 offline gate

- Date: 2026-09-15
- Files changed: this roadmap log only.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/asrc_capacity_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/rate_transition_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/crossfeed_lifecycle_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/liveprog_reload_lock_test"; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-gzip-downloader-current/gzip_downloader_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t12-parser-current/eel_parser_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t03-fileops-current/preset_file_operations_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t06-compander-current/dsp_host_reload_test; src/tests/pipewire_rt_contract_test.sh; src/tests/pulse_wrapper_contract_test.sh; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t20-packaging-current meta/tests/packaging_contract_test.sh; git diff --check; git -C src/subprojects/EELEditor diff --check; echo 'current integrated T20 offline gate passed'` -> exited 0 and printed `current integrated T20 offline gate passed`. Native tests, downloader, parser, file-operation, host reload and packaging/backend/workflow contracts passed; expected validation warnings and Qt offscreen plugin notices were non-fatal.
- Scope: this is a current offline evidence checkpoint, not T20 completion. Fresh full application build, complete Qt/backend/device/package matrix, installed identity/permission inspection and authorized runtime verification remain open. The worktree contains pre-existing and roadmap changes across the listed task files; no unrelated edits were removed.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T13 callback preparation boundary

- Date: 2026-09-15
- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h`, `libjamesdsp/tests/process_allocation_test.c`, `docs/testing.md` and this roadmap log.
- Implementation: removed all format-wrapper `blockSize != n` refresh/setup branches. Processing now only checks prepared capacity and uses the bounded silence path for an unexpected quantum. `JamesDSPReallocateBlock` now commits `blockSize` and refreshes block-size-dependent state only after successful allocation; failed allocation preserves the prior buffer, capacity and runtime block size. Added a deterministic refresh-call counter to assert the smaller-quantum processing path does not invoke refresh.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' DEFINES+=JDSP_TEST_HOOKS -o "$root/libjamesdsp/Makefile"; make -C "$root/libjamesdsp" -B -j2; qmake6 libjamesdsp/tests/process_allocation_test.pro 'CONFIG += DEBUG_ASAN' LIBS+="-L$root/libjamesdsp" -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; for mode in default compressor bass reverb stereo tube crossfeed; do if [ "$mode" = default ]; then args=(); else args=("$mode"); fi; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/process_allocation_test" "${args[@]}"; done; for spec in asrc_capacity_test.pro rate_transition_test.pro crossfeed_lifecycle_test.pro; do name=${spec%.pro}; qmake6 "libjamesdsp/tests/$spec" 'CONFIG += DEBUG_ASAN' LIBS+="-L$root/libjamesdsp" -o "$root/libjamesdsp/tests/$name.Makefile"; make -C "$root/libjamesdsp/tests" -f "$name.Makefile" -B -j2; done; for test in asrc_capacity_test rate_transition_test crossfeed_lifecycle_test; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/$test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all seven allocation modes and dependent ASRC/rate/crossfeed regressions passed with no ASan/UBSan diagnostics.
- Scope: this closes callback-time buffer growth and callback-triggered block-size refresh for the native format wrappers. Full callback timing/p99 instrumentation, complete off-callback candidate/effect reclamation, concurrent publication and shutdown/TSan evidence remain open; T13 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T14 denoiser numerical rate matrix

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation/test coverage: the `stftDenoise.eel` fixture now reloads at 48 kHz and 44.1 kHz, verifies `srate`, checks the analytical `nes1.a = exp(-(2048/2/rate)/0.9)` coefficient and range, and runs 40 deterministic 77-frame sine/step blocks with finite, bounded output assertions.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test" resources/assets/liveprog/stftDenoise.eel; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 -u libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test"; python3 -m py_compile libjamesdsp/tests/*.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; focused denoiser test passed and the 47-file in-tree manifest run passed: 45 expected-valid scripts and two expected parser/compiler rejections, with no ASan/UBSan diagnostics. The previously recorded full 50-file run remains the external/custom corpus evidence.
- Scope: this closes the demonstrated denoiser initialization rate/stimulus regression. Full per-script inventory with parameter/channel contracts, all stimulus classes across every shipping script and pinned Airwindows parity remain open; T14 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T14 corpus inventory

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/inventory_eel_corpus.py` and this roadmap log.
- Implementation: added a maintained inventory validator/emitter driven by `eel_corpus_manifest.json`. It resolves all bundled, custom and additional entries, records role and expected compile outcome, parses declared controls, records lifecycle sections, derives channel-coupling hints and records host-rate/large-state/imported-table initialization dependencies. The JSON artifact is written only under `build/`.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t14-inventory; python3 libjamesdsp/tests/inventory_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json --output "$build/eel_corpus_inventory.json"; python3 -m json.tool "$build/eel_corpus_inventory.json" >/dev/null; python3 -m py_compile libjamesdsp/tests/inventory_eel_corpus.py; wc -l "$build/eel_corpus_inventory.json"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; inventory reported 50 files, 103 controls, 48 expected passes and 2 expected rejects; JSON syntax, Python compilation and whitespace checks passed.
- Scope: the 50-entry inventory requirement is now checked. It records static behavioral hints rather than proving runtime channel semantics; full silence/impulse/sine/step/noise matrix, control sweeps and pinned Airwindows parity remain open, so T14 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T14 corpus stimulus sweep

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap log.
- Implementation/test coverage: every manifest entry expected to compile now receives deterministic silence, left impulse, right impulse, sine, step and seeded-noise 32-frame exercises with finite-output assertions. Script-specific high-pass isolation, denoiser coefficient/time-response and non-finite diagnostics remain in place.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 -u libjamesdsp/tests/run_eel_corpus.py --skip-external libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test" >"$root/t14-stimuli-corpus.log" 2>&1; python3 -m py_compile libjamesdsp/tests/*.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all 47 in-tree entries passed, including 45 expected-valid scripts and two expected parser/compiler rejections, with no ASan/UBSan diagnostics.
- Scope: this adds the required stimulus classes across the in-tree corpus but does not yet sweep every control at min/default/max, all block/rate/reload combinations, or provide pinned Airwindows parity. T14 remains `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T14 custom stimulus sweep

- Date: 2026-09-15
- Files changed: this roadmap log only.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; for script in /home/soloarch/Workspace/projects/jamesdsp-liveprog/awBaxandall.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/awInterstage.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; do ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test" "$script"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all three external/custom scripts passed the five-stimulus runtime sweep under ASan/UBSan with no diagnostics.
- Scope: combined with the 47-entry in-tree sweep, the current five-stimulus evidence covers all 50 manifest entries. Control extrema/rate/reload permutations and pinned Airwindows numerical parity remain open; T14 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T15 execution signal-spy route

- Date: 2026-09-15
- Files changed: `src/tests/liveprog_editor_bridge_test.cpp` and this roadmap log.
- Implementation/test coverage: extended the headless bridge fixture to instantiate the editor signal source, connect `EELEditor::executionRequested` to the same `LiveprogSelectionWidget::updateFromEelEditor` route used by `MainWindow`, emit one request and assert exactly one additional reload plus path adoption. Existing direct-path and duplicate-reload assertions remain.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge-signal; mkdir -p "$build"; qmake6 src/tests/liveprog_editor_bridge_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$build/liveprog_editor_bridge_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; signal route passed under ASan/UBSan with no diagnostics.
- Scope: this proves the editor execution signal reaches the selection/reload route exactly once in the headless bridge fixture. Full MainWindow-linked compiler success/failure, failed-save, cancellation, reopen and shutdown permutations remain open; T15 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T17 local event/revision contract

- Date: 2026-09-15
- Files changed: `meta/tests/workflow_contract_test.py` and this roadmap log.
- Implementation/test coverage: extended the workflow contract to require PR and master-push triggers, manual dispatch, published/prereleased release triggers, `${{ github.sha }}` source checkouts, and release/dispatch-only publication jobs. Existing assertions continue to cover sanitizer/native jobs, identity artifacts and out-of-source paths.
- Verification command: `set -euo pipefail; python3 meta/tests/workflow_contract_test.py; python3 -m py_compile meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow contract passed.
- Scope: local event/revision behavior is now checked and the corresponding T17 board item is marked complete. Hosted Actions execution and the full Qt/backend matrix remain unverified; T17 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T18 Flatpak binary identity fixture

- Date: 2026-09-15
- Files changed: `meta/tests/packaging_contract_test.sh` and this roadmap log.
- Implementation/test coverage: expanded the maintained fake-Flatpak fixture to assert the inner staging command installs the selected `$binary`, stages its exact parent directory read-only, and reports the SHA256 of a custom executable override. Existing `/usr/bin/true`, path-with-spaces, required-file, fail-fast and Debian collision checks remain.
- Verification command: `set -euo pipefail; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging-identity meta/tests/packaging_contract_test.sh; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh; python3 meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; packaging and workflow contracts passed, shell syntax and whitespace checks passed.
- Scope: T18’s maintained override/identity, fail-fast and documented-path checks are now complete. Real Flatpak runtime/export, package permissions/root ownership and installed-package inspection remain open; T18 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T19 lifecycle-order parity

- Date: 2026-09-15
- Files changed outside this worktree: `/home/soloarch/Workspace/projects/eelvault/validate.py`, `test_eelvault.py`; this roadmap log. The EELVault directory has no Git metadata and remains separately identified.
- Red evidence: temporary fixture `@sample // run audio` followed by `@init` returned native `liveprog_runtime_test` exit 0, while Python validation initially returned invalid solely for the order diagnostic.
- Implementation: changed EELVault’s non-canonical section-order diagnostic to a warning stating that the native parser accepts it; duplicate, malformed, unsupported-section and metadata validation errors remain errors. Added a permanent Python regression for the native-accepted order.
- Verification command: `set -euo pipefail; cd /home/soloarch/Workspace; PYTHONPATH=/home/soloarch/Workspace python3 -m projects.eelvault.test_eelvault; python3 projects/eelvault/run_regressions.py; python3 -m py_compile projects/eelvault/*.py; cd /home/soloarch/Workspace/upstream/JDSP4Linux-luna; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; EELVault tests, dependency-free regression suite, Python compilation and repository/submodule whitespace checks passed. Native comparison was independently observed with the current ASan/UBSan native runtime test and returned success.
- Scope: T19 lifecycle-order reproduction and structural alignment are complete. Full native/Python fixture parity and intentional compiler/runtime differences remain open; T19 stays `IN_PROGRESS`. No installation was performed against the live EEL library.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T19 current 50-file native/Python comparison

- Date: 2026-09-15
- Files changed: this roadmap log only.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; out=/home/soloarch/Workspace/build/jamesdsp-luna-t19-current-comparison.json; timeout 300 python3 -u libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test" "$out" > /home/soloarch/Workspace/build/jamesdsp-luna-t19-current-comparison.log 2>&1; python3 -c "import json; d=json.load(open('$out')); assert d['count'] == 50 and d['native_expected_pass']; print('records=', d['count'], 'native_expected_pass=', d['native_expected_pass']); print('differences=', [(x['path'], x['python_valid'], x['native_pass']) for x in d['files'] if x['python_valid'] != x['native_pass']])"` -> exited 0; comparison reported 50 records, native expected outcomes passed, and exactly two intentional structural/native differences: non-shipping `hpfloat.eel` and editor `demo.eel` are Python-valid but native-rejected.
- Scope: the same current 50 fixtures are now checked by native runtime outcome and Python structural validation, with intentional differences recorded. T19’s broader semantic/compiler boundary documentation remains open; T19 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T20 final isolated headless build and regression refresh — 2026-09-15

- Date: 2026-09-15
- Files changed: `libjamesdsp/tests/tests.pro`, this roadmap log. The project file change removes a redundant out-of-source `PRE_TARGETDEPS` path; the top-level qmake project already supplies the `libjamesdsp` dependency.
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; qmake6 JDSP4Linux.pro 'CONFIG += CI' 'CONFIG += HEADLESS' -o "$build/Makefile"; make -C "$build" -j2` -> first clean configure failed at make dependency resolution because `libjamesdsp/tests/tests.pro` named the library as `tests/../liblibjamesdsp.a`; after removing that redundant dependency, the fresh rerun exited 0 and produced `$build/src/jamesdsp` and `$build/libjamesdsp/tests/liveprog_runtime_test`.
- Verification command: `set +e; out=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/regression-rerun.log; timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t13-capacity-failure/test/process_allocation_test; timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t13-reload-lock-green/test/liveprog_reload_lock_test; timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t09-invalid-rate/rate_transition_test; timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge-signal/liveprog_editor_bridge_test; timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp/tests/liveprog_runtime_test` -> allocation, reload-lock, rate-transition, crossfeed and 50-file runtime tests exited 0. The editor bridge initially exited 1 only under the default QtWayland platform because LeakSanitizer reported 1,152 bytes in 12 libwayland-client allocations; `QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s /home/soloarch/Workspace/build/jamesdsp-luna-t15-bridge-signal/liveprog_editor_bridge_test` exited 0.
- Verification command: `git rev-parse HEAD; git submodule status --recursive; sha256sum libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/crossfeed.c src/MainWindow.cpp meta/flatpak/build-local-bundle.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; source revision is `eb848bf507325ecbe765569d37c163d3b7c6fd11`, recursive submodule states were recorded, and hashes were captured for representative native/UI/package files.
- Scope: fresh headless application/native build and current offline regressions are green. Full GUI build remains unavailable because the three optional GUI submodules are absent; hosted GitHub Actions, real Flatpak export/install and installed runtime identity remain unverified. The default-platform editor leak is environmental and the offscreen sanitizer route is the maintained headless evidence. T20 remains `IN_PROGRESS` until the remaining package/runtime/handoff criteria are satisfied.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T20 build identity artifact — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; { echo build_identity; echo "build_dir=$build"; echo 'config=CI HEADLESS'; sha256sum "$build/src/jamesdsp" "$build/libjamesdsp/tests/liveprog_runtime_test"; echo 'source_revision=eb848bf507325ecbe765569d37c163d3b7c6fd11'; git submodule status --recursive; command -v flatpak-builder || echo flatpak-builder=unavailable; command -v flatpak || echo flatpak=unavailable; command -v dpkg-deb || echo dpkg-deb=unavailable; } > "$build/build-identity.txt"` -> exited 0. The artifact records app hash `6d3b5feca4798a5a9460205f9b49cb4a030c30da5d3d1bb7e3619db5f52eb254`, native runtime-test hash `4c8b267b5dfd6c012365a59698c90c155e1db2764aad1077212cbfa084906872`, source revision and recursive submodule states.
- Scope: `flatpak` and `dpkg-deb` are present, but `flatpak-builder` is unavailable; no package was exported or installed. The identity artifact is under `build/`, and package permission/runtime equivalence remains pending.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T12 canonical metadata and UI precision — 2026-09-15

- Date: 2026-09-15
- Files changed: `src/data/EelParser.h`, `src/interface/LiveprogSelectionWidget.cpp`, `src/tests/eel_parser_test.cpp`, `src/tests/liveprog_editor_bridge_test.cpp` and this roadmap log. The parser implementation was unchanged in this slice.
- Red evidence: `qmake6 src/tests/eel_parser_test.pro 'CONFIG += DEBUG_ASAN' ...; timeout 30s .../eel_parser_test` exited 134 on `offgridNumber->getDefault() == 0.12`, demonstrating that an off-grid declared default was exposed unquantized. The UI bridge test similarly exited 134 on `precisionSlider->minimum() == 100` before the scaling change.
- Implementation: `EELNumberRangeProperty::getDefault()` now quantizes defaults from the declared minimum by the positive step and clamps them. Liveprog numeric sliders derive a decimal scale from min/max/step, use that scale for range/value/divisor, and set a unit step; this preserves 0.001 controls instead of truncating them to hundredths.
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t12-red-default; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s "$build/eel_parser_test"; build=/home/soloarch/Workspace/build/jamesdsp-luna-t12-ui-red; make -C "$build" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s "$build/liveprog_editor_bridge_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; parser and offscreen UI bridge tests passed with ASan/UBSan and the expected declared range `100..1000` for a `0.001` step.
- Scope: T12 metadata/quantization and UI precision checklist items are now evidenced. Identifier-boundary replacement beyond the existing exact-key/comment coverage, string-awareness and unsupported editable construct diagnostics remain open; T12 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T20 post-T12 rebuild — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; make -C "$build" -j2 >"$build/make-after-t12.log" 2>&1; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180s "$build/libjamesdsp/tests/liveprog_runtime_test" >"$build/runtime-after-t12.log" 2>&1; sha256sum "$build/src/jamesdsp" "$build/libjamesdsp/tests/liveprog_runtime_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the current source rebuilt successfully and the 50-file native runtime sweep passed under ASan/UBSan. Updated app hash: `3e90ee7c4a5ae3b60cc6b326592c632e871dc46274dfc7894432d3774915339`.
- Scope: this refresh confirms T12 changes did not regress the integrated native corpus. T20 remains `IN_PROGRESS` for unavailable full GUI/hosted CI/real package and installed-runtime verification; no runtime process was launched.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T12 comment/string-aware assignment matching — 2026-09-15

- Date: 2026-09-15
- Files changed: `src/data/EelParser.cpp`, `src/tests/eel_parser_test.cpp` and this roadmap log.
- Red evidence: `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t12-red-default -B -j2; timeout 30s .../eel_parser_test` exited 134 because the matcher treated `gain = 8;` inside a block comment as the declared value, causing the valid property to disappear.
- Implementation: added a length-preserving lexer mask for line/block comments and quoted strings; matching uses the mask while replacement offsets still address the original source. Exact-key boundary, `pregain`, line-comment and block-comment behavior is now regression-tested without rewriting unrelated bytes.
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t12-red-default; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s "$build/eel_parser_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; parser test passed under ASan/UBSan.
- Scope: T12 parser metadata, quantization, precision and assignment matching requirements are evidenced. Unsupported editable-construct diagnostics remain open, so T12 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T12 unsupported-edit diagnostic — 2026-09-15

- Date: 2026-09-15
- Files changed: `src/data/EelParser.h`, `src/data/EelParser.cpp`, `src/tests/eel_parser_test.cpp` and this roadmap log.
- Red evidence: `qmake6 src/tests/eel_parser_test.pro 'CONFIG += DEBUG_ASAN' ...; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t12-diagnostic-red -B -j2` exited 2 because the new regression required the absent `EELParser::getDiagnostics()` API. The fixture uses `expression = other;`, which is not safely editable as a numeric control.
- Implementation: added a read-only diagnostics list reset per load and report entries when a declared control has no editable numeric assignment. The source remains unchanged and the unsupported construct is not silently treated as a valid UI control.
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t12-diagnostic-red; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s "$build/eel_parser_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the parser regression passed under ASan/UBSan and emitted diagnostics for both missing and expression-valued declarations.
- Scope: all T12 card checkboxes are now evidenced. T12 remains `IN_PROGRESS` pending stronger VM/disk/UI canonical-value round-trip coverage and downstream integration review.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T20 post-T12 diagnostic integration — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; make -C "$build" -j2 >"$build/make-after-t12-diagnostics.log" 2>&1; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180s "$build/libjamesdsp/tests/liveprog_runtime_test" >"$build/runtime-after-t12-diagnostics.log" 2>&1; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; final headless application/native targets rebuilt and the 50-file runtime corpus passed under ASan/UBSan. Current app hash is `6bbe8b8443659aea8fb54f95ed62ec11284c57df568219c92bbc93a4211b34fb`.
- Scope: the T12 diagnostic changes integrate without native corpus regression. T12 remains `IN_PROGRESS` for stronger VM/disk/UI canonical round-trip evidence; T20 remains `IN_PROGRESS` for unavailable GUI, hosted CI, package export/install and installed-runtime evidence.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T03 manager integration boundary — 2026-09-15

- Date: 2026-09-15
- Red evidence: an isolated manager-level fixture was attempted with `PresetManager`, a temporary `XDG_CONFIG_HOME`, and the preset-directory override. Its first compile required QtDBus and additional model sources; after adding those dependencies, the link failed on `RouteListModel::makeDefaultRoute`, `PresetRuleTableModel::containsDeviceAndRouteId` and related Qt meta-object symbols. This harness expansion was removed so the maintained helper target remains valid.
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t03-manager; qmake6 src/tests/preset_file_operations_test.pro 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30s "$build/preset_file_operations_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the original SafeFileOperations regression passed under ASan/UBSan after rollback of the unmaintainable manager harness.
- Scope: T03’s helper-level identity, atomic-copy, failure-preservation, safe-name and rename/remove coverage remains green. Direct `PresetManager`/UI signal integration is still open; the missing GUI submodule/full application dependency boundary is recorded rather than hidden.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T03 current focused regression refresh — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s /home/soloarch/Workspace/build/jamesdsp-luna-t03-file-selection/file_selection_widget_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60s /home/soloarch/Workspace/build/jamesdsp-luna-t03-preset/preset_file_operations_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; current widget, manager and lower-level filesystem regressions passed under ASan/UBSan.
- Scope: this refresh confirms the existing T03 identity-aware copy, bookmark, rename/remove, failure-injection and manager fixtures remain green. The original destructive baseline is retained as review evidence in F03; full application-linked UI and permission/ownership behavior remain open, so T03 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T04 current legacy/lifecycle parameter refresh — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90s "$root/libjamesdsp/tests/liveprog_runtime_test" resources/assets/liveprog/gainControl.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `liveprog runtime test passed`; the current hook-enabled native build exercised the legacy derived-gain and lifecycle slider paths under ASan/UBSan.
- Scope: this refresh confirms the T04 native parameter behavior remains green after T12 parser/UI changes. Qt-level selected/persisted/reset/list/disabled signal coverage remains open; T04 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T05 rejected-candidate diagnostic preservation — 2026-09-15

- Date: 2026-09-15
- Files changed: `src/tests/dsp_host_reload_test.cpp` and this roadmap log.
- Implementation/test coverage: the direct headless host fixture now asserts that missing-file and syntax-rejected candidates publish non-empty compiler diagnostic text (including the missing-file reason) while `liveprogActive()` and the previous gain output remain unchanged.
- Verification command: `set -o pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t05-diagnostics-current; qmake6 src/tests/dsp_host_reload_test.pro DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp 'CONFIG += DEBUG_ASAN' -o "$build/Makefile"; make -C "$build" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90s "$build/dsp_host_reload_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the ASan/UBSan host fixture passed and emitted expected missing/syntax diagnostics.
- Scope: candidate diagnostic lifetime and preserved native state are now directly asserted. UI-level status rendering and the full MainWindow/backend signal route remain open; T05 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T08 current archive safety refresh — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 90s /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/untar_test/untar_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The isolated libarchive fixture exercised normal Unicode names, missing/truncated archives, traversal and absolute members, duplicate entries, symlink/hardlink targets, FIFO rejection and the 64 MiB member-size boundary; only the expected missing-archive warning was emitted.
- Scope: T08 archive path/type/duplicate/size and error-propagation coverage is now reflected as complete on the board. Transactional database publication, streamed download limits and full UI cancellation remain delegated to T08/T16 acceptance work; T08 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T08 transactional package-manager refresh — 2026-09-15

- Date: 2026-09-15
- Verification command: `set -o pipefail; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120s /home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager/aeq_package_manager_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The fixture served a complete local package over a loopback HTTP server, verified publication and installed validity, then served an incomplete candidate and verified rejection plus byte-identical preservation of the prior database.
- Scope: fresh staging, metadata/content validation, successful publication and rollback preservation are evidenced. Corrupt-JSON/filesystem variants, exact result-signal counting, streamed download boundaries and full UI cancellation remain open; T08 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed; the loopback package was disposable.
- Commit/PR: not committed.

### Luna implementation session — T10 crossfeed reader/replacement refresh — 2026-09-15

- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, `libjamesdsp/tests/crossfeed_lifecycle_test.c` and this roadmap.
- Implementation: normal and benchmark processing now read `crossfeedEnabled` while holding the same mutex used by enable/disable and convolver replacement. The lifecycle fixture now runs a processing thread concurrently with a toggler that repeatedly enables, changes modes and disables crossfeed, while the main thread forces refreshes.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed-current; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' 'DEFINES += JDSP_TEST_HOOKS' -o "$root/libjamesdsp/Makefile"; make -C "$root/libjamesdsp" -B -j2; qmake6 libjamesdsp/tests/crossfeed_lifecycle_test.pro 'CONFIG += DEBUG_ASAN' LIBS+="-L$root/libjamesdsp" -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/crossfeed_lifecycle_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the test printed `crossfeed lifecycle test passed` with no ASan/UBSan diagnostics.
- Scope: the controlled ASan/UBSan reader/replacement test is green. A separate TSan run is still unavailable on this host (`FATAL: ThreadSanitizer: unexpected memory mapping` in the prior attempt), and native assembly/JIT instrumentation plus shutdown stress remain open; T10 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T09/T10/T11 regression checkpoint — 2026-09-15

- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed-current; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/crossfeed_lifecycle_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local/libjamesdsp/tests/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t20-src-tests/codecontainer_save_test/codecontainer_save_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Crossfeed lifecycle and sample-rate transition tests printed their pass messages under ASan/UBSan; atomic-save preservation passed and emitted only its expected missing-destination diagnostic; both whitespace checks passed.
- Scope: T09 remains IN_PROGRESS pending live backend/device and shutdown/TSan evidence. T10’s controlled reader/replacement criteria are now checked, while the separate TSan/shutdown stress criterion remains open. T11’s focused atomic-save regression remains green, with editor/UI failure-signal and commit-failure-seam coverage still open.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T12 precision and parser safety refresh — 2026-09-15

- Files changed: `src/data/EelParser.cpp`, `src/interface/LiveprogSelectionWidget.cpp`, `src/interface/event/ScrollFilter.h`, `src/tests/eel_parser_test.cpp`, `src/tests/liveprog_editor_bridge_test.cpp` and this roadmap.
- Implementation: decimal precision now derives from a bounded decimal grid instead of binary-float text artifacts; comment masking uses non-whitespace sentinels so commented assignments cannot be selected; dynamically installed scroll filters are parent-owned and no longer leak across property-panel rebuilds.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t12-current; make -C "$root/bridge" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/bridge/liveprog_editor_bridge_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/parser/eel_parser_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The offscreen bridge and parser tests passed under ASan/UBSan; canonical values such as `0.123`, `0.1001` and `0.101` were preserved, and no leak report remained. Expected unsupported-control warnings were emitted.
- Scope: T12 parser/UI precision and source-selection coverage is green. Full production VM/disk round-trip and MainWindow integration remain open; T12 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T08 package-manager failure matrix — 2026-09-15

- Files changed: `src/tests/aeq_package_manager_test.cpp` and this roadmap.
- Coverage: the package-manager fixture now rejects corrupt `version.json` exactly once, preserves the previously installed payload, rejects a blocked parent-directory filesystem failure exactly once while preserving its sentinel bytes, and retains the existing successful replacement/incomplete-candidate rollback assertions. The downloader fixture separately covers streamed partial/write failure behavior.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t08-package-current; qmake6 src/tests/aeq_package_manager_test.pro 'CONFIG += DEBUG_ASAN' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/aeq_package_manager_test"; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-gzip-downloader-current/gzip_downloader_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Both focused Qt targets passed under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: T08’s listed package-manager failure/replacement and downloader partial-write evidence is now green; full installed-package UI cancellation and production repository integration remain open. T08 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T11 deterministic commit-failure seam — 2026-09-15

- Files changed: `src/subprojects/EELEditor/src/model/codecontainer.h`, `src/tests/codecontainer_save_test.cpp`, `src/tests/codecontainer_save_test.pro` and this roadmap.
- Implementation: added a test-only `EELEDITOR_TEST_HOOKS` commit-failure seam around the `QSaveFile` commit boundary. The focused fixture now proves a failed commit returns false and leaves the prior Unicode-named file bytes intact; the seam never affects production builds.
- Red verification: `set +e; root=/home/soloarch/Workspace/build/jamesdsp-luna-t11-commit-failure-red; qmake6 src/tests/codecontainer_save_test.pro -o "$root/Makefile"; make -C "$root" -B -j2` -> nonzero with the intentional undefined `CodeContainerSetSaveCommitFailureForTests` test reference.
- Green verification: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t11-commit-failure-green; qmake6 src/tests/codecontainer_save_test.pro 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/codecontainer_save_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Normal, missing-destination and injected commit-failure paths passed under ASan/UBSan; only expected save diagnostics were emitted.
- Scope: T11 open/write/commit preservation and QSaveFile behavior are now checked. Full editor dirty-state/status and execution-signal permutations remain open; T11 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T06 config load/IPC refresh — 2026-09-16

- Files changed: `src/tests/dsp_config_validation_test.cpp` and this roadmap.
- Implementation/test coverage: the validation fixture now isolates `XDG_CONFIG_HOME` in a temporary directory, exercises `DspConfig::load(QString)` with valid IPC-style text, observes exactly one `configBuffered` and `updated` signal, checks persisted `audio.conf`, then feeds malformed/empty content and verifies default recovery without sanitizer findings.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t06-load-current; qmake6 src/tests/dsp_config_validation_test.pro 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -B -j2; XDG_CONFIG_HOME="$root/config-home" ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/dsp_config_validation_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The load/IPC/default-recovery fixture passed under ASan/UBSan; expected debug fallback diagnostics were emitted.
- Scope: direct configuration load/IPC behavior is now covered. Full MainWindow/offscreen widget loading remains open because the reviewed GUI subprojects are unavailable in this isolated checkout; T06 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T04 disabled-state regression refresh — 2026-09-16

- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap.
- Implementation/test coverage: the native lifecycle fixture now explicitly disables Liveprog, processes a block through `JamesDSPProcess`, asserts the disabled path leaves both channels unchanged, then re-enables before continuing legacy and modern parameter tests. Existing coverage in the same fixture continues to exercise the legacy derived-gain failure/reload policy and one-shot `@slider` lifecycle behavior.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t04-disable-current; qmake6 libjamesdsp/tests/tests.pro 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' LIBS+="-L/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local/libjamesdsp" -o "$root/Makefile"; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/liveprog_runtime_test" resources/assets/liveprog/gainControl.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `liveprog runtime test passed` with no ASan/UBSan diagnostics.
- Scope: native legacy/lifecycle/disabled behavior is green. UI-selected/persisted reset/list and full host integration remain open; T04 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T04 offscreen UI parameter contract — 2026-09-16

- Files changed: `src/tests/liveprog_editor_bridge_test.cpp` and this roadmap.
- Coverage: the offscreen bridge now drives a fractional slider and enum combo through their real signal handlers, checks the emitted canonical value, verifies persisted source (`0.500` and `mode = 1`), invokes reset-to-default and verifies both values return to source defaults, and checks disabled/re-enabled widget state. Combined with the native gain/lifecycle fixture, this covers UI-selected, disk, and audible behavior for the demonstrated controls.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t04-ui-current; qmake6 src/tests/liveprog_editor_bridge_test.pro 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/liveprog_editor_bridge_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 with no ASan/UBSan diagnostics.
- Scope: T04’s focused native and offscreen UI parameter contract is green. Full MainWindow/backend integration remains open; T04 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T13 callback allocation probe — 2026-09-16

- Files changed: `libjamesdsp/tests/liveprog_callback_probe_test.c`, `libjamesdsp/tests/liveprog_callback_probe_test.pro` and this roadmap.
- Implementation/test coverage: added a linker-wrapped `malloc`/`calloc`/`realloc`/`free` probe. The test measures the real `JamesDSPProcess` callback path separately from `JamesDSPReallocateBlock` quantum preparation, asserting zero callback allocations/frees while confirming preparation allocates outside that probe boundary. The existing reload-lock fixture remains the slow-`@init`, serialized-compiler, stale-rate and active-program continuity evidence.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-callback-probe; qmake6 libjamesdsp/tests/liveprog_callback_probe_test.pro 'LIBS += -L/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local/libjamesdsp' -o "$root/Makefile"; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/liveprog_callback_probe_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `callback allocations=0 frees=0; quantum-preparation allocations=1` and `liveprog callback allocation probe passed`.
- Scope: direct callback heap instrumentation is now green. Full call-graph/file-read/old-state reclamation accounting, block-boundary publication, shutdown stress and hosted TSan evidence remain open; T13 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T14 full control/rate corpus sweep — 2026-09-16

- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c`, `libjamesdsp/tests/run_eel_corpus.py` and this roadmap.
- Implementation/test coverage: the maintained corpus runner now passes every declared control's default/min/max values to the native fixture. The fixture exercises silence, left/right impulses, sine, step and seeded noise, bounded finite output at block sizes 1/2/7/32/77, and reloads at 44.1/48 kHz for every expected-valid script. External maintained Airwindows scripts are included as behavioral checks; no pinned numerical reference is claimed.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t14-control-sweep; mkdir -p "$root"; qmake6 libjamesdsp/tests/tests.pro 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' LIBS+=-L/home/soloarch/Workspace/build/jamesdsp-luna-t17-sanitizer-local/libjamesdsp -o "$root/Makefile"; make -C "$root" -B -j2; python3 libjamesdsp/tests/inventory_eel_corpus.py libjamesdsp/tests/eel_corpus_manifest.json --output "$root/eel_corpus_inventory.json"; python3 -u libjamesdsp/tests/run_eel_corpus.py --inventory "$root/eel_corpus_inventory.json" libjamesdsp/tests/eel_corpus_manifest.json "$root/liveprog_runtime_test"` -> exited 0; inventory reported 50 files, 103 controls, 48 expected passes and 2 expected rejects, and the native runner reported `native EEL corpus expectations passed: 50 files` with no ASan/UBSan diagnostics.
- Scope: the T14 control/stimulus/block/rate/reload criterion is now checked. Pinned upstream Airwindows numerical parity remains open, so T14 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T15 explicit editor synchronization and workflow fixture — 2026-09-16

- Files changed: `src/subprojects/EELEditor/src/widgets/codeeditor.h`, `src/subprojects/EELEditor/src/widgets/codeeditor.cpp`, `src/subprojects/EELEditor/src/eeleditor.cpp`, `src/subprojects/EELEditor/src/widgets/projectview.cpp`, `src/tests/eeleditor_workflow_test.cpp`, `src/tests/eeleditor_workflow_test.pro` and this roadmap.
- Implementation: added an explicit current-container synchronization method and invoke it before Save, Save As, Run and a tab transition. Project-view destruction now blocks item-change signals so parent teardown cannot call an already-destroyed `EELEditor` receiver.
- Red evidence: the first workflow fixture run failed because it incorrectly expected a tab switch to write an unsaved edit to disk; the corrected contract checks that the edit is restored in memory when returning to the tab.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t15-eeleditor-sync; qmake6 src/subprojects/EELEditor/src/src.pro 'CONFIG += DEBUG_ASAN' 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -B -j2; root=/home/soloarch/Workspace/build/jamesdsp-luna-t15-workflow; qmake6 src/tests/eeleditor_workflow_test.pro -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=0 timeout 120 "$root/eeleditor_workflow_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> workflow assertions passed and exit 0, but UBSan reported two remaining vendor docking teardown downcasts (`CDockContainerWidget` to `CDockManager`) with non-halting mode. The strict `UBSAN_OPTIONS=halt_on_error=1` run exited 1 at that vendor shutdown path.
- Scope: explicit Run/current-text and tab-switch preservation are now directly exercised; clean sanitizer shutdown, Save As dialog/cancellation, failed-save, invalid-script, unsaved-new-document, reopen and duplicate-signal permutations remain open. T15 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T16 cancellation regression refresh — 2026-09-16

- Files changed: this roadmap only in this slice; the existing AutoEQ cancellation fixture remains unchanged.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-gzip-current; mkdir -p "$root"; qmake6 src/tests/gzip_downloader_test.pro 'CONFIG += DEBUG_ASAN' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 under ASan/UBSan. The fixture covered bounded download/write failures, repeated abort/start, normal chunked extraction, extraction interruption, parent destruction, dialog close/reject/Escape and no false success; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: T16’s existing cancellation/lifetime criteria remain green. Explicit validation-phase cancellation and a clean strict-sanitizer editor shutdown are separate open work; T16 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T20 change and identity audit — 2026-09-16

- Files changed: this roadmap log; audit artifact `/home/soloarch/Workspace/build/jamesdsp-luna-t20-change-audit/change-audit.txt`.
- Verification command: `set -euo pipefail; build=/home/soloarch/Workspace/build/jamesdsp-luna-t20-change-audit; ... git status --short --branch; git diff --name-only; git ls-files --others --exclude-standard; git diff --stat; git submodule status --recursive; sha256sum [representative native/UI/AutoEQ/editor/packaging/workflow files]; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The audit recorded 54 tracked changed paths, all current untracked deliverables, recursive submodule identities, and hashes for representative task-critical files.
- Findings: the isolated branch contains the reviewed dirty source plus roadmap implementation files; no commit, publication or unrelated cleanup was performed. Generated Python `__pycache__` entries are visible in the audit and remain build/workspace residue, not product scope.
- Scope: source/diff ownership and full all-change attribution still require a final manual mapping against every roadmap log entry; package permissions and installed-runtime identity remain unverified. T20 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T17/T18/T19/T20 contract refresh — 2026-09-16

- Files changed: this roadmap log; generated comparison JSON `/home/soloarch/Workspace/build/jamesdsp-luna-t20-current-comparison.json`.
- Verification command: `python3 meta/tests/workflow_contract_test.py; python3 -m py_compile meta/tests/workflow_contract_test.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; workflow contract passed.
- Verification command: `BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging-current meta/tests/packaging_contract_test.sh; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh` -> exited 0; packaging contract and shell syntax passed.
- Verification command: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; out=/home/soloarch/Workspace/build/jamesdsp-luna-t20-current-comparison.json; timeout 300 python3 -u libjamesdsp/tests/compare_eel_manifest.py libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test" "$out"; python3 -c "import json; d=json.load(open('$out')); assert d['count'] == 50 and d['native_expected_pass']; print('records=', d['count'], 'native_expected_pass=', d['native_expected_pass']); print('differences=', [(x['path'], x['python_valid'], x['native_pass']) for x in d['files'] if x['python_valid'] != x['native_pass']])"` -> exited 0; 50 records, all native expected outcomes passed, and exactly two intentional Python-valid/native-rejected fixtures (`hpfloat.eel`, editor `demo.eel`). The first auxiliary summary expression had a syntax typo after the comparator had passed; corrected summary rerun passed.
- Scope: local workflow, packaging and native/Python corpus contracts remain green. Hosted Actions, real bundle/runtime validation, full editor shutdown sanitizer cleanliness and remaining UI/backend acceptance are still open; T17–T20 remain `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T03 favorites-directory self-bookmark — 2026-09-16

- Files changed: `src/tests/file_selection_widget_test.cpp` and this roadmap.
- Coverage: the offscreen `FileSelectionWidget` fixture now selects a file already inside its configured bookmark directory and triggers the real Bookmark button. It asserts exactly one success signal and byte-identical source contents, protecting the original favorites-directory self-copy data-loss reproduction.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t03-favorites-current; mkdir -p "$root"; qmake6 src/tests/file_selection_widget_test.pro 'CONFIG += DEBUG_ASAN' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/file_selection_widget_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 with no ASan/UBSan diagnostics.
- Scope: the concrete favorites-directory UI regression is now tested. Full MainWindow-linked preset/import/export IPC behavior remains open because optional GUI subprojects are absent; T03 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T15 strict editor workflow and ownership regressions — 2026-09-16

- Files changed: editor submodule `src/eeleditor.cpp`, `src/model/eelcompleter.cpp`, `src/widgets/codeeditor.{h,cpp}`, `src/widgets/projectview.{h,cpp}`, `3rdparty/docking-system/src/{DockContainerWidget.h,DockContainerWidget.cpp,DockManager.cpp}`; parent `src/tests/eeleditor_workflow_test.cpp` and this roadmap. Existing editor-submodule modifications were preserved and extended in place.
- Implementation: actual Save As now adopts the file path in both `CodeContainer` and its project row; close-current then resolves the new path. Editor-owned completer/highlighter/symbol-provider/style lifetimes are explicit, the completer model is parented to its owner, ProjectView teardown blocks unsafe parent callbacks, and the docking manager clears its self-reference before the inherited container destructor.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t15-eeleditor-sync; make -C "$root" -j2; root=/home/soloarch/Workspace/build/jamesdsp-luna-t15-workflow; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/eeleditor_workflow_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The editor subproject built with ASan/UBSan; offscreen Run, actual Save As, original-byte preservation, subsequent Save, fast tab switching, adopted-row close, editor teardown, ASan leak detection and UBSan all passed. Only Qt’s expected offscreen `propagateSizeHints()` diagnostic appeared.
- Red evidence: initial strict runs exposed the ProjectView callback during parent teardown, then docking-manager self-reference downcasts and editor-owned completer/style leaks. The current strict rerun is clean after the targeted ownership/lifetime fixes.
- Scope: Save As adoption and current-text/tab continuity criteria are checked. Compiler result/status rendering, failed-save UI/cancellation and wider reopened-document permutations remain open, so T15 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — critical native sanitizer refresh — 2026-09-16

- Files changed: this roadmap only.
- Verification command: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t01-red/libjamesdsp/tests/asrc_capacity_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t13-rate/rate_transition_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t10-crossfeed/crossfeed_lifecycle_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t13-capacity-failure/test/process_allocation_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. ASRC capacity, rate transition, concurrent crossfeed lifecycle and steady-state allocation regressions all passed with ASan/UBSan; both whitespace checks passed.
- Scope: this refresh confirms the critical T01/T09/T10/T13 native regressions remain green after current UI/editor changes. T01 remains `VERIFIED`; T09/T10/T13 retain their documented backend/TSan/shutdown gaps.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — first-milestone focused regression refresh — 2026-09-16

- Files changed: this roadmap only.
- Verification command: `set -euo pipefail; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t03-manager/preset_manager_test; XDG_CONFIG_HOME=/home/soloarch/Workspace/build/jamesdsp-luna-t06-load-current/config-home ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t06-load-current/dsp_config_validation_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t12-current/parser/eel_parser_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t11-commit-failure-green/codecontainer_save_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t15-workflow/eeleditor_workflow_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Preset manager, config load/default recovery, parser, atomic save failure preservation and the strict editor workflow all passed; diagnostics were expected missing/default/save and offscreen UI messages.
- Scope: current focused tests across T03/T06/T11/T12/T15 remain green. Full application-linked UI/backend integration and the remaining task-card permutations stay open.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T03 offscreen rename action matrix — 2026-09-16

- Files changed: `src/tests/file_selection_widget_test.cpp` and this roadmap.
- Coverage: the offscreen fixture now invokes the actual Rename button and services its modal `QInputDialog`, verifying successful rename bytes, collision preservation of both files, and traversal rejection without creating an outside file. Existing cases in the fixture still cover favorites self-bookmark preservation/signals, injected bookmark-copy failure and deletion failure.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t03-favorites-current; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/file_selection_widget_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 under ASan/UBSan; only expected offscreen `propagateSizeHints()` diagnostics appeared.
- Scope: widget-level favorites/rename/collision/traversal and failure-preservation coverage is stronger. Full application-linked preset/import/export IPC behavior remains unavailable with the missing optional GUI submodule, so T03 remains `IN_PROGRESS`.

- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T20 fresh headless build/corpus/identity — 2026-09-16

- Files changed: this roadmap; identity artifact `/home/soloarch/Workspace/build/jamesdsp-luna-t20-current-identity/build-identity-current.txt`.
- Build command: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; make -C "$root" -j2` -> exited 0 and rebuilt the current headless application, native library and Liveprog runtime test.
- Regression command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; python3 -u libjamesdsp/tests/run_eel_corpus.py --inventory /home/soloarch/Workspace/build/jamesdsp-luna-t14-control-sweep/eel_corpus_inventory.json libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test"; python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t18-packaging-final meta/tests/packaging_contract_test.sh; bash -n meta/build_deb_package.sh meta/flatpak/build-local-bundle.sh meta/tests/packaging_contract_test.sh; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; full 50-entry corpus passed expected outcomes (48 valid and 2 expected rejects), workflow/packaging contracts and shell syntax passed.
- Identity: current headless executable SHA256 `119a0548546fe47e399e438011b8ba7453586ae0b1c27ecf5ef65876a136e0c4`; runtime-test SHA256 `f94ff057d68c8861d9a389524b3d68854816e6efd579f61afc72b053afcee490`. Source revision and recursive submodule SHAs plus representative source hashes are in the artifact. `flatpak-builder` is unavailable; `flatpak` is installed but no bundle was built or installed.
- Scope: fresh current-source headless build, complete native corpus, local CI/packaging contracts and build identity are verified. Hosted CI, real Flatpak candidate/permissions, MainWindow GUI integration and installed-device runtime remain open; T20 remains `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T16 cooperative validation cancellation — 2026-09-16

- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageValidation.{h,cpp}`, `AeqPackageManager.cpp`, `src/tests/aeq_package_validation_test.cpp`, and this roadmap.
- Regression-first evidence: after adding the deterministic callback test, `make -C /home/soloarch/Workspace/build/jamesdsp-luna-t16-validation-cancel -B -j2` failed to compile as expected because `validPackage` had no cancellation argument. Added an optional cooperative cancellation callback checked before each indexed measurement; package installation now supplies the current Qt thread interruption state and still publishes only after full successful validation.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-validation-cancel; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/aeq_package_validation_test"; root=/home/soloarch/Workspace/build/jamesdsp-luna-t08-aeq-manager; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/aeq_package_manager_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t16-validation-cancel/aeq_package_validation_test; git diff --check` -> exited 0. Validation and package-manager integration targets rebuilt; cancellation, archive/package success and rejection/preservation regressions passed under ASan/UBSan. Qt emitted only expected offscreen `propagateSizeHints()` notices.
- Scope: deterministic cancellation inside package-validation iteration is now unit-tested, and the installer honors a Qt thread interruption request. This does not prove GUI cancellation during validation: the current dialog still performs synchronous validation after download, so phase-level UI cancellation, repeated start/abort and shutdown acceptance remain open. T16 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 rate-state and sanitizer checkpoint — 2026-09-16

- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/dynamic.c`, plus the T09 native implementation/tests and sanitizer-related native files noted in the in-progress worktree; this roadmap.
- Implementation: effective-rate refresh now reruns the active EEL program’s `@init`/`@slider` and reapplies host-set slider values. The transition fixture covers 44.1/48/96 kHz device/internal-rate behavior, forced and non-forced refresh, invalid rates, retained overrides and resulting output. Forced effects-on refresh now also avoids compressor gain interpolation when its FFT grid is not initialized.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; make -C "$root/libjamesdsp" -j2; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += sanitize_undefined' -o "$root/libjamesdsp/tests/rate_transition_test.Makefile"; make -C "$root/libjamesdsp/tests" -f rate_transition_test.Makefile -B -j2; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=alignment=0:halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/rate_transition_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=alignment=0:halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test"; git diff --check` -> build/relink completed, but the rate test did not produce a passing result: UBSan reported misaligned pointer and insufficient-object-size accesses in the packed EEL bytecode interpreter (`Effects/eel2/glue_port.h`, including lines 472, 456, 719 and 649); the command chain therefore did not establish either suite as green.
- Follow-up command: `set +e; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=0 timeout 120 "$root/libjamesdsp/tests/rate_transition_test" > "$root/rate-ubsan-output.txt" 2>&1; status=$?; printf 'rate_transition_status=%s\\n' "$status"; tail -n 35 "$root/rate-ubsan-output.txt"` -> exit status 0 and printed `rate transition test passed`, but emitted UBSan diagnostics from EEL packed-bytecode accesses. This is not a clean UBSan result. `UBSAN_OPTIONS=alignment=0` does not suppress a check already compiled into the binary. The strict run also exposed and prompted the targeted unconfigured-compressor guard; the non-halting run no longer reported the earlier `dynamic.c:1670` zero denominator.
- Scope: the new rate-state regression executes to completion when UBSan continues after diagnostics, but a sanitizer-clean EEL interpreter execution, the required bounded CLI refresh regression, backend integration, filter/delay timing assertions and lifecycle/shutdown coverage remain open. T09 remains `IN_PROGRESS`; no acceptance box is marked complete on this evidence.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 strict sanitizer green checkpoint — 2026-09-16

- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/{dynamic.c,eel2/glue_port.h,eel2/nseel-compiler.c,eel2/stb_sprintf.h}`, `libjamesdsp/tests/liveprog_runtime_test.c`, and this roadmap.
- Regression/debug evidence: strict sanitizer runs first identified (1) unaligned bytecode operand and callback-function-pointer loads/stores plus a before-array floating stack pointer in `glue_port.h`, (2) unaligned 16/32-bit `stb_sprintf` chunk accesses, and (3) an intentional EEL `0/0` and `1/0` fixture triggering the optional float-divide-by-zero sanitizer during constant folding. Bytecode scalar/function-pointer accesses and string chunk accesses now use `memcpy`; the floating stack uses an integer top index; constant folding explicitly returns IEEE NaN/signed infinity for zero denominators. The runtime fixture now asserts that rate-change `@init` resets program-owned counters while host-set sliders survive.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; make -C "$root/libjamesdsp" -j2; make -C "$root/libjamesdsp/tests" -B -j2; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += sanitize_undefined' -o "$root/libjamesdsp/tests/rate_transition_test.Makefile"; make -C "$root/libjamesdsp/tests" -f rate_transition_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/rate_transition_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. `liveprog runtime test passed` and `rate transition test passed`; neither emitted ASan/UBSan diagnostics, and both whitespace checks passed.
- Intermediate regression result: after rate-state reinitialization the runtime test’s prior cumulative `blocks == 26` expectation was stale; observed state was `blocks=1` before the rejected-reload follow-up process call, so its final expected value is 2. The test now documents and asserts that policy.
- Scope: focused strict native sanitizer acceptance is green. T09 remains `IN_PROGRESS` because bounded native CLI regression promotion, PipeWire/PulseAudio backend integration, filter/delay timing assertions and shutdown/concurrent lifecycle acceptance are not complete. No T09 acceptance box is marked complete solely from this checkpoint.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 strict sanitizer rerun and backend contracts — 2026-09-16

- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; make -C "$root/libjamesdsp" -j2; make -C "$root/libjamesdsp/tests" -B -j2; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += sanitize_undefined' -o "$root/libjamesdsp/tests/rate_transition_test.Makefile"; make -C "$root/libjamesdsp/tests" -f rate_transition_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/rate_transition_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; runtime and rate tests both printed their pass messages with no sanitizer diagnostics; whitespace checks passed.
- Backend contract command: `bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh` -> both printed `PipeWire RT callback contract passed` and `PulseAudio wrapper contract passed`.
- Native-suite extension attempt: building `asrc_capacity_test` in the T09 build directory failed at link because that library variant was not built with `JDSP_TEST_HOOKS` (`undefined reference to JamesDSPSetBufferAllocationFailureForTests`). This is a build-configuration mismatch, not a test assertion; the already documented T01 sanitizer test runs remain the current T01 evidence. Rebuild T01 with its test-hook define before claiming a fresh integrated T01 run.
- Scope: T09’s core strict sanitizer regressions and static backend callback contracts are now green. These static scripts do not establish live PipeWire/PulseAudio device transitions; filter/delay timing, bounded CLI-specific invocation and lifecycle/shutdown acceptance remain open. T09 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 bounded refresh CLI regression — 2026-09-16

- Files changed: new `libjamesdsp/tests/native_rate_refresh_cli_test.{c,pro}` and this roadmap.
- Coverage: standalone native test initializes 48 kHz, loads an EEL program whose init/slider coefficients depend on `srate`, performs the original forced 44.1 kHz refresh path, and asserts device rate, internal `fs`, EEL `srate`, init-derived rate and processed sample. The harness is bounded by shell `timeout` so a refresh deadlock is a failing timeout rather than a swallowed result.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; qmake6 libjamesdsp/tests/native_rate_refresh_cli_test.pro -o "$root/libjamesdsp/tests/native_rate_refresh_cli_test.Makefile"; make -C "$root/libjamesdsp/tests" -f native_rate_refresh_cli_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 15 "$root/libjamesdsp/tests/native_rate_refresh_cli_test"; git diff --check` -> exited 0 and printed `bounded native rate refresh passed` without sanitizer diagnostics.
- Scope: the hang and stale-rate reproductions are now represented by bounded/focused native tests; checklist items 1 and 2 are checked. Full atomicity across effect refresh, filter/delay timing and live backend/device transitions remain open; T09 remains `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 internal-rate delay timing follow-up — 2026-09-16

- Files changed: `libjamesdsp/tests/native_rate_refresh_cli_test.c` and this roadmap.
- Coverage: extended the bounded native rate test with an EEL ring delay whose duration is one millisecond. A 44.1 kHz transition produces the impulse at sample 44; a 96 kHz device transition (internally processed at 48 kHz) reinitializes the delay to 48 samples and produces the impulse at sample 48.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; make -C "$root/libjamesdsp/tests" -f native_rate_refresh_cli_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 15 "$root/libjamesdsp/tests/native_rate_refresh_cli_test"; git diff --check` -> exited 0; printed `bounded native rate refresh passed` without ASan/UBSan diagnostics; whitespace check passed.
- Scope: effective internal-rate delay timing is now asserted for 44.1 and 96 kHz device configurations. Filter coefficient/timing equivalence and live backend/device transitions remain open; T09 remains `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 rate-derived filter timing follow-up — 2026-09-16

- Files changed: `libjamesdsp/tests/native_rate_refresh_cli_test.c` and this roadmap.
- Coverage: the bounded EEL fixture now also calculates a one-pole filter coefficient from `srate`, checks the exact coefficient and all 64 impulse-response samples after 44.1 kHz and 96 kHz device transitions, and verifies the latter uses internal 48 kHz timing.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; make -C "$root/libjamesdsp/tests" -f native_rate_refresh_cli_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 15 "$root/libjamesdsp/tests/native_rate_refresh_cli_test"; git diff --check` -> exited 0; `bounded native rate refresh passed`, no sanitizer diagnostics, whitespace check passed.
- Scope: T09 checklist items 1, 2 and 4 are now verified. Backend/device integration, comprehensive effect-on/off state consistency and lifecycle/shutdown coverage remain open; T09 stays `IN_PROGRESS`.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — strict EEL sanitizer corpus and rate gate — 2026-09-16

- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/dynamic.c`, `eel2/{glue_port.h,nseel-compiler.c,stb_sprintf.h}`, `libjamesdsp/tests/liveprog_runtime_test.c`, `native_rate_refresh_cli_test.c`, and this roadmap.
- Regression findings/fixes: strict corpus testing found a three-band crossover state misaligned after a single-float metadata slot; EEL RAM state now stores a computed aligned offset used consistently by initialize/process/clear. PFB state offsets are advanced to satisfy `WarpedPFB` alignment and written back to their EEL variables. Runtime callback/function-pointer/scalar bytecode accesses and stb_sprintf chunk copies use alignment-safe `memcpy`, and the EEL floating stack no longer forms a pointer before its array. Intentional zero division retains IEEE NaN/signed-infinity behavior through a shared helper for constant folding and runtime division opcodes; an uninitialized compressor refresh no longer interpolates against an absent FFT grid.
- Full corpus command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 -u libjamesdsp/tests/run_eel_corpus.py --inventory /home/soloarch/Workspace/build/jamesdsp-luna-t14-control-sweep/eel_corpus_inventory.json libjamesdsp/tests/eel_corpus_manifest.json "$root/libjamesdsp/tests/liveprog_runtime_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. All 50 manifest entries matched expected outcomes (48 valid, 2 expected rejects); no ASan/UBSan diagnostic occurred; both whitespace checks passed.
- Current native verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-no-callback-refresh; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += sanitize_undefined' -o "$root/libjamesdsp/tests/rate_transition_test.Makefile"; make -C "$root/libjamesdsp/tests" -f rate_transition_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/libjamesdsp/tests/rate_transition_test"; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 15 "$root/libjamesdsp/tests/native_rate_refresh_cli_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; outputs were `liveprog runtime test passed`, `rate transition test passed`, and `bounded native rate refresh passed`, with no sanitizer diagnostics; both whitespace checks passed.
- Scope: strict native rate/runtime/CLI, PFB/splitter alignment and complete reviewed EEL corpus are green. T09 remains `IN_PROGRESS`: actual PipeWire/PulseAudio device transition integration, full effect-state atomicity and shutdown/TSan lifecycle coverage remain open. T14 remains `IN_PROGRESS` pending its pinned Airwindows numerical-reference comparison; corpus behavior alone is not claimed as parity.
- No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Final handoff template

```text
Completed and verified: [task IDs]
Still open/blocked/obsolete with evidence: [task IDs]
Exact changed files: [grouped by repository]
Tests and build commands/results: [include failures and unavailable variants]
Package/runtime identity and validation status:
Unresolved P1/P2 risks:
Work log last entry and next safe action:
```

### Luna implementation session — T03 current regression refresh — 2026-09-16

- Task-board update: checked T03’s historical data-loss reproduction row. F03 contains the reproduced baseline failure; maintained temp-directory helper and offscreen widget tests assert that self-copy and bookmarking a file already in the favorites directory preserve the exact original bytes. The original destructive behavior is not reintroduced to re-prove it.
- Verification command: `set -euo pipefail; for name in preset_file_operations file_selection_widget preset_manager; do build="/home/soloarch/Workspace/build/jamesdsp-luna-t03-${name}-current"; mkdir -p "$build"; qmake6 "src/tests/${name}_test.pro" -o "$build/Makefile"; make -C "$build" -B -j2; if [ "$name" = file_selection_widget ]; then QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/${name}_test"; else ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$build/${name}_test"; fi; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. All three current targets rebuilt and passed under ASan/UBSan; offscreen widget emitted only the platform plugin’s `propagateSizeHints` informational notices. Both whitespace checks passed.
- Scope: focused helper/manager/widget acceptance remains green. Full application-linked preset/import/export IPC integration is still unavailable because an optional GUI subproject include is absent; therefore T03 remains `IN_PROGRESS`. No install, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T05 preservation contract and current host regression — 2026-09-16

- Task-board update: checked the T05 contract-conflict row. `docs/liveprog-four-stage-spec.md` explicitly requires failed parse/allocation/compile/initialization to leave the active VM, variables, enabled state and audio processing unchanged, with explicit user disable remaining immediate. The native runtime and host fixtures now assert preservation, later valid recovery and explicit disable; the contradictory legacy disable-on-failure expectation is not retained.
- Verification attempt and diagnosis: rebuilt the ASan/UBSan host fixture before rebuilding its static native dependency, so its first execution used an archive whose object timestamps predated the current `jdsp_header.h`; the stale ABI produced an assertion failure at the valid-load active-state check. I then rebuilt the native target from current isolated-worktree sources and explicitly relinked the fixture against that archive.
- Successful verification command: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp -B -j2; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t05-preserve-current -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t05-preserve-current/dsp_host_reload_test` -> exited 0. The host regression passed through initial missing-file inactivity, valid activation, missing/invalid candidate preservation with unchanged gain output, valid recovery, and explicit disable; expected configuration and rejected-script diagnostics appeared, with no ASan/UBSan findings.
- Scope: T05’s written conflict is resolved and the current native/host contract regression is green. The full MainWindow signal-route/offscreen status integration and other task acceptance gaps remain; T05 stays `IN_PROGRESS`. The initial stale-archive failure is retained here as a build-identity diagnosis, not hidden as a product regression. No install, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T05 current native runtime preservation refresh — 2026-09-16

- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless; qmake6 libjamesdsp/tests/tests.pro 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/libjamesdsp/tests/liveprog_runtime_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0 and printed `liveprog runtime test passed`; no sanitizer diagnostics; whitespace checks passed.
- Build boundary: the runtime test executable/harness was freshly compiled with ASan/UBSan and linked against the freshly rebuilt current-source native archive, whose production objects in this headless target are not sanitizer-instrumented. T05’s separately rebuilt host test did run ASan/UBSan-instrumented host code. Existing fully instrumented native runtime evidence remains in the earlier T05/T09 sanitizer entries; this refresh specifically confirms current-source preservation semantics, not complete-library sanitizer instrumentation.
- Scope: native valid→invalid→processing→valid-recovery and explicit-disable behavior is refreshed. Full MainWindow route integration remains open because `FlatTabWidget`, `LiquidEqualizerWidget` and `GraphicEQWidget` submodule worktrees are empty in this checkout; source inspection confirms the compiler-result handler reads `host()->liveprogActive()`, but source inspection is not claimed as an integration test. T05 remains `IN_PROGRESS`.

### Luna implementation session — T08 current transactional package regression refresh — 2026-09-16

- Verification command: `set -euo pipefail; for name in aeq_package_validation aeq_package_manager; do root="/home/soloarch/Workspace/build/jamesdsp-luna-t08-${name}-current"; mkdir -p "$root"; qmake6 "src/tests/${name}_test.pro" -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/${name}_test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Both targets rebuilt with ASan/UBSan; archive-validation and package-manager transaction/rollback tests passed, with only expected offscreen `propagateSizeHints()` notices; whitespace checks passed.
- Scope: this refresh confirms current-source validation and transactional publication regressions. It does not close installed-package runtime validation or all UI cancellation phases; T08 remains `IN_PROGRESS`. No install, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T09 ordinary-backend effect-rate refresh — 2026-09-16

- Files changed: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, `libjamesdsp/tests/rate_transition_test.c`, and this roadmap.
- Regression red: with bass boost configured/enabled, `JamesDSPSetSampleRate(jdsp, 44100.0f, 0)` left `jdsp->dbb.fs` at 48000; the test failed at `rate_transition_test.c:84` with `Assertion 'fabs(jdsp->dbb.fs - 44100.0) < 0.1' failed` (exit 134). PipeWire and PulseAudio both use `forceRefresh=0` for device rate notifications.
- Implementation: preserve `oldFs` while holding the transition mutex and execute the existing frequency-dependent effect refresh helpers when forced or when effective internal `fs` changes. This updates DBB/other dependent effect state for ordinary 48↔44.1 kHz backend transitions while leaving effects at internal 48 kHz when the device changes to 96 kHz. Existing mixed-lock helper ownership remains unchanged and atomic publication is not claimed.
- Full native sanitizer build command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2` -> exited 0; all current native production objects were compiled with ASan, UBSan and float-divide-by-zero instrumentation.
- Rate regression command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp/tests; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/rate_transition_test.Makefile"; make -C "$root" -f rate_transition_test.Makefile -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/rate_transition_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; printed `rate transition test passed`, including enabled bass-boost processing through ordinary 48↔44.1 kHz transitions, unchanged 48 kHz internal effect state at a 96 kHz device rate, disabled-state transition, invalid-rate retention, concurrent process/rate and crossfeed refresh cases. No sanitizer findings; both whitespace checks passed.
- Additional current-source gates: `qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 240 "$root/liveprog_runtime_test"` -> exited 0 with `liveprog runtime test passed`; bounded CLI refresh rebuilt against the same instrumented archive and passed with `bounded native rate refresh passed`; `bash src/tests/pipewire_rt_contract_test.sh` and `bash src/tests/pulse_wrapper_contract_test.sh` both passed.
- Scope: T09 checklist item 5 is checked for bounded CLI, static backend callback contracts, effects on/off, and invalid rates. Static contracts are not live backend/device integration. The effect refresh helpers still publish piecemeal after releasing the transition lock, so atomic processing-state visibility, live device transitions and shutdown/TSan acceptance remain open; T09 remains `IN_PROGRESS`. No install, publication, commit, Flatpak permission change or running-application restart was performed.

### Luna implementation session — T07 lifecycle acceptance closeout — 2026-09-16

- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c` and this roadmap only. The separately owned `/home/soloarch/Workspace/projects/jamesdsp-liveprog` tree remains unmodified; its untracked baseline was preserved.
- Regression coverage: the diagnostic now asserts that setting `slider1=2` updates derived `gain` without changing `@block`/`@sample` counters; the next one-sample process advances block count, sample count, and recorded block size exactly once and produces gain-2 stereo output. The same run exercises bundled high-pass channel isolation at 44.1/48 kHz.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp/tests; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/liveprog_runtime_test" resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; runtime regression printed `liveprog runtime test passed`; no ASan/UBSan diagnostics; both whitespace checks passed.
- Board update: T07 is `VERIFIED`; its channel-isolation, 44.1/48 kHz response and diagnostic lifecycle/control acceptance criteria now have direct maintained regression coverage. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T09 ThreadSanitizer race evidence — 2026-09-16

- Files changed: `libjamesdsp/tests/rate_transition_test.pro` and this roadmap. The new `DEBUG_TSAN` mode selects ThreadSanitizer without combining incompatible ASan/UBSan instrumentation.
- Build command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-tsan/libjamesdsp; mkdir -p "$root"; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/Makefile"; make -C "$root" -B -j2; qmake6 libjamesdsp/tests/rate_transition_test.pro 'CONFIG += DEBUG_TSAN' 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/tests/rate_transition_test.Makefile"; make -C "$root/tests" -f rate_transition_test.Makefile -B -j2` -> exited 0; current native library and rate test were compiled/linked with TSan.
- Runtime evidence: plain `TSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/rate_transition_test"` exited 66 before `main` with `FATAL: ThreadSanitizer: unexpected memory mapping`. With `setarch "$(uname -m)" -R env TSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/rate_transition_test"`, TSan started and reported a lock-order-inversion warning involving CrossfeedEnable/convolver thread initialization (exit 66; retained as a diagnostic, not asserted as a confirmed deadlock). With deadlock detection disabled, `setarch "$(uname -m)" -R env TSAN_OPTIONS=detect_deadlocks=0:halt_on_error=1 timeout 180 "$root/tests/rate_transition_test"` exited 66 on a confirmed data race: `allocate_buffer_layout` writes the `tmpBuffer` pointer layout during `JamesDSPSetSampleRate` while the processing thread reads those pointers in `JamesDSPProcess` without holding the same mutex. Symbolized disassembly identifies the raced field as the buffer pointer at `jdsp + 0x29e070`.
- Blocker/board update: T09 is `BLOCKED`, not VERIFIED. The existing mutex only guards portions of `JamesDSPProcess`, while replacement/reclamation and effect refresh mutate processing-visible state. Widening the mutex around existing helper calls would introduce nested-lock deadlocks and hold the audio callback across allocations/convolver rebuilds; resolving this requires a complete candidate-state publication/reclamation design coordinated with T13. T09’s prior bounded ASan/UBSan tests remain useful but do not override this TSan failure. Next independent safe task: continue T08/T16 transaction and cancellation acceptance; T10/T13/T14 dependency paths remain gated. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T08 current package transaction regression refresh — 2026-09-16

- Verification command: `set -euo pipefail; for name in aeq_package_validation aeq_package_manager; do root="/home/soloarch/Workspace/build/jamesdsp-luna-t08-${name}-current"; mkdir -p "$root"; qmake6 "src/tests/${name}_test.pro" 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/${name}_test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Both targets rebuilt from current sources; archive validation and package staging/replacement/rollback assertions passed under ASan/UBSan, with only expected offscreen `propagateSizeHints()` notices; whitespace checks passed.
- Scope: no installation or publication was performed. T08 remains `IN_PROGRESS` because the complete streamed-download size/write boundary suite and all UI cancellation phases are tracked with T16 and still need final integrated evidence.
- Commit/PR: not committed.

### Luna implementation session — T16 current cancellation/lifetime regression refresh — 2026-09-16

- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-cancellation-current; mkdir -p "$root"; qmake6 src/tests/gzip_downloader_test.pro 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 240 "$root/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the fully rebuilt downloader/extraction/dialog fixture passed chunked and over-limit downloads, write failure, repeated start/abort, slow extraction cancellation, worker completion, parent destruction, dialog close/reject, Abort button and Escape cases. No ASan/UBSan findings; only expected offscreen `propagateSizeHints()` notices; whitespace checks passed.
- Scope: this remains bounded offline fixture evidence. Package validation after download currently runs synchronously in the AutoEQ promise path, so UI cancellation during validation is not demonstrated; completion/race and full application shutdown permutations also remain open. T16 stays `IN_PROGRESS`. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T08 offline acceptance closeout — 2026-09-16

- Fresh current-source verification completed in this checkpoint:
  - `set -euo pipefail; for name in aeq_package_validation aeq_package_manager; do root="/home/soloarch/Workspace/build/jamesdsp-luna-t08-${name}-current"; mkdir -p "$root"; qmake6 "src/tests/${name}_test.pro" 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/${name}_test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; both validator and package staging/rollback tests passed under ASan/UBSan, with only expected offscreen notices.
  - `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t08-untar-current; mkdir -p "$root"; qmake6 src/tests/untar_test.pro -o "$root/Makefile"; make -C "$root" -B -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/untar_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; generated traversal/absolute paths, duplicates, links, truncation, Unicode, archive size limits and cooperative interruption passed under ASan/UBSan; expected malformed/missing archive diagnostics only.
  - T16 current downloader regression command (logged in the immediately preceding T16 entry) exited 0 and freshly rebuilt/passed compressed and streamed download caps, short write, chunked success, cancellation, and extraction/dialog lifetime cases under ASan/UBSan.
- Acceptance decision: T08’s offline scope is verified: unsafe members cannot escape extraction, failure/cancellation cannot replace the active database, and success follows package validation and staging publication. A system installation was neither required by this task card nor performed. UI cancellation phases remain T16 work and are not used to hold T08 open.
- Board update: T08 is `VERIFIED`. T16 remains `IN_PROGRESS` for integrated cancellation phases/shutdown; T09 is separately `BLOCKED` on its TSan-confirmed processing-state race. No publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T04 editor/host acceptance refresh — 2026-09-16

- Verification command: `set -euo pipefail; for spec in src/tests/liveprog_editor_bridge_test.pro src/tests/dsp_host_reload_test.pro; do name=$(basename "$spec" .pro); root="/home/soloarch/Workspace/build/jamesdsp-luna-${name}-t04-current"; mkdir -p "$root"; if [ "$name" = dsp_host_reload_test ]; then qmake6 "$spec" 'CONFIG += DEBUG_ASAN sanitize_undefined' DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp -o "$root/Makefile"; else qmake6 "$spec" 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; fi; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/$name"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The rebuilt editor bridge passed parameter precision/list/reset and reload-signal assertions; the host fixture passed sanitized config handling and last-good-program reload/error paths. Only pre-existing compiler warnings and expected rejected/missing-script diagnostics appeared; no ASan/UBSan findings; whitespace checks passed.
- Scope: this refresh confirms the editor persistence/signal and host behavior fixtures together, but does not bind a real MainWindow widget update through to audible native processing in one integration test. T04 remains `IN_PROGRESS`; T05 remains `IN_PROGRESS` for full UI status integration. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — T16 completion-race and validation-shutdown regressions — 2026-09-16

- Files changed: `src/tests/gzip_downloader_test.cpp` and this roadmap. The fixture adds a deterministic race where interruption becomes visible inside the validator immediately before it returns an otherwise-successful result; the downloader must emit no success and the dialog must reject. A second case destroys the validation dialog’s parent window and verifies the validator observes interruption and the owning dialog/worker are drained.
- Initial verification exposed a sampling race in the pre-existing validation-cancel test: the modal dialog reached Rejected before the test sampled the worker’s atomic cancellation-observed flag. The assertion was retained and the test now waits a bounded 500 ms for worker completion; it was not removed or relaxed.
- Build and one-run command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-cancellation-current; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 240 "$root/gzip_downloader_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> rebuilt all fixture objects with AddressSanitizer and UndefinedBehaviorSanitizer; after the bounded worker-completion wait was added, exited 0 with no sanitizer findings. Qt offscreen emitted only `propagateSizeHints()` notices.
- Repeatability command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-cancellation-current; for run in 1 2 3 4 5; do QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 240 "$root/gzip_downloader_test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> all five consecutive sanitizer runs and both whitespace checks exited 0; only expected offscreen size-hint notices appeared.
- Scope: cancellation during download/extraction/validation, repeated abort, rejection/destruction, exactly-once completion, and cancellation concurrent with successful validation return now have direct fixture evidence. T16 remains `IN_PROGRESS` because full AeqPackageManager/application shutdown and publication lifecycle integration is not exercised by this isolated dialog/worker fixture. No installation, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — first-milestone focused regression refresh — 2026-09-16

- Worktree/build boundary: sources were read from `/home/soloarch/Workspace/upstream/JDSP4Linux-luna`; all Makefiles, objects and executables were placed under `/home/soloarch/Workspace/build/jamesdsp-luna-milestone-*`. The shared checkout and running audio application were not changed or restarted.
- Focused suite command: `set -euo pipefail; for name in preset_file_operations file_selection_widget preset_manager dsp_config_validation eq_preset_validation liveprog_editor_bridge; do spec="src/tests/${name}_test.pro"; target="${name}_test"; [ "$name" = liveprog_editor_bridge ] && target=liveprog_editor_bridge_test; root="/home/soloarch/Workspace/build/jamesdsp-luna-milestone-${name}-current"; mkdir -p "$root"; qmake6 "$spec" 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/$target"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. All six current-source targets rebuilt and passed. The preset file/widget/manager and editor bridge build flags included ASan/UBSan; the generic config flags did not instrument the DspConfig and EQ targets because those `.pro` files lack the corresponding flag setup, so those passes are recorded as functional-only here. Expected Qt offscreen notices and existing compiler warnings only; whitespace checks passed.
- Sanitized config/EQ follow-up command: `set -euo pipefail; for name in dsp_config_validation eq_preset_validation; do root="/home/soloarch/Workspace/build/jamesdsp-luna-milestone-${name}-asan-current"; mkdir -p "$root"; qmake6 "src/tests/${name}_test.pro" 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/${name}_test"; done; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; both rebuilt with compile and link sanitizer flags and passed under ASan/UBSan without findings. The EQ target emitted one existing unused-function warning.
- Board decision: no acceptance checkbox/status was promoted by this refresh. T03/T04/T05/T06 still lack their documented full-application/widget integration evidence; T09 remains BLOCKED by the logged TSan race and dependent work stays gated. Next dependency-safe path is to address remaining T03–T06 integration evidence where available, then resume tasks whose prerequisites are verified. No installation, publication, commit or permission change was performed.
- Commit/PR: not committed.

### Luna implementation session — T16 manager shutdown UAF regression and verification — 2026-09-16

- Files changed: `src/subprojects/AutoEqIntegration/AeqPackageManager.cpp`, `src/tests/aeq_package_manager_test.cpp`, and the task board/log. The real ownership test now parents `AeqPackageManager` to its host window, matching `AeqSelector`; destroying that host during extraction destroys both the manager and dialog while the install promise settles. It asserts host/manager destruction, rejected (not resolved) install, and byte-identical preservation of the previously installed measurement file.
- Regression-first red: command `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-manager-shutdown-red; mkdir -p "$root"; qmake6 src/tests/aeq_package_manager_test.pro 'CONFIG += DEBUG_ASAN sanitize_undefined' -o "$root/Makefile"; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 "$root/aeq_package_manager_test"` -> exited 1 with ASan `heap-use-after-free` in `AeqPackageManager::installPackage()` after host destruction; stack traced the stale `GzipDownloaderDialog*` access following `QDialog::exec()`.
- Implementation: manager now holds the modal dialog in `QPointer<GzipDownloaderDialog>` and only calls `deleteLater()` if it survived `exec()`. The test uses the actual QObject parent ownership tree; no fake dialog or manager was substituted.
- Initial green verification: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-manager-shutdown-red; make -C "$root" -B -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 "$root/aeq_package_manager_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> the dialog-level host-destruction case passed with no sanitizer findings after the guarded-pointer fix. This was subsequently strengthened to destroy the manager with its host.
- Final manager shutdown repeat command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t16-manager-shutdown-red; for run in 1 2 3; do QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 "$root/aeq_package_manager_test"; done; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 240 /home/soloarch/Workspace/build/jamesdsp-luna-t16-cancellation-current/gzip_downloader_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; all three full package-manager sanitizer runs passed with manager+host destruction, promise rejection and prior-database preservation; the complete downloader/dialog phase-matrix sanitizer regression passed; both whitespace checks passed. Only expected offscreen size-hint notices appeared.
- Review: the read-only code review reported no defect in the QPointer/staging teardown fix and initially noted the manager was still alive in the first integration fixture; the fixture was then upgraded to destroy the manager with the host and passed the final repeated sanitizer run.
- Board update: T16 is `VERIFIED`; all four task-card checkboxes are checked. T09 remains `BLOCKED` on the already recorded TSan-confirmed processing-state race; T03–T06 remain `IN_PROGRESS` for their separately recorded full application/UI integration criteria. No system install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — GUI dependency unblock and T03 verification — 2026-09-16

- Worktree dependency action: `git submodule update --init -- src/subprojects/FlatTabWidget src/subprojects/GraphicEQWidget src/subprojects/LiquidEqualizerWidget` -> exited 0. Populated only the pinned GUI submodules in the isolated worktree (FlatTabWidget `06509713d85dc336c4a3b089ef9d265003aaf48e`, GraphicEQWidget `ba63ad32682b20e2d4fde4c8a4aafe4da3423cc5`, LiquidEqualizerWidget `013055c360c66a08325208065211ffba1f5bc192`). The modified EELEditor submodule was not initialized/reset/modified by this action.
- Full application compile/link: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-full-ui; mkdir -p "$root"; qmake6 src/src.pro -o "$root/Makefile"; make -C "$root" -j2` -> exited 0. The application binary was not launched.
- T03 manager verification: `set -euo pipefail; d=/home/soloarch/Workspace/build/jamesdsp-luna-t03-manager-final; mkdir -p "$d"; qmake6 /home/soloarch/Workspace/upstream/JDSP4Linux-luna/src/tests/preset_manager_test.pro -o "$d/Makefile"; make -C "$d" -j2; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$d/preset_manager_test"` -> exited 0 under ASan/UBSan. The test covers manager rename success/collision, traversal rejection, injected rename/remove errors and preservation, successful removal and repeat/malformed removals.
- T03 offscreen widget verification: `set -euo pipefail; d=/home/soloarch/Workspace/build/jamesdsp-luna-t03-widget-final; mkdir -p "$d"; qmake6 /home/soloarch/Workspace/upstream/JDSP4Linux-luna/src/tests/file_selection_widget_test.pro -o "$d/Makefile"; make -C "$d" -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$d/file_selection_widget_test"` -> exited 0 under ASan/UBSan. Assertions cover self-bookmark identity and exactly-once bookmark signaling, byte-preserving bookmark creation, rename success/collision/traversal, and removal signaling; only expected offscreen `propagateSizeHints()` notices appeared.
- Additional focused verification: `QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t04-bridge-final/liveprog_editor_bridge_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /home/soloarch/Workspace/build/jamesdsp-luna-t03-widget-final/file_selection_widget_test; git diff --check` -> command exited 0; editor bridge and widget tests passed and the whitespace check passed. The editor bridge remains a widget/host-boundary fixture, not a single MainWindow-to-audible-native integration test.
- Config/EQ spot checks: the rebuilt `dsp_config_validation_test` and `eq_preset_validation_test` under `/home/soloarch/Workspace/build/jamesdsp-luna-t06-final` both exited 0. In this invocation the `.pro` targets did not carry sanitizer compile flags; the earlier milestone log contains the separate ASan/UBSan-instrumented config/EQ runs. No loadConfig()/IPC/offscreen MainWindow fixture was added, so T06 stays `IN_PROGRESS`.
- Board update: T03 is `VERIFIED` based on maintained helper/manager/widget regressions plus full app compile/link. T04 and T05 remain `IN_PROGRESS` pending combined MainWindow/status-to-host integration; T06 remains `IN_PROGRESS` pending its specified config-load/IPC/offscreen UI fixture. No install, publication, commit, Flatpak permission change or running-application restart was performed.
- Commit/PR: not committed.

### Luna implementation session — first-milestone host and editor regression refresh — 2026-09-16

- Verification command: `set -euo pipefail; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t04-bridge-final/liveprog_editor_bridge_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-t05-preserve-current/dsp_host_reload_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 /home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp/tests/liveprog_runtime_test resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Editor bridge, native host, and native lifecycle/channel regressions passed under ASan/UBSan; expected missing/invalid-script and malformed-config diagnostic messages only; both whitespace checks passed.
- Scope: host tests exercise valid activation, missing/unreadable and malformed candidate preservation, later recovery, explicit disable, malformed/non-finite fixed EQ and compander vectors, and finite subsequent processing. They do not constitute a combined MainWindow signal/status-to-audible-native integration test. T04/T05 therefore remain `IN_PROGRESS`; T06 remains `IN_PROGRESS` until config fixtures traverse the MainWindow load/IPC/offscreen UI route. T03 remains `VERIFIED`; T09 remains `BLOCKED` on its logged TSan processing-state race.
- Commit/PR: not committed.

### Luna implementation session — T06 IPC-to-MainWindow malformed EQ integration — 2026-09-16

- Worker: Codex implementation session in the isolated `luna/2026-09-15-roadmap` worktree.
- Files changed: `src/MainWindow.cpp`, new `src/mainwindow_config_test.pro`, new `src/tests/mainwindow_config_integration_test.cpp`, and this roadmap.
- Implementation: added an offscreen fixture using the real `MainWindow`, `DspConfig`, and `IpcHandler::setAndCommit` path. It verifies a valid 15-band fixed EQ, truncated and NaN fixed gain vectors produce neutral UI state rather than partial updates, and malformed flexible data leaves flexible mode selected with neutral gains. The fixture uses a fake audio service so it tests UI config parsing without conflating the separately maintained DspHost lifecycle fixture.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-full-ui; make -C "$root" -f Makefile.t06 -j2 mainwindow_config_integration_test; dbus-run-session -- env QT_QPA_PLATFORM=offscreen timeout 120 "$root/mainwindow_config_integration_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The real MainWindow initialized offscreen; IPC fixtures passed and emitted the expected three invalid-EQ diagnostics; both whitespace checks passed. The only Qt notice was the offscreen platform's unsupported system-tray signal.
- Diagnostic experiment: an early combined UI/native-liveprog fixture failed because `MainWindow` reloads persisted config during construction and subsequent malformed-IPC fixture updates changed the Liveprog enable/file state. Repeated checks showed the UI-created property slider existed but that later host state was not the initialized state assumed by the fixture. The unrelated assertions were removed rather than converting this config-boundary fixture into a mixed-state test. T04/T05 remain `IN_PROGRESS` for the documented combined status-to-host UI acceptance; their dedicated editor/native/host regressions remain green as recorded above.
- Board update: T06 is `VERIFIED`; its IPC/offscreen acceptance checkbox is checked. T03/T07/T08/T16 and T00–T02 remain verified; T04/T05 retain their documented integration gap; T09 remains blocked on the recorded TSan processing-state race. No application launch/restart, install, publication, commit, or Flatpak permission change occurred.
- Commit/PR: not committed.

### Luna implementation session — T11 atomic saves verified — 2026-09-16

- Worker: Codex implementation session in isolated worktree `luna/2026-09-15-roadmap`.
- Files changed in this checkpoint: `src/subprojects/EELEditor/src/model/codecontainer.h`, `src/subprojects/EELEditor/src/eeleditor.cpp`, `src/data/EelParser.{h,cpp}`, `src/interface/LiveprogSelectionWidget.cpp`, `src/tests/{eeleditor_workflow_test.cpp,codecontainer_save_test.cpp,liveprog_editor_bridge_test.cpp}`, the corresponding test `.pro` files, and this roadmap. Existing source edits in the shared checkout were not modified.
- TDD evidence: the workflow regression first failed because successful saves left `QTextDocument::isModified()` true. After that was fixed, the injected parameter-save failure first failed because the headless build exposed no error string. Root cause: the error-message assignment was incorrectly conditional on non-headless UI compilation. Diagnostic propagation is now independent of QMessageBox display and supplies a useful deterministic reason when QSaveFile reports an empty error (as happens in the injected commit-failure case). The initial GUI workflow test invocation without `QT_QPA_PLATFORM=offscreen` was stopped after it hung in a dialog; all bounded reruns explicitly set offscreen. No product regression was inferred from that invocation.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t11-save; make -C "$root/liveprog-widget" -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/liveprog-widget/liveprog_editor_bridge_test"; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/workflow/eeleditor_workflow_test"; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$root/codecontainer_save_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. All three targets were built with ASan/UBSan and leak detection. Bridge assertions passed for failed numeric parameter persistence: error signal names the target, file bytes remain unchanged, and neither reload nor VM variable-change signal fires. Workflow assertions passed for normal save/Save As/Run and injected failures: source remains modified and available, disk bytes are unchanged, Save As retains the prior path, and no Saved/Execution success signal escapes. Direct save regression passed for nonexistent destination, Unicode destination, and injected commit failure; the Unicode normal-save and standard save cases passed. Only expected QSaveFile failure diagnostics and Qt offscreen plugin notices appeared; sanitizer checks and whitespace checks passed.
- Board update: T11 is `VERIFIED` against all listed acceptance criteria; T12 is the next dependency-ready task. No install, publish, commit, Flatpak permission change, or audio-app launch/restart occurred.
- Commit/PR: not committed.

### Luna implementation session — T04/T05 host/UI transaction integration and T06 final gate — 2026-09-16

- Worker: Codex implementation session in isolated worktree `luna/2026-09-15-roadmap`.
- Files changed this checkpoint: `src/audio/base/DspHost.cpp`, `src/mainwindow_config_test.pro`, `src/tests/mainwindow_config_integration_test.cpp`, and this roadmap. Existing `src/MainWindow.cpp` EQ fallback implementation was exercised unchanged.
- Regression/root cause: the initial full-UI build linked the pre-existing `/home/soloarch/Workspace/build/libjamesdsp/liblibjamesdsp.a` (mtime 2026-09-15), not the roadmap's current-source archive. Hash of selected current-source integration archive `/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp/liblibjamesdsp.a`: `c1a01aa480cbbc26427266458b06dc621e6818a528380500805d63cc462ac1fc`. The test `.pro` now requires `DSP_LIB_DIR` explicitly and removes the default stale archive from its link inputs; the shared archive was not overwritten. With the current archive, a separate real ordering defect was reproduced: on explicit disable `DspHost` emitted compile success before applying `LiveProgDisable`, so MainWindow read the still-active host state and re-entered config application, leaving UI enabled while host ended disabled.
- Implementation: successful Liveprog candidates now apply enable/disable before emitting `EelCompilerResult`. Failed candidates still emit diagnostics without changing the last-good VM, enabled bit, or audio. The real MainWindow fixture now checks startup active status; legacy gain slider source persistence and unity processing; bundled high-pass `freq` control change 100→200 Hz against the independently computed first impulse coefficient; syntax and temporarily missing-file candidate rejection with old output/UI continuity; explicit disable; plus T06 IPC short/non-finite/flexible-vector neutral fallback.
- Integration verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-full-ui; qmake6 src/mainwindow_config_test.pro DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp -o "$root/Makefile.t04"; make -C "$root" -f Makefile.t04 -j2 mainwindow_config_integration_test; dbus-run-session -- env QT_QPA_PLATFORM=offscreen timeout 120 "$root/mainwindow_config_integration_test" > /home/soloarch/Workspace/build/t04-t06-mainwindow-green.log 2>&1; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The fixture initialized the actual MainWindow and passed all T04/T05/T06 assertions. Expected logs: legacy host reports `dB`/`freq` are not safe for direct live setting and invokes persisted reload; malformed syntax and missing file warnings; three invalid EQ diagnostics at host/UI. No assertion, timeout, ASan, or UBSan failure. Qt offscreen emits its known unsupported tray signal notice.
- Sanitizer/native verification command: `set -euo pipefail; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 180 /home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp/tests/liveprog_runtime_test resources/assets/liveprog/gainControl.eel resources/assets/liveprog/highpass200Hz.eel /home/soloarch/Workspace/projects/jamesdsp-liveprog/liveprogLifecycleDiagnostic.eel; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-dsp_host_reload_test-final-current/dsp_host_reload_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /home/soloarch/Workspace/build/jamesdsp-luna-liveprog_editor_bridge_test-final-current/liveprog_editor_bridge_test; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Native lifecycle/runtime, freshly rebuilt ASan/UBSan DspHost reload test, and ASan/UBSan editor bridge test all passed; output contained only expected malformed-config and rejected-script diagnostics; whitespace checks passed.
- Full Qt app build check: `set -euo pipefail; make -C /home/soloarch/Workspace/build/jamesdsp-luna-full-ui -j2; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0; the GUI application linked and was not launched. This existing app Makefile still selects `/home/soloarch/Workspace/build/libjamesdsp` (the stale archive identified above); therefore current-native integration evidence is specifically from the explicit-archive test target, not claimed from that full-app binary.
- Board update: T04, T05, and T06 are `VERIFIED`; T00–T08 are verified. T09 remains `BLOCKED` on the recorded TSan state-publication race. No install, publication, commit, Flatpak permission change, or audio-app launch/restart was performed.
- Next dependency-ready task: T11 (prerequisites T04/T05 now verified); T10/T13/T14 remain gated on T09.
- Commit/PR: not committed.

### Luna implementation session — T11 verified; T12/T15 continued — 2026-09-16

- Worker: Codex implementation session in isolated worktree `luna/2026-09-15-roadmap`.
- T11 verification and acceptance evidence are recorded in the preceding T11 session entry; board state is `VERIFIED`.
- T12 sanitizer verification command: `set -euo pipefail; parser=/home/soloarch/Workspace/build/jamesdsp-luna-t12-parser; mkdir -p "$parser"; qmake6 src/tests/eel_parser_test.pro -o "$parser/Makefile"; make -C "$parser" -j2 eel_parser_test; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$parser/eel_parser_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Parser cases passed for step/minimum quantization, high-precision disk formatting, identifier boundaries, comments, unsupported expression assignment, duplicate enum metadata and invalid metadata diagnostics.
- T12 native UI/disk/VM verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-full-ui; qmake6 src/mainwindow_config_test.pro DSP_LIB_DIR=/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp -o "$root/Makefile.t04"; make -C "$root" -f Makefile.t04 -j2 mainwindow_config_integration_test; dbus-run-session -- env QT_QPA_PLATFORM=offscreen timeout 120 "$root/mainwindow_config_integration_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. The new 0.001-step `gain` control moves from 0.100 to 0.357 in the UI; the persisted script contains `gain = 0.357;`; a native processing impulse returns 0.357 within 0.0001. Existing high-pass control and config/host continuity assertions also passed. The test links the explicitly selected current-source archive at `/home/soloarch/Workspace/build/jamesdsp-luna-t20-final-headless/libjamesdsp`; it does not use the stale shared default archive.
- T15 change: `src/tests/eeleditor_workflow_test.cpp` now rejects the Save As dialog deterministically after an unsaved edit and asserts that active path/tooltip, source text, dirty state and save-signal count are unchanged.
- T15 exact verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t11-save; make -C "$root/workflow" -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/workflow/eeleditor_workflow_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Workflow assertions passed for Run’s selected path, Save As adoption and original preservation, subsequent Save, failed Save/Run/Save As, immediate tab-switch text restoration, Save As cancellation, and closing the adopted-path row. Actual MainWindow integration confirms one host consumer and invalid-source continuity. Reopen/shutdown permutations remain open; T15 remains `IN_PROGRESS`.
- T15 follow-up: `src/tests/eeleditor_workflow_test.cpp` also closes and reopens the Save-As-adopted path, checks the persisted text is restored, then exits the editor fixture under ASan/UBSan. Exact command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t11-save; make -C "$root/workflow" -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/workflow/eeleditor_workflow_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. Unsaved-new-document behavior remains untested; T15 stays `IN_PROGRESS`.
- T15 final follow-up: the same workflow fixture now uses the actual New Script wizard with temporary directory/file name, modifies the generated new document and Runs it. It checks the execution path, saved contents, clean document state, adopted-path close/reopen, and destructor shutdown. Exact command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t11-save; make -C "$root/workflow" -j2; QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/workflow/eeleditor_workflow_test"; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0, including the new-document assertions.
- Board update: T11, T12 and T15 `VERIFIED`; T09 remains `BLOCKED`, so T10/T13/T14 and downstream T17–T20 are now explicitly `BLOCKED` by dependencies. The already documented T09 TSan lock-order/state-publication race requires a complete candidate-state publication/reclamation design coordinated with T13; widening the current mutex around effect refresh can deadlock and hold the processing callback during allocations/convolver rebuilds. No install, publish, commit, Flatpak permission change, or running audio application launch/restart occurred.
- Commit/PR: not committed.

### Luna implementation session — T14 Airwindows behavior and NaN-guard regression — 2026-09-16

- Worker: Codex implementation session in isolated worktree `luna/2026-09-15-roadmap`.
- Files changed: `libjamesdsp/tests/liveprog_runtime_test.c`, `docs/testing.md`, and this roadmap. The per-script native corpus loop now snapshots `liveprogNonFiniteSamples` and asserts it does not change after that script’s complete stimuli/control/block/rate/reload exercise; on failure it prints the script path and count before aborting. This prevents the host NaN/Inf-to-zero guard from masking corpus errors. The two maintained Airwindows ports are explicitly documented as behavioral checks, with no pinned-upstream parity claim, plus finite/non-finite and magnitude bounds, controls/rates/blocks/stimuli, and per-script timeout.
- Verification command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t14-nonfinite; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += DEBUG_ASAN' -o "$root/Makefile"; make -C "$root" -j2 liblibjamesdsp.a; qmake6 libjamesdsp/tests/tests.pro 'CONFIG += DEBUG_ASAN' -o "$root/test/Makefile"; make -C "$root/test" -j2 liveprog_runtime_test; ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 300 python3 -u libjamesdsp/tests/run_eel_corpus.py --inventory /home/soloarch/Workspace/build/jamesdsp-luna-t14-control-sweep/eel_corpus_inventory.json libjamesdsp/tests/eel_corpus_manifest.json "$root/test/liveprog_runtime_test"; python3 -m py_compile libjamesdsp/tests/run_eel_corpus.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0. All 50 manifest outcomes passed: 48 scripts compiled and completed the sanitizer/stimulus/sweep assertions; 2 known unsupported examples were rejected as expected. The external `awBaxandall.eel` and `awInterstage.eel` both passed the behavioral checks. The editor `demo.eel` rejection diagnostic is expected. ASan/UBSan and Python syntax checks passed.
- Experiment note: the first fresh-run attempt stopped in the runtime harness’s pre-corpus baseline at `assert(jdsp->liveprogEnabled)`, before the new per-script check ran. Rebuilding/relinking the sanitizer native archive and runtime target from the isolated worktree yielded a green baseline and full corpus; no script failure was suppressed.
- Board update: T14’s Airwindows behavior-check criterion is checked, but T14 remains `IN_PROGRESS` pending blocked prerequisite T09. T11/T12/T15 remain `VERIFIED`; T09 remains `BLOCKED`, with dependent T10/T13/T14/T17–T20 gated. No install, publish, commit, Flatpak permission change, or audio-app launch/restart occurred.
- Commit/PR: not committed.

### Luna dependency checkpoint — reachable work complete; T09 gate remains — 2026-09-16

- Completed this continuation’s dependency-ready acceptance: T11, T12 and T15 are `VERIFIED`; the 50-entry T14 corpus/Airwindows behavior criterion also passes, but T14 cannot be promoted past its blocked T09 prerequisite.
- Exact final integrity checks: `git diff --check; git -C src/subprojects/EELEditor diff --check` -> exited 0.
- Dependency board: T00–T08, T11, T12, T15 and T16 are `VERIFIED`. T09 is `BLOCKED` by its already reproduced TSan lock-order/state-publication race. T10, T13, T14, T17, T18, T19 and T20 are now explicitly `BLOCKED` by the dependency graph; their remaining work cannot be safely accepted until the T09 processing-state ownership/publication design is resolved. The earlier T09 log explains why widening the current lock is unsafe.
- No further dependency-ready task remains in T01–T20. No install, publish, commit, Flatpak permission change, or running audio-app launch/restart occurred. Existing shared user changes were preserved; all build/test artifacts stayed under `/home/soloarch/Workspace/build/`.
- Commit/PR: not committed.

### Luna implementation session — T09 quiescence gate and strict TSan closure — 2026-09-16

- Worker: Codex implementation session in isolated worktree `luna/2026-09-15-roadmap`.
- Files changed this checkpoint: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`, `jdsp_header.h`, `generalDSP/TwoStageFFTConvolver.c`, `libjamesdsp/tests/rate_transition_test.c`, `docs/testing.md`, and this roadmap. Processing entrypoints now acquire a bounded atomic read admission; rate/buffer/shutdown transitions pause new readers and drain current readers on the control thread. Fixed convolver setup-wait mutex ownership to avoid the TSan-detected lock-order cycle during crossfeed rate refresh. Documented rate reinitialization semantics: `@init` state resets, host slider values survive, and `@slider` recomputes derived coefficients before readers resume.
- Exact strict TSan command: `setarch "$(uname -m)" -R env JDSP_RATE_TSAN_SKIP_CROSSFEED=1 TSAN_OPTIONS=halt_on_error=1 timeout 180 /home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-tsan/libjamesdsp/tests/rate_transition_test` -> exit 0; output `rate transition test passed`; no TSan race or lock-order diagnostic. This rate target was rebuilt from the current source with `-fsanitize=thread -fno-omit-frame-pointer` on library and test.
- Exact strict ASan/UBSan rebuild/run command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t09-rate-effects-asan/libjamesdsp; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -B -j2; for test in rate_transition_test native_rate_refresh_cli_test; do qmake6 "libjamesdsp/tests/$test.pro" 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/tests/$test.Makefile"; make -C "$root/tests" -f "$test.Makefile" -B -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$root/tests/$test"; done` -> exit 0; outputs `rate transition test passed` and `bounded native rate refresh passed`; no ASan/UBSan diagnostic. Prior independent sanitizer checks remain green for runtime, allocation, and T01 ASRC tests (see their earlier entries).
- Integrity command: `git diff --check; git -C src/subprojects/EELEditor diff --check` -> exit 0.
- Board update: T09 is now `VERIFIED`; T10 is `IN_PROGRESS` as the next dependency-ordered task. T13/T14 and T17–T20 remain dependency-gated. No install, publish, commit, Flatpak permission change, or running audio application launch/restart occurred. All build artifacts remain under `/home/soloarch/Workspace/build/`; shared checkout/user work was preserved.
- Commit/PR: not committed.

### Luna implementation session — T10 rate-active crossfeed lifecycle closure — 2026-09-20

- Worker: Codex implementation session in isolated worktree `luna/2026-09-15-roadmap`.
- Files changed: `libjamesdsp/tests/crossfeed_lifecycle_test.c` and this roadmap. The lifecycle fixture now cycles 44.1/48/96/48 kHz with crossfeed enabled before disable and shutdown, in addition to its unchanged-enable identity assertion and concurrent processor/toggler stress.
- Exact strict TSan command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t10-tsan/libjamesdsp; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/Makefile"; make -C "$root" -B -j2; qmake6 libjamesdsp/tests/crossfeed_lifecycle_test.pro 'CONFIG += DEBUG_TSAN' 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/tests/crossfeed_lifecycle_test.Makefile"; make -C "$root/tests" -f crossfeed_lifecycle_test.Makefile -B -j2; setarch "$(uname -m)" -R env TSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/crossfeed_lifecycle_test"` -> exit 0; output `crossfeed lifecycle test passed`; no TSan race or lock-order diagnostic.
- Exact ASan/UBSan command: same qmake/make sequence under `/home/soloarch/Workspace/build/jamesdsp-luna-t10-asan/libjamesdsp` with `-fsanitize=address,undefined -fno-omit-frame-pointer`, followed by `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/crossfeed_lifecycle_test"` -> exit 0; output `crossfeed lifecycle test passed`; no sanitizer diagnostic.
- Acceptance result: unchanged enable retains the selected long-convolver identity; concurrent processing and replacement/toggling complete; active crossfeed survives repeated rate refreshes; explicit disable and `JamesDSPFree` complete cleanly. T10 is now `VERIFIED`. No install, publish, commit, Flatpak permission change, or running audio application launch/restart occurred. Build outputs remain under `/home/soloarch/Workspace/build/`.
- Next action: T13 callback-boundary instrumentation and lifecycle acceptance; T14 remains ready after T09/T10/T12 prerequisites.
- Commit/PR: not committed.

### Luna implementation session — T13 callback-boundary evidence and T14 promotion — 2026-09-20

- T13 exact ASan/UBSan command: `set -euo pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-asan/libjamesdsp; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -B -j2; for test in liveprog_callback_probe_test liveprog_reload_lock_test; do qmake6 "libjamesdsp/tests/$test.pro" 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/tests/$test.Makefile"; make -C "$root/tests" -f "$test.Makefile" -B -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 180 "$root/tests/$test"; done` -> exit 0. Callback probe output: `liveprog callback allocation probe passed`; reload-lock test exit 0 with no sanitizer diagnostic. The probe observed zero callback allocations/frees and allocations during explicit control-side quantum preparation.
- T13 strict TSan command: dedicated `libjamesdsp/tests/liveprog_reload_lock_tsan_test.pro` (the existing `.pro` hard-enables ASan/UBSan; attempting to add TSan to it produced GCC diagnostic `-fsanitize=thread is incompatible with -fsanitize=address`, so that failed configuration was not counted) built the same source with `-fsanitize=thread -fno-omit-frame-pointer`; `setarch "$(uname -m)" -R env TSAN_OPTIONS=halt_on_error=1 timeout 180 /home/soloarch/Workspace/build/jamesdsp-luna-t13-tsan/libjamesdsp/tests/liveprog_reload_lock_tsan_test` -> exit 0, no TSan diagnostic. T13 remains `IN_PROGRESS`: synchronous candidate compilation/publication and retired-state destruction are still host-path operations, and the deliberately slow `@init` fixture proves active processing continuity but does not establish a complete off-callback candidate worker/reclamation design.
- T14 promotion basis: the existing strict ASan/UBSan full-corpus run recorded in the 2026-09-16 T14 entry passed all 50 manifest outcomes (48 valid scripts and 2 expected unsupported outcomes), including stimuli, controls, block sizes, rate reloads, Airwindows behavioral checks and pre-sanitization non-finite counters. T09/T10 prerequisites are now verified; T14’s acceptance boxes are complete, so T14 is now `VERIFIED`. This is not an acoustic-quality or upstream parity claim.
- Integrity command: `git diff --check; git -C src/subprojects/EELEditor diff --check` -> exit 0. No install, publish, commit, Flatpak permission change, or running audio application launch/restart occurred. Build outputs remain under `/home/soloarch/Workspace/build/`.
- Next action: T13’s minimal safe next slice is to separate candidate construction/publication from the processing reader contract; T17–T20 remain blocked on T13 and release-gate evidence.
- Commit/PR: not committed.

### Luna implementation session — append-only final handoff — 2026-09-20

- Final board: T00–T12, T14–T16 and T19 are `VERIFIED`; T13 is `IN_PROGRESS`; T17, T18 and T20 are `BLOCKED` by T13/live-hosted acceptance. T10’s rate-active crossfeed lifecycle evidence and T19’s restored EELVault fixture evidence are included above.
- Exact final checks: `python3 meta/tests/workflow_contract_test.py; BUILD_ROOT=/home/soloarch/Workspace/build/jamesdsp-luna-t17-current-20260920 meta/tests/packaging_contract_test.sh; bash src/tests/pipewire_rt_contract_test.sh; bash src/tests/pulse_wrapper_contract_test.sh; bash src/tests/visual_theme_registry_test.sh; bash -n meta/flatpak/build-local-bundle.sh meta/build_deb_package.sh; python3 -m py_compile meta/tests/workflow_contract_test.py libjamesdsp/tests/run_eel_corpus.py libjamesdsp/tests/compare_eel_manifest.py; git diff --check; git -C src/subprojects/EELEditor diff --check` -> all passed.
- Open risk/blocker: T13 has no live GStreamer development environment (`gstreamer-1.0` pkg-config entries and `gst/gst.h` are absent), so no production-linked PipeWire/PulseAudio device transition or installed-runtime claim is made. The safe offline design and sanitizer evidence are recorded; next action is backend-linked runtime verification when that environment is available.
- No install, publish, commit, Flatpak permission change, or running audio application restart occurred. All generated builds and reports remain under `/home/soloarch/Workspace/build/`; shared user work was preserved.

### Luna implementation session — T13 backend-linked HEADLESS validation — 2026-09-21

- Preflight: clean isolated branch `luna/2026-09-15-roadmap`; no source reset or overwrite. `pkg-config --modversion gstreamer-1.0 gstreamer-audio-1.0 libpulse libpipewire-0.3` -> `1.24.2`, `1.24.2`, `16.1`, `1.0.5`. PipeWire pkg-config is available; the prior missing-package limitation is cleared.
- The first PulseAudio qmake/make attempt correctly stopped because the application target requires a sibling out-of-tree native archive: `No rule to make target .../pulse/../libjamesdsp/liblibjamesdsp.a`. This was a build-layout issue only. The corrected command built the native archive first under `/home/soloarch/Workspace/build/jamesdsp-luna-t13-backends-20260921/libjamesdsp`, then built both backend targets: `qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' -o "$root/libjamesdsp/Makefile"; make -C "$root/libjamesdsp" -j2; qmake6 src/src.pro 'CONFIG += HEADLESS USE_PULSEAUDIO' -o "$root/pulse/Makefile"; make -C "$root/pulse" -j2; qmake6 src/src.pro 'CONFIG += HEADLESS' -o "$root/pipewire/Makefile"; make -C "$root/pipewire" -j2` -> exit 0 for native, PulseAudio and PipeWire builds.
- Link evidence: `ldd "$root/pulse/jamesdsp"` showed `libgstreamer-1.0.so.0` and `libpulse.so.0`; `ldd "$root/pipewire/jamesdsp"` showed `libpipewire-0.3.so.0`. `readelf -d` reported the same NEEDED entries. No backend binary was launched, so the installed running audio application was not restarted or disturbed.
- Wrapper/backend checks: `gcc -fsyntax-only -std=gnu11 $(pkg-config --cflags gstreamer-1.0 gstreamer-audio-1.0 libpulse) -Ilibjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp -Isrc/audio/pulseaudio/wrapper src/audio/pulseaudio/wrapper/gstjamesdsp.c` -> exit 0; `bash src/tests/pulse_wrapper_contract_test.sh` -> `PulseAudio wrapper contract passed`; `bash src/tests/pipewire_rt_contract_test.sh` -> `PipeWire RT callback contract passed`; `git diff --check` and editor-submodule diff check -> exit 0.
- T13 remains `IN_PROGRESS`, not VERIFIED: backend-linked compilation and static callback contracts are now covered for both installed backend stacks, but live device negotiation/format transitions and runtime shutdown were intentionally not exercised under the no-restart constraint. Existing ASan/UBSan and TSan callback/reload evidence remains valid. Next action requires an explicitly coordinated disposable backend runtime fixture or authorization to launch the freshly built headless target; no installed application restart is implied.
- No install, publish, commit, Flatpak permission change, or running audio application restart occurred. All build output remains under `/home/soloarch/Workspace/build/`.

### Luna implementation session — T13 isolated PipeWire runtime lead slice — 2026-09-21

- Startup-path finding: non-CLI startup registers `me.timschneeberger.jdsp4linux` on the session D-Bus, loads XDG config/cache/data through `AppConfig`, constructs `PipewireAudioService`, creates the PipeWire virtual `jamesdsp_sink`, and enters `QCoreApplication::exec()`. `PipewireAudioService`/`PwPipelineManager` destruction disconnects and stops the PipeWire loop. PulseAudio startup similarly constructs a GStreamer/Pulse pipeline and starts `PulseAudioProcessingThread`; the host has no standalone `pulseaudio` daemon executable, only the installed PulseAudio protocol served by PipeWire.
- Safe launch environment: a disposable PipeWire daemon was started with `XDG_RUNTIME_DIR` under `/home/soloarch/Workspace/build/jamesdsp-luna-t13-runtime-20260921/runtime-*`; the built app used separate `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `HOME`, `PIPEWIRE_REMOTE=pipewire-0`, `PULSE_SINK=output.t13-output`, and `dbus-run-session`. A disposable `pw-loopback -r pipewire-0 -n t13-output` supplied only isolated test nodes. No installed app process or system PipeWire/PulseAudio server was contacted.
- Exact isolated runtime command: `XDG_RUNTIME_DIR="$runtime" pipewire`; `XDG_RUNTIME_DIR="$runtime" pw-loopback -r pipewire-0 -n t13-output`; `env XDG_RUNTIME_DIR="$runtime" XDG_CONFIG_HOME="$config" XDG_CACHE_HOME="$cache" XDG_DATA_HOME="$home/data" HOME="$home" PIPEWIRE_REMOTE=pipewire-0 PULSE_SINK=output.t13-output dbus-run-session -- timeout --signal=TERM --kill-after=5 15 /home/soloarch/Workspace/build/jamesdsp-luna-t13-backends-20260921/pipewire/jamesdsp --min-verbosity 0 --no-color` -> exit `124` from the deliberate timeout. The log showed PipeWire 1.0.5 connection, private `jamesdsp_sink` creation, `JamesDsp successfully connected to PipeWire graph`, attempted link to `output.t13-output`, and the bounded warning `Information about the ports of the output device output.t13-output ... taking to long ... Aborting the link`. There was no crash or sanitizer output. After termination, `pw-cli` listed only the disposable loopback nodes; the private JamesDSP node was gone, demonstrating client-resource cleanup on process termination.
- Initial isolated launch with `PULSE_SINK=t13-output` demonstrated the name mismatch path: the loopback nodes are `output.t13-output`/`input.t13-output`, so FilterContainer logged `No output device set. Aborting the link`; this was corrected in the stronger run above. The first pipewire invocation used unsupported `--no-daemon` and failed with `pipewire: unrecognized option '--no-daemon'`; the corrected foreground `pipewire` invocation passed.
- Runtime boundary: the disposable loopback exposed no negotiated audio ports, so successful output-device format/rate negotiation could not be established. No system route was changed to manufacture one. The host has no standalone `pulseaudio` executable (`which pulseaudio` returned none), preventing a separate private PulseAudio daemon; the existing PulseAudio protocol server is the installed system PipeWire and was intentionally not used by the app runtime test. PulseAudio remains covered by the backend-linked build, GStreamer wrapper syntax, and static contract tests.
- Read-only system post-check: `pactl info` still reported `Server Name: PulseAudio (on PipeWire 1.0.5)`, `Default Sink: jamesdsp_sink`, and the same default source; `pw-cli info 0` still reported core `pipewire-0`, version `1.0.5`, and `default.clock.rate = 48000`. No temporary PipeWire/loopback process remained. T13 remains `IN_PROGRESS`; the smallest blocker is a disposable backend fixture with negotiated ports (or coordinated authorization for a non-installed runtime device fixture) to prove actual format/rate transition and graceful event-loop shutdown. No install, publish, commit, Flatpak permission change, or installed-app restart occurred.

### Luna implementation session — T13 disposable negotiated-port fixture investigation — 2026-09-21

- Reproduced the prior no-ports result first. Exact isolated command sequence: `runtime=$(mktemp -d /home/soloarch/Workspace/build/repro-runtime-XXXXXX); XDG_RUNTIME_DIR="$runtime" pipewire >repro-noports-pipewire.log 2>&1 &; XDG_RUNTIME_DIR="$runtime" pw-loopback -r pipewire-0 -n t13-output >repro-noports-loopback.log 2>&1 &; XDG_RUNTIME_DIR="$runtime" pw-cli -r pipewire-0 ls Node >repro-noports-nodes.log; XDG_RUNTIME_DIR="$runtime" pw-cli -r pipewire-0 ls Port >repro-noports-ports.log`. Result: nodes `output.t13-output` and `input.t13-output` existed, but `wc -l repro-noports-ports.log` returned `0`; standalone PipeWire has no session-manager negotiation for the loopback.
- Tested the smallest explicit-property variation with short runtime paths to avoid PipeWire’s Unix socket limit: `pw-loopback -r pipewire-0 -n t13-explicit -c 2 -m '[ FL, FR ]' -l 10 --capture-props '{"media.class":"Audio/Source","audio.rate":44100,"audio.channels":2,"audio.position":["FL","FR"]}' --playback-props '{"media.class":"Audio/Sink","audio.rate":44100,"audio.channels":2,"audio.position":["FL","FR"]}'`. The nodes were correctly named `output.t13-explicit`/`input.t13-explicit`, but the standalone daemon still returned `JSON_PORT_LINES=0`. The first long-path attempt failed before graph startup with `socket path ... exceeds 108 bytes`, `could not load mandatory module "libpipewire-module-protocol-native": File name too long`, and `failed to create context: File name too long`; this is a fixture-path limitation, not a production failure. An initial `pw-play /dev/zero` attempt also failed with `sndfile: failed to open audio file "/dev/zero": Format not recognised`; raw streams must use `pw-cat` with stdin and dynamically discovered node names.
- Added only disposable WirePlumber to the same private PipeWire runtime, retaining JSON loopback properties. Exact variation: `XDG_RUNTIME_DIR="$runtime" pipewire &; XDG_RUNTIME_DIR="$runtime" wireplumber &; XDG_RUNTIME_DIR="$runtime" pw-loopback -r pipewire-0 -n t13-wire -c 2 -m '[ FL, FR ]' -l 10 --capture-props '{"media.class":"Audio/Source","audio.rate":44100,"audio.channels":2,"audio.position":["FL","FR"]}' --playback-props '{"media.class":"Audio/Sink","audio.rate":44100,"audio.channels":2,"audio.position":["FL","FR"]}'`. Result: `pw-cli` reported `output.t13-wire`/`input.t13-wire` and `52` port lines. This is the first fixture variation that exposes negotiated ports. WirePlumber emitted nonfatal missing-libcamera, unavailable-BlueZ and portal-camera diagnostics, and enumerated/churned host ALSA/V4L2 objects inside the private daemon; that host-device enumeration makes the fixture unsuitable as a deterministic acceptance environment.
- Active-stream backend attempt used only build-local XDG state and the built binary `/home/soloarch/Workspace/build/jamesdsp-luna-t13-backends-20260921/pipewire/jamesdsp`: `env XDG_RUNTIME_DIR="$runtime" XDG_CONFIG_HOME="$config" XDG_CACHE_HOME="$cache" XDG_DATA_HOME="$home/data" HOME="$home" PIPEWIRE_REMOTE=pipewire-0 PULSE_SINK=output.t13-runtime dbus-run-session -- timeout --signal=TERM --kill-after=5 20 /home/soloarch/Workspace/build/jamesdsp-luna-t13-backends-20260921/pipewire/jamesdsp --min-verbosity 0 --no-color`, with raw 44.1-kHz `pw-cat` playback/record streams directed at dynamically discovered loopback nodes. Before app launch: `PORTS_BEFORE_LINES=138 LINKS_BEFORE_LINES=16`; after app launch: `APP_PORTS_LINES=220 APP_LINKS_LINES=48`; after the rate-stream transition: `RATE_PORTS_LINES=240 RATE_LINKS_LINES=64`. The backend log proved filter creation (`PwBasePlugin::connect_to_pw: @PwJamesDspPlugin: JamesDsp successfully connected to PipeWire graph`), but also reported `FilterContainer::connect_filters: link from node 71 to output device 45 failed`, repeated `Remote error res: No such file or directory`/`unknown resource ... op:2`, and WirePlumber/ALSA resource churn including `Device or resource busy` and `error -95 start failed`. Therefore this did not prove a connected processing path or a valid format/rate transition.
- A no-stream control run against the WirePlumber fixture confirmed the same boundary: `RUNTIME=/home/soloarch/Workspace/build/jdsp-t13-y0ILGD TARGET=output.t13-nostream PORT_LINES=52 LINK_LINES=0`, then the built backend under `dbus-run-session` created `jamesdsp_sink` and `jamesdsp_filter`, emitted `JamesDsp successfully connected to PipeWire graph`, and exited via the deliberate timeout with no crash or sanitizer report. Its post-run link dump contained only two links between the private graph nodes; repeated unknown-resource errors and host ALSA node destruction remained. This establishes filter creation and cleanup, not negotiated processing or transition acceptance.
- T13 remains `IN_PROGRESS`. No deterministic disposable fixture was found that both exposes negotiated ports and provides a stable JamesDSP output link through active playback/rate transition; no production-code change is justified by these fixture failures. All processes, configs, logs and streams were confined to `/home/soloarch/Workspace/build`; the installed PipeWire/PulseAudio graph and running JamesDSP application were not contacted or restarted. Exact artifacts are under `/home/soloarch/Workspace/build/jamesdsp-luna-t13-runtime-20260921/` and `/home/soloarch/Workspace/build/jdsp-t13-y0ILGD/`. Smallest next blocker: a session-manager fixture that can suppress host-device enumeration while retaining negotiated loopback ports, or separately coordinated runtime-device authorization. Board remains T13 `IN_PROGRESS`; no T17/T18/T20 promotion is warranted.
- Integrity result before commit: `git diff --check` and `git -C src/subprojects/EELEditor diff --check` -> exit 0. No install, publish, Flatpak permission change, running-app restart, or source cleanup occurred.

### Codex continuation — T13 host-monitor isolation refinement — 2026-09-21

- Created a disposable WirePlumber configuration under `/home/soloarch/Workspace/build/jdsp-t13-fixture-test/wp` that sets `alsa_monitor.enabled = false`, `v4l2_monitor.enabled = false`, and `libcamera_monitor.enabled = false`. A private PipeWire + WirePlumber run then exposed only the Dummy/Freewheel drivers before test nodes; no host ALSA/V4L2 device churn was observed. The system PipeWire/PulseAudio graph was not contacted.
- A private `pw-loopback` with `output.t13-wire`/`input.t13-wire` and `pw-cat` streams produced stable disposable nodes and active stream links (`nodes=6`, `ports=11`, `links=2` in the fixture-only run) with 44.1-kHz capture data written under `build/`. This confirms monitor suppression is effective and stream negotiation can occur in the isolated daemon.
- Launching the backend-linked headless JamesDSP binary against the same fixture created `jamesdsp_sink` and `jdsp_@PwJamesDspPlugin_JamesDsp`, and emitted `JamesDsp successfully connected to PipeWire graph`; playback and capture streams ran without a crash and produced `capture_bytes=707364` in the first run. However, the loopback target exposed only one `FR` port in the tested property form, so the stereo filter-to-output link failed (`link from node 63 to output device 33 failed`). A warmed-stream retry still reproduced the output-link failure while proving the private graph remained isolated.
- T13 therefore remains `IN_PROGRESS`. No production source change, installed-app restart, Flatpak permission change, or system graph mutation was made. Next smallest slice is to make the disposable loopback expose two correctly labelled `FL`/`FR` ports (or use an equivalent two-channel virtual sink/source) before retrying the JamesDSP link and rate-transition acceptance.

### Codex continuation — T13 two-channel null-sink fixture — 2026-09-21

- Replaced the problematic `pw-loopback` target with two build-local PipeWire `support.null-audio-sink` adapter objects in `/home/soloarch/Workspace/build/jdsp-t13-fixture-test/pipewire-null.conf`: `output.t13-null` (`Audio/Sink`, `audio.position = "FL,FR"`) and an isolated source object. WirePlumber still ran with ALSA/V4L2/libcamera monitors disabled, so no host-device enumeration was involved.
- With private 48-kHz warmup and 44.1-kHz/48-kHz `pw-cat` streams, `output.t13-null` exposed stable stereo playback and monitor ports (`playback_FL`, `playback_FR`, `monitor_FL`, `monitor_FR`). The backend-linked headless JamesDSP process created `jamesdsp_sink` and its PipeWire filter, exited via the deliberate timeout without crash/sanitizer output, and emitted no output-link, unknown-resource, or remote-error diagnostics.
- Runtime snapshots recorded eight active links at both the 44.1-kHz and 48-kHz stream phases. The link topology included the application stream → `jamesdsp_sink` → JamesDSP filter → `output.t13-null` stereo sink. This is the first isolated fixture run that proves a connected stereo processing path and a clean stream-rate handoff without touching the installed graph.
- T13 remains `IN_PROGRESS` because the broader task still includes the processing-state ownership/reclamation design and sanitizer-backed shutdown evidence; this runtime slice closes the former disposable-device fixture blocker. No production source change, installed-app restart, Flatpak permission change, or system graph mutation was made.

### Codex continuation — T13 immutable Liveprog publication slice — 2026-09-21

- Read-only audit: `Effects/liveprogWrapper.c` compiled a candidate off-callback, swapped the embedded `jdsp->eel` under `m_in_processing`, then destroyed the previous VM after unlocking; `JamesDSPProcess` held that mutex across `LiveProgProcess`. This made the callback safe from use-after-free but allowed slow EEL execution to block control-side publication. The audit also confirmed sample-rate refresh is a control-side mutation under the existing processing admission gate.
- Red test added before production changes: `libjamesdsp/tests/liveprog_publication_test.c` and `.pro`. It runs a deliberately slow `@sample` loop concurrently with `LiveProgStringParser`, asserts reload publication latency below the default 100 ms budget, then performs a 44.1-kHz rate change and `JamesDSPFree`. The pre-fix build/run command `set -o pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-publication-red; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' -o "$root/Makefile"; make -C "$root" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro -o "$root/tests/Makefile"; make -C "$root/tests" -j2; timeout 30 "$root/tests/liveprog_publication_test"` compiled, then failed at `assert(reload_ms < 100)` in the old mutex path.
- Minimal implementation: `LiveProg` generations are heap-published through `JamesDSPLib.liveProgCurrent`; callback processing loads the current generation without `m_in_processing`, while old generations are linked to a control-owned retired list and destroyed during `JamesDSPFree` after processing admission closes. The callback no longer holds the control mutex around Liveprog execution. Existing test-only VM inspections use `JamesDSPGetCurrentLiveProgForTests`; no PipeWire fixture or production backend code changed.
- Normal verification: `set -o pipefail; root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-publication-green; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' -o "$root/libjamesdsp/Makefile"; make -C "$root/libjamesdsp" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro -o "$root/libjamesdsp/tests/Makefile"; make -C "$root/libjamesdsp/tests" -j2; timeout 30 "$root/libjamesdsp/tests/liveprog_publication_test"` -> exit 0, `reload latency during callback=34ms`, `liveprog publication test passed`.
- ASan/UBSan verification: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-publication-asan; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/tests/Makefile"; make -C "$root/tests" -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$root/tests/liveprog_publication_test"` -> exit 0, `reload latency during callback=37ms`, `liveprog publication test passed`, with no ASan/UBSan diagnostic.
- Strict TSan verification: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-publication-tsan; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/Makefile"; make -C "$root" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/tests/Makefile"; make -C "$root/tests" -j2; setarch "$(uname -m)" -R env JDSP_PUBLICATION_LATENCY_LIMIT_MS=1000 TSAN_OPTIONS=halt_on_error=1 timeout 60 "$root/tests/liveprog_publication_test"` -> exit 0, `reload latency during callback=190ms`, `liveprog publication test passed`; no TSan race or lock-order diagnostic. The larger latency budget is only for sanitizer instrumentation; normal and ASan/UBSan retain the 100 ms default.
- Acceptance status: this focused generation/publication test covers concurrent processing plus reload, rate change and shutdown, and passes normal, ASan/UBSan and strict TSan. T13 remains `IN_PROGRESS`, not `VERIFIED`, pending the broader existing Liveprog corpus/rate regression matrix and a fuller shutdown/reclamation stress pass. All build output remains under `/home/soloarch/Workspace/build/`; no installed process, system graph, Flatpak permission or backend fixture was changed.

### Codex continuation — T13 publication regression follow-up — 2026-09-21

- Existing regression commands against the published-generation library: `qmake6 libjamesdsp/tests/native_rate_refresh_cli_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/native/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/native -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/native/native_rate_refresh_cli_test` -> exit 0, `bounded native rate refresh passed`; `qmake6 libjamesdsp/tests/rate_transition_test.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/rate/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/rate -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/rate/rate_transition_test` -> exit 0, `rate transition test passed`.
- Corpus-backed runtime smoke: `qmake6 libjamesdsp/tests/tests.pro -o /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/runtime/Makefile; make -C /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/runtime -B -j2; ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60 /home/soloarch/Workspace/build/jamesdsp-luna-t13-existing-check/runtime/liveprog_runtime_test resources/assets/liveprog/swapChannels.eel` -> exit 0, `liveprog runtime test passed`. The runtime target was rebuilt after adding `JDSP_TEST_HOOKS` to its test project so the current-generation accessor is declared; no production behavior was changed for that correction.
- Final source/test scope for this slice: `liveprogWrapper.c`, `jdspController.c`, `jdsp_header.h`, the focused publication test and existing Liveprog test project/accessor updates, plus this append-only log. `git diff --check` and `git -C src/subprojects/EELEditor diff --check` -> exit 0. T13 remains `IN_PROGRESS`; no completion claim is made from the focused slice alone.

### Codex continuation — T13 control-side Liveprog ownership correction — 2026-09-21

- Blocker reproduced by audit: `LiveProgSetVariable()` previously called `jdsp_lock()` and returned without unlocking when `liveProgCurrent` was null. More importantly, after generation publication, `LiveProgSetVariable()`, `LiveProgEnable()/Disable()` and sample-rate refresh still mutated the current EEL VM while the callback could execute it.
- Regression coverage expanded in `libjamesdsp/tests/liveprog_publication_test.c`: it destroys and recreates the Liveprog state to force the no-current path, asserts `LiveProgSetVariable()` returns and `pthread_mutex_trylock(&dsp.m_in_processing)` succeeds; it concurrently runs processing with repeated slider-variable updates and 44.1/48-kHz rate changes, then verifies shutdown. `JDSP_PUBLICATION_SLOW_ITERATIONS` and `JDSP_PUBLICATION_CONTROL_ITERATIONS` only bound synthetic workload size for instrumented runs; normal defaults remain 500000 EEL loop iterations and 64 control iterations.
- Ownership fix: `processing_pause()`/`processing_resume()` are now the control-side admission API. Liveprog enable/disable and variable updates pause new callbacks and wait for readers before VM mutation; the public sample-rate refresh wrapper does the same, while `JamesDSPSetSampleRate()` calls `LiveProgRefreshSampleRatePaused()` inside its already-held pause window. The no-current path resumes admission before returning. No callback mutex was widened.
- Normal verification: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-ownership-fix-normal; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' -o "$root/Makefile"; make -C "$root" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro -o "$root/tests/Makefile"; make -C "$root/tests" -j2; timeout 90 "$root/tests/liveprog_publication_test"` -> exit 0, `reload latency during callback=37ms`, `liveprog publication test passed`.
- ASan/UBSan verification: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-ownership-fix-asan; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/Makefile"; make -C "$root" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro 'QMAKE_CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=address,undefined' -o "$root/tests/Makefile"; make -C "$root/tests" -j2; JDSP_PUBLICATION_SLOW_ITERATIONS=100 JDSP_PUBLICATION_CONTROL_ITERATIONS=8 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 90 "$root/tests/liveprog_publication_test"` -> exit 0, `reload latency during callback=36ms`, `liveprog publication test passed`; no ASan/UBSan diagnostic.
- Strict TSan verification: `root=/home/soloarch/Workspace/build/jamesdsp-luna-t13-ownership-fix-tsan; qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS' 'DEFINES += JDSP_TEST_HOOKS' 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_CXXFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/Makefile"; make -C "$root" -j2; qmake6 libjamesdsp/tests/liveprog_publication_test.pro 'QMAKE_CFLAGS += -fsanitize=thread -fno-omit-frame-pointer' 'QMAKE_LFLAGS += -fsanitize=thread' -o "$root/tests/Makefile"; make -C "$root/tests" -j2; setarch "$(uname -m)" -R env JDSP_PUBLICATION_SLOW_ITERATIONS=100 JDSP_PUBLICATION_CONTROL_ITERATIONS=8 JDSP_PUBLICATION_LATENCY_LIMIT_MS=1000 TSAN_OPTIONS=halt_on_error=1 timeout 120 "$root/tests/liveprog_publication_test"` -> exit 0, `reload latency during callback=163ms`, `liveprog publication test passed`; no TSan race or lock-order diagnostic.
- Status: the no-current lock leak and control-side VM mutation boundary are corrected and covered. T13 remains `IN_PROGRESS` pending the broader roadmap acceptance matrix and longer-lived reclamation stress; commit `728e8b5` was not pushed. All artifacts remain under `/home/soloarch/Workspace/build/`; no installed app, system graph or Flatpak configuration was touched.
