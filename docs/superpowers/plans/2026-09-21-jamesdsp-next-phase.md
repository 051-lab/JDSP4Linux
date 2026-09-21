# JamesDSP4Linux Stabilization and Release-Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the remaining high-risk T13 processing-state acceptance work, then validate the application through native, backend, packaging, Flatpak, and release-gate checks without disturbing the installed running application.

**Architecture:** Treat the current Luna branch and fork `master` as the only source line. Complete Liveprog generation publication and reclamation first, using control-side admission for all VM mutation and immutable callback-visible state. Reuse the proven disposable two-channel PipeWire fixture for backend acceptance, then execute packaging and installed-runtime checks only after native and backend evidence is green.

**Tech Stack:** C11/POSIX threads, Qt 6/qmake, PipeWire 1.0.5, WirePlumber 0.4, GStreamer 1.24, PulseAudio protocol, Flatpak, Debian packaging, ASan/UBSan, TSan, Python contract tests, EEL/Liveprog corpus.

**Spec:** `docs/superpowers/plans/2026-09-15-jamesdsp-luna-roadmap.md`

## Global Constraints

- Work only in `/home/soloarch/Workspace/upstream/JDSP4Linux-luna`; keep generated output under `/home/soloarch/Workspace/build/`.
- Preserve the installed JamesDSP process and system PipeWire/PulseAudio graph until an explicit runtime-install task is reached.
- Never widen the audio callback mutex across compilation, allocation, VM initialization, convolver rebuilds, or destruction.
- Every processing-visible state mutation must use the processing admission/publication contract; no direct control-thread mutation of an executing Liveprog VM.
- Keep the fork single-branch: publish only to `https://github.com/051-lab/JDSP4Linux.git` branch `master`.
- Do not promote T13 or unblock T17/T18/T20 from compilation alone; each promotion requires the evidence listed in this plan.
- Use short private runtime paths for PipeWire tests to stay below Unix socket path limits.

## Review Focus

- Retired Liveprog state reclaimed while a callback is still executing — covered by the long-lived publication stress test in Task 2.
- Control-side variable/rate updates racing with callback VM mutation — covered by the concurrent control test in Task 2.
- Reinitialization/destruction after publication, including no-current-generation calls — covered by Task 2 shutdown permutations.
- Backend target ports disappearing or changing channel/rate — covered by the isolated stereo PipeWire test in Task 4.
- Packaging/runtime using a different binary or stale archive than the source under test — covered by Task 7 artifact-identity checks.

## Task 1: Establish the clean baseline and ledger

**Files:**
- Read: `docs/superpowers/plans/2026-09-15-jamesdsp-luna-roadmap.md`
- Create: `.superpowers/sdd/2026-09-21-jamesdsp-next-phase/progress.md`
- Modify: none

**Interfaces:**
- Consumes: current `master` at `cd006f4` and the roadmap’s latest board.
- Produces: a reproducible baseline commit, status record, and test inventory for later tasks.

- [ ] **Step 1: Record repository identity and clean status.**

  Run:

  ```bash
  repo=/home/soloarch/Workspace/upstream/JDSP4Linux-luna
  git -C "$repo" status --short --branch
  git -C "$repo" log -5 --oneline
  git -C "$repo" diff --check
  git -C "$repo" -C src/subprojects/EELEditor diff --check
  ```

  Expected: branch is `luna/2026-09-15-roadmap`, worktree is clean, and both whitespace checks exit 0.

- [ ] **Step 2: Create the ledger and record the baseline.**

  The first line must be:

  ```text
  # SDD ledger — plan: docs/superpowers/plans/2026-09-21-jamesdsp-next-phase.md
  ```

  Record the exact `HEAD` hash, fork remote, current board (`T13 IN_PROGRESS`, `T17/T18/T20 BLOCKED`), and the fact that the installed app has not been restarted.

- [ ] **Step 3: Commit only the plan/ledger if the repository workflow requires a tracked plan.**

  Do not commit generated build output. Use:

  ```bash
  git add docs/superpowers/plans/2026-09-21-jamesdsp-next-phase.md
  git commit -m "docs: add stabilization and release-gate plan"
  ```

## Task 2: Complete long-lived Liveprog publication and reclamation stress

