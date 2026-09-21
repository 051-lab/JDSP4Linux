#!/usr/bin/env python3
"""Validate the local CI workflow's revision and native-test contracts."""

from pathlib import Path


workflow = Path(__file__).parents[2] / ".github/workflows/package-deb.yml"
text = workflow.read_text(encoding="utf-8")

assert "  push:" in text
assert "    branches: [ master ]" in text
assert "  pull_request:" in text
assert "  workflow_dispatch:" in text
assert "  release:" in text
assert "     - published" in text
assert "     - prereleased" in text
assert text.count("ref: ${{ github.sha }}") >= 3
assert "if: github.event_name == 'push' && github.ref == 'refs/heads/master'" in text
assert "if: github.event_name == 'release' || github.event_name == 'workflow_dispatch'" in text
assert "uses: actions/checkout@v4" in text
assert "git rev-parse HEAD" in text
assert "git submodule status --recursive" in text
assert "qmake6 libjamesdsp/tests/tests.pro" in text
assert "qmake6 libjamesdsp/tests/asrc_capacity_test.pro" in text
assert "qmake6 libjamesdsp/tests/rate_transition_test.pro" in text
assert "asrc_capacity_test" in text
assert "liveprog_runtime_test" in text
assert "rate_transition_test" in text
assert "crossfeed_lifecycle_test" in text
assert "run_eel_corpus.py --skip-external" in text
assert "compare_eel_manifest.py" in text
assert "process_allocation_test" in text
assert "qmake6 libjamesdsp/tests/liveprog_reload_lock_test.pro" in text
assert "liveprog_reload_lock_test" in text
assert "eel_parser_test" in text
assert "codeeditor_sync_test" in text
assert "dsp_host_reload_test" in text
assert 'DSP_LIB_DIR="$GITHUB_WORKSPACE/build/jamesdsp/libjamesdsp"' in text
assert 'DSP_LIB_DIR="$GITHUB_WORKSPACE/build/jamesdsp-asan/libjamesdsp"' in text
assert "preset_manager_test" in text
assert "libglib2.0-dev" in text
assert "DEFINES+=JDSP_TEST_HOOKS" in text
assert "'CONFIG += DEBUG_ASAN'" in text
assert "LFLAGS='-fsanitize=address -fsanitize=undefined'" not in text
for test_project in (
    "libjamesdsp/tests/tests.pro",
    "libjamesdsp/tests/rate_transition_test.pro",
    "libjamesdsp/tests/liveprog_reload_lock_test.pro",
):
    project_text = (Path(__file__).parents[2] / test_project).read_text(encoding="utf-8")
    assert "sanitize_address" in project_text, test_project
    assert "sanitize_undefined" in project_text, test_project
assert "verify-native-sanitizers" in text
assert "Record sanitizer source identity" in text
assert "github_sha=%s" in text
assert "qmake_config=CONFIG+=DEBUG_ASAN DEFINES+=JDSP_TEST_HOOKS" in text
assert "sanitizer-verification-identity-${{ github.sha }}" in text
assert "make -C build/ci-pulse-${{ matrix.flavor }} -j1" in text
assert "make -C build/ci-pipewire-${{ matrix.flavor }} -j1" in text
assert "path: build/ci-pulse-${{ matrix.flavor }}/src/jamesdsp" in text
assert "path: build/ci-pipewire-${{ matrix.flavor }}/src/jamesdsp" in text
assert "qmake6 JDSP4Linux.pro \"CONFIG += CI\" \"CONFIG += USE_PULSEAUDIO\" -o build/ci-pulse-${{ matrix.flavor }}/Makefile" in text
assert "qmake6 JDSP4Linux.pro \"CONFIG += CI\" -o build/ci-pipewire-${{ matrix.flavor }}/Makefile" in text
assert "GITHUB_OUTPUT" in text
assert "::set-output" not in text
print("workflow contract test passed")
