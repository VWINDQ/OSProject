#!/usr/bin/env bash
# Run a command in the gcc:14-bookworm container against this working tree.
# Usage: bash scripts/dk.sh make test
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v cygpath >/dev/null 2>&1; then
    repo="$(cygpath -m "${repo}")"
fi

MSYS_NO_PATHCONV=1 exec docker run --rm -v "${repo}:/work" -w /work gcc:14-bookworm "$@"
