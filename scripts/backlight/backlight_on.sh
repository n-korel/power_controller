#!/usr/bin/env bash
# Turn on backlight (POWER_CTRL bit2). Use --with-display for bench with display.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/bl.sh" on "$@"
