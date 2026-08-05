#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."

BUILD=${BUILD:-build}
export PATH="$PATH:$HOME/.npm-global/bin"
. test/hostenv.sh
export BUILD

exec python3 test/run_cases.py "$@"
