#!/usr/bin/env bash
# Quick brightness set: brightness.sh 75  or  brightness.sh pwm 300
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/bl.sh" set "$@"
