# Linux LiveProg Four-Stage Implementation Plan

1. Update the native `LiveProg` state to hold slider and block code handles,
   `samplesblock`, and lifecycle-state cleanup helpers.
2. Replace substring-based parsing with line-aware section extraction and
   explicit diagnostics.
3. Implement candidate-VM compilation and atomic state publication.
4. Update the Linux host reload path to preserve the active program on failed
   compilation and consume copied diagnostics.
5. Update native callers and headers for the parser API change.
6. Add focused native/runtime regression coverage before relying on the full
   application build.
7. Build the existing Linux target, run the focused tests, run `git diff
   --check`, and inspect the final diff for unrelated changes.