**Files:**
- Modify: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/Effects/liveprogWrapper.c`
- Modify: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdspController.c`
- Modify: `libjamesdsp/subtree/Main/libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h`
- Modify: `libjamesdsp/tests/liveprog_publication_test.c`
- Modify: `libjamesdsp/tests/liveprog_publication_test.pro`

**Interfaces:**
- Consumes: `processing_pause()`/`processing_resume()`, `liveProgCurrent`, `liveProgRetired`, and the existing publication test hooks.
- Produces: an explicit reclamation contract in which callbacks load one generation, control code owns retired generations, and destruction waits for processing admission to close.

- [ ] **Step 1: Extend the failing test before changing implementation.**

  Add test phases for 30 seconds or 10,000 iterations (bounded by environment variables for sanitizers): concurrent `LiveProgStringParser`, `LiveProgSetVariable`, 44.1/48 kHz changes, `LiveProgEnable`/`LiveProgDisable`, and repeated `JamesDSPProcess`. Add at least three shutdown permutations: processing stopped before free, processing thread joining during free, and no-current-generation control calls. Assert no deadlock, no non-finite output, no stale-generation access, and bounded reload latency.

- [ ] **Step 2: Prove the extended test fails or exposes the missing coverage on the current implementation.**

  Run the normal test with the default long workload:

  ```bash
  timeout 180 /home/soloarch/Workspace/build/jamesdsp-luna-t13-stress-red/tests/liveprog_publication_test
  ```

  Expected: the pre-fix version either fails the new reclamation assertion, reports a race under TSan, or lacks the required shutdown evidence. Do not weaken the assertion to make the old code pass.

- [ ] **Step 3: Implement the smallest ownership correction.**

  Keep candidate compilation and VM destruction off the callback. Ensure `LiveProgSetVariable`, enable/disable, and sample-rate refresh either pause processing admission or publish a new state; they must never mutate a generation concurrently with `LiveProgProcess`. Ensure every early return resumes admission and that `LiveProgDestructor` handles embedded, current, and retired generations exactly once.

- [ ] **Step 4: Run normal, ASan/UBSan, and strict TSan stress tests.**

  Use separate build roots under `/home/soloarch/Workspace/build/jamesdsp-luna-t13-stress-{normal,asan,tsan}`. The ASan run must use `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`; the TSan run must use `setarch "$(uname -m)" -R env TSAN_OPTIONS=halt_on_error=1`. Any sanitizer diagnostic, deadlock timeout, or callback-latency regression blocks completion.

- [ ] **Step 5: Run existing Liveprog regressions against the same native archive.**

  Pass criteria: `liveprog_runtime_test`, `native_rate_refresh_cli_test`, `rate_transition_test`, `liveprog_callback_probe_test`, and `liveprog_reload_lock_test` all pass; callback allocation count remains zero; `git diff --check` passes.

- [ ] **Step 6: Commit the implementation and evidence.**

  Commit source, tests, project files, and an append-only roadmap entry together:

  ```bash
  git add libjamesdsp docs/superpowers/plans/2026-09-15-jamesdsp-luna-roadmap.md
  git commit -m "test: complete Liveprog reclamation stress coverage"
  ```

## Task 3: Run the complete native/EEL acceptance matrix

**Files:**
- Read: `libjamesdsp/tests/eel_corpus_manifest.json`
- Read: `libjamesdsp/tests/run_eel_corpus.py`
- Read: `libjamesdsp/tests/compare_eel_manifest.py`
- Modify: roadmap only if evidence reveals a new defect.

**Interfaces:**
- Consumes: the T13-corrected native archive from Task 2.
- Produces: full corpus evidence proving that publication changes preserve EEL behavior, rate transitions, non-finite handling, and Airwindows fixtures.

- [ ] **Step 1: Build a clean ASan/UBSan native library from the current source.**

  Use `qmake6 libjamesdsp/libjamesdsp.pro 'CONFIG += CI HEADLESS DEBUG_ASAN sanitize_undefined'` in a fresh build root and build with `make -B -j2`.

