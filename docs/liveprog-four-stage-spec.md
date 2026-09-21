# Linux LiveProg Four-Stage Compatibility Specification

## Scope

Add host-side support for the EEL lifecycle sections `@init`, `@slider`,
`@block`, and `@sample` in the native Linux LiveProg engine. Preserve the
existing two-stage script behavior while making reloads transactional.

## Runtime contract

- `@init` runs once for a successfully loaded program.
- `@slider` runs after `@init` on initial load and after a host variable change.
- `@block` runs once at the beginning of each native stereo audio block.
- `@sample` runs once for each stereo frame.
- `srate`, `spl0`, and `spl1` remain host variables.
- `samplesblock` contains the current native block frame count during `@block`.
- `@sample` remains required for an executable LiveProg program.
- `@init`, `@slider`, and `@block` are optional, preserving legacy scripts.

## Parsing rules

Recognize section markers only when they occur at the beginning of a logical
line, allowing leading spaces or tabs. Markers may have a trailing comment,
but arbitrary text after a marker is malformed. CRLF input is supported.

Exactly one recognized section of each type is permitted. Duplicate sections
are rejected. Unsupported lifecycle markers are rejected with a diagnostic
rather than silently discarded. Text before the first recognized section is
treated as script metadata and is not compiled by LiveProg.

## Transactional loading

1. Split and copy the source sections without modifying the active VM.
2. Allocate a candidate VM and register all host variables.
3. Compile every present section, including `@sample`.
4. Execute candidate `@init`, then candidate `@slider`, only after all compile
   operations succeed.
5. Under the existing DSP mutex, replace the active `LiveProg` state and free
   the old state.
6. On any parse, allocation, compile, or initialization failure, free only the
   candidate and leave the active VM, variables, enabled state, and audio
   processing unchanged.

Compiler diagnostics must be copied to caller-owned storage before a failed
candidate VM is destroyed.

## Host integration

The Linux host must stop disabling LiveProg before attempting compilation. An
explicit user disable remains immediate; a source reload changes enabled state
only after successful loading. Existing UI compilation signals and variable
watch behavior must remain compatible with the revised parser API.

## Verification requirements

The test coverage must prove:

- legacy `@init`/`@sample` scripts still load and process;
- `@slider` executes on load and variable changes;
- `@block` executes exactly once per native block and sees `samplesblock`;
- sections work in any order and with CRLF input;
- duplicate and malformed sections fail clearly;
- invalid reload preserves the previous program and audible output;
- a later valid reload still succeeds after an invalid reload;
- non-finite sample protection remains intact.
