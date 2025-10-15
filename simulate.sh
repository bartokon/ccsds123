#!/bin/bash
set -euo pipefail

if [[ $# -gt 0 ]]; then
  echo "[simulate.sh] Positional arguments are no longer supported. Use the Makefile targets instead." >&2
fi

exec make hdl-sim
