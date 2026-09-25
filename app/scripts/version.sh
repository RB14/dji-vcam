#!/usr/bin/env bash
# Prints the full version of the checkout (docs/building.md "Versions and releases"):
#   0.1.0                       exactly the release tag v0.1.0, with no uncommitted changes
#   0.1.0-dev+g1a2b3c4          any other commit
#   0.1.0-dev+g1a2b3c4.dirty    with uncommitted changes
# The rule lives in cmake/DjiVcamVersion.cmake, which CMake builds use directly.
set -euo pipefail
exec cmake -P "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/cmake/DjiVcamVersion.cmake"
