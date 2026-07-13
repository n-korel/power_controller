#!/usr/bin/env bash
# Turn off backlight only (SCALER/LCD unchanged).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/bl.sh" off "$@"