- [ ] **Step 2: Run the 50-entry EEL corpus.**

  Run `python3 -u libjamesdsp/tests/run_eel_corpus.py --inventory <inventory> libjamesdsp/tests/eel_corpus_manifest.json <sanitized-liveprog-runtime-test>` with a 300-second timeout. Expected: all 48 valid scripts pass and the two documented unsupported scripts are rejected for their expected reasons.

- [ ] **Step 3: Run native effect/rate regressions.**

  Run `asrc_capacity_test`, `rate_transition_test`, `native_rate_refresh_cli_test`, `process_allocation_test`, `crossfeed_lifecycle_test`, `liveprog_callback_probe_test`, `liveprog_reload_lock_test`, and `liveprog_publication_test` under the appropriate sanitizer builds.

- [ ] **Step 4: Record any behavior change explicitly.**

  If a corpus result changes, stop promotion, identify whether it is a real regression or an expected ownership behavior, and add a focused test before changing the manifest.

## Task 4: Re-run the isolated stereo PipeWire backend acceptance

**Files:**
- Read: `/home/soloarch/Workspace/build/jdsp-t13-fixture-test/pipewire-null.conf`
- Read: `/home/soloarch/Workspace/build/jdsp-t13-fixture-test/wp/main.lua.d/90-disable-host-monitors.lua`
- Modify: roadmap only; keep fixture files out of the source repository unless a later task deliberately productizes them.

**Interfaces:**
- Consumes: the backend-linked PipeWire binary and corrected native library from Tasks 2–3.
- Produces: isolated stereo processing, 44.1/48 kHz transition, and bounded shutdown evidence with no system graph contact.

- [ ] **Step 1: Start only private PipeWire/WirePlumber state.**

  Use a short `XDG_RUNTIME_DIR` under `build/`, the custom null-sink configuration, disabled host monitors, and `dbus-run-session`. Verify `pw-cli` lists `playback_FL`, `playback_FR`, `monitor_FL`, and `monitor_FR` on `output.t13-null`.

- [ ] **Step 2: Run the backend-linked JamesDSP target.**

  Start the freshly built headless PipeWire target with `AudioOutputDevice=output.t13-null`, feed 44.1 kHz audio into `jamesdsp_sink`, then replace it with 48 kHz audio. Capture `pw-cli ls Link` snapshots before/after and retain the application log.

- [ ] **Step 3: Assert the acceptance conditions.**

  Require stereo filter-to-output links at both rates, no `link ... failed`, `unknown resource`, or `Remote error` diagnostics, and clean disappearance of private nodes after the deliberate timeout. Verify the system `pactl info` and default sink are unchanged afterward.

- [ ] **Step 4: Repeat with the PulseAudio/GStreamer-linked build where feasible.**

  Use only the private disposable environment; do not point the test at the installed PipeWire PulseAudio server. If a private PulseAudio daemon cannot be created on this host, record the limitation and retain the successful GStreamer link/wrapper evidence.

## Task 5: Reconcile roadmap statuses and unblock dependent work

**Files:**
- Modify: `docs/superpowers/plans/2026-09-15-jamesdsp-luna-roadmap.md`
- Modify: `docs/superpowers/plans/2026-09-21-jamesdsp-next-phase.md` ledger

**Interfaces:**
- Consumes: Tasks 2–4 evidence.
- Produces: an accurate board with no historical status contradiction.

- [ ] **Step 1: Promote T13 only if every acceptance criterion is evidenced.**

  T13 may become `VERIFIED` only after long-lived reclamation stress, native corpus, isolated stereo backend transition, and shutdown evidence all pass. Otherwise leave it `IN_PROGRESS` and name the smallest remaining blocker.

- [ ] **Step 2: Reconcile T09/T14 and downstream dependency labels.**

  Do not mark T17/T18/T20 ready merely because one T13 test passes. Update the final board entry, not historical entries, and explain any task whose older log says `BLOCKED` while later evidence makes it ready.

## Task 6: Run full repository contract and GUI/editor acceptance

**Files:**
- Read/run: `meta/tests/workflow_contract_test.py`
- Read/run: `meta/tests/packaging_contract_test.sh`
- Read/run: `src/tests/*.pro` targets for editor, preset, downloader, config, and UI integration.
- Modify: only focused source/tests if a reproducible defect is found.

