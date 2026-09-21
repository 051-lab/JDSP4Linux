# Testing and audio-state ownership

This document records the current boundary for the Luna regressions. It is a
design and verification record, not a claim that arbitrary EEL code is hard
real-time safe.

## PipeWire format changes

`PwPluginBase::on_process` only observes the PipeWire rate/quantum, publishes
them through atomics, marks the format unavailable, and emits a dispatcher
notification. `apply_pending_format` runs on the dispatcher thread: it
deactivates the filter, resizes the dummy buffers, updates the rate and
quantum, calls the plugin setup, then publishes `format_ready` and
reactivates the filter. While preparation is incomplete, the callback copies
input to output and returns.

The callback must not call `setup`, resize/reserve a container, read a file,
compile EEL, or destroy an old DSP candidate. This is enforced by the current
PipeWire source contract test; runtime scheduler and allocation instrumentation
remain required before calling the boundary hard real-time proven.

Native format wrappers follow the same rule: `ensure_processing_capacity` only
checks the capacity prepared by `JamesDSPReallocateBlock`; it never grows or
refreshes effect state from the processing entry point. A successful
`JamesDSPReallocateBlock` commits the new runtime block size and refreshes
block-size-dependent state on the control/setup thread; an allocation failure
leaves the prior buffer and block size intact. An unexpected quantum therefore
takes the bounded wrapper silence/bypass path, while the control/setup thread
must prepare a larger capacity before normal processing resumes. The sanitizer
`process_allocation_test` covers injected allocation failure, the no-refresh
smaller-quantum path, successful control-side preparation, and an unexpected
4096-frame block with zero callback allocations.

## LiveProg candidate ownership

The native parser currently constructs a candidate VM and code handles before
publishing it under the JamesDSP mutex. A successful publication retires the
previous VM after the publication point; a failed candidate is destroyed and
the last valid VM remains active. The current implementation still performs
compilation and retirement synchronously in the host reload path. A future
single compile worker must serialize access to EEL compiler globals, publish a
generation/rate-matched candidate at a block boundary, and reclaim retired
states only after audio readers have left the block.

Until that worker/reclamation design is implemented and instrumented, reload
latency and old-state destruction are not represented as hard real-time safe.
The native T04/T09 regressions prove transaction and rate invariants only.

## LiveProg sample-rate transitions

`JamesDSPSetSampleRate` distinguishes the device-facing `trueSampleRate` from
the effective native `fs`. A supported rate change updates the VM's `srate` to
that effective `fs`, then reruns the active program's `@init`, reapplies the
latest values supplied through `LiveProgSetVariable`, and runs `@slider`, while
processing admission is paused and the DSP mutex is held where required. The
admission gate counts active processing readers; the control thread closes
admission, waits for existing readers to leave, performs buffer/ASRC/rate/effect
replacement, then reopens admission. It never makes the callback wait on a
control mutex: a callback that arrives while admission is closed emits bounded
silence. Processing therefore cannot observe a partially refreshed state.
Initialization-derived and slider-derived coefficients are refreshed before
processing resumes. Program state intentionally initialized by `@init` (for
example delay/filter history) is reset; host-controlled slider values survive. A
96 kHz device rate may use an internal 48 kHz `fs`, and EEL must observe 48
kHz in that case.

The native `rate_transition_test` asserts this policy at 44.1 and 48 kHz,
checks preserved host parameter values and the sample generated from the
refreshed coefficient, and exercises forced effect refresh with crossfeed
enabled and disabled. Transition refresh work remains control-path work under
the control path; callback latency and moving expensive preparation/reclamation
off the control-side quiescence window remain T13 acceptance work.

## Airwindows EEL port checks

`awBaxandall.eel` and `awInterstage.eel` are treated as maintained behavioral
ports, not as numerically equivalent implementations of upstream Airwindows.
No pinned upstream reference binary, matching host/settings harness, or
parity claim is part of this suite. The native corpus runner executes each
port with silence, left/right impulses, deterministic sine, step and seeded
noise; tests every inventoried control at its declared default, minimum and
maximum; processes block lengths 1, 2, 7, 32 and 77; and reloads at 44.1 and
48 kHz. The native regression checks the pre-sanitization non-finite sample
counter is unchanged around each corpus member's complete exercise. The
acceptance tolerance for sanitized outputs is deliberately a safety bound
rather than an acoustic-quality metric: samples must be finite and have
absolute magnitude below `1e6`. This catches invalid/unbounded behavior
without implying tonal parity or subjective sound quality. The corpus runner
applies a 120-second timeout per script and checks each expected native
compile outcome.

## Evidence convention

All out-of-source builds belong under `/home/soloarch/Workspace/build/`.
Sanitizer test commands must provide sanitizer runtime flags at link time when
the qmake target does not propagate them. Offline tests do not authorize
installation, publication, Flatpak permission changes, or restarting a live
audio application.