**Interfaces:**
- Consumes: a T13-verified native library and current source tree.
- Produces: green repository contracts and documented remaining UI integration gaps.

- [ ] **Step 1: Run repository contracts and syntax checks.**

  Run workflow, packaging, PipeWire RT, PulseAudio wrapper, visual-theme, shell syntax, Python compilation, and both repository `git diff --check` commands.

- [ ] **Step 2: Rebuild and run focused Qt tests under ASan/UBSan/offscreen.**

  Cover preset file/manager, file selection, DSP config, EQ validation, Liveprog editor bridge, downloader cancellation, and package manager shutdown. Use separate build roots and a timeout for every target.

- [ ] **Step 3: Exercise the remaining MainWindow integration gaps.**

  Add or run an offscreen fixture that proves config load → IPC → MainWindow status → native host state for Liveprog enable/file/error transitions. Preserve persisted configuration across construction and teardown; do not use a test that mutates state after construction without proving the signal route.

## Task 7: Validate Flatpak and DEB artifacts without changing the installed app

**Files:**
- Read/run: `meta/flatpak/build-local-bundle.sh`
- Read/run: `meta/flatpak/LOCAL_BUILD.md`
- Read/run: `meta/build_deb_package.sh`
- Modify: packaging scripts only if a contract test and a real isolated build demonstrate a defect.

**Interfaces:**
- Consumes: verified source branch and clean build root.
- Produces: reproducible Flatpak bundle and DEB artifacts with recorded binary identity.

- [ ] **Step 1: Build the local Flatpak bundle in `/home/soloarch/Workspace/build/jamesdsp/`.**

  Confirm the custom `custom-liveprog` workflow and filesystem permission scope remain unchanged. Record the exact bundle path and source commit.

- [ ] **Step 2: Build the DEB package in an isolated staging directory.**

  Verify package metadata, installed files, dependency declarations, and no collision with the standard Flathub installation.

- [ ] **Step 3: Verify artifact identity.**

  Compare `--version`, embedded commit/version output, linked libraries, and checksums against the source/build commit. A successful package build with the wrong binary is a failure.

## Task 8: Perform disposable installed-runtime checks

**Files:**
- Modify: no source files unless a runtime defect is reproduced and traced.
- Create: logs and manifests only under `/home/soloarch/Workspace/build/jamesdsp-release-gate-<date>/`.

**Interfaces:**
- Consumes: Task 7 Flatpak/DEB artifacts.
- Produces: evidence that the actual packaged application starts, loads configuration, connects to the intended backend, and shuts down cleanly.

- [ ] **Step 1: Install/test only in an isolated user or temporary prefix.**

  Do not replace the standard installation or restart the currently running app. Record package path, executable path, branch, and runtime environment.

- [ ] **Step 2: Verify startup and backend selection.**

  Confirm the process uses the artifact under test, creates the expected private audio nodes, and reads only the intended configuration paths.

- [ ] **Step 3: Verify shutdown, reload, and configuration persistence.**

  Exercise normal exit, timeout exit, Liveprog reload, rate transition, malformed-source preservation, and restart persistence. Retain logs and exit codes.

## Task 9: Final release gate and publication

**Files:**
- Modify: roadmap and this plan ledger.
- Create: a signed or annotated Git tag only after all gates pass.

**Interfaces:**
- Consumes: Tasks 1–8 evidence.
- Produces: one publishable `master` commit and a release tag; no extra long-lived branch.

- [ ] **Step 1: Run the complete final command set from a fresh build root.**

  Include native corpus, sanitizer tests, backend fixtures, workflow/packaging contracts, Flatpak/DEB builds, and disposable runtime checks.

- [ ] **Step 2: Review the diff as a release reviewer.**

  Confirm no secrets, generated artifacts, temporary configs, broad permissions, unrelated formatting, or stale branch references entered the repository. Run `git diff --check`, status, and `git diff --stat`.

- [ ] **Step 3: Push the single branch and create a tag only after explicit evidence.**

  ```bash
  git push fork HEAD:master
  git tag -a v<verified-version> -m "JamesDSP4Linux verified release"
  git push fork v<verified-version>
  ```

  Do not create a release tag while T13 or any runtime/package gate remains open.

