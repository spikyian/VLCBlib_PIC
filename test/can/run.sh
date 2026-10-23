#!/bin/bash
# Host tests for the CAN drivers: builds every node variant and runs bus.c.
#   test/can/run.sh [VLCB-defs directory]
# Needs gcc, python3, git and a POSIX shell (Linux, macOS, WSL). Nothing here is built for the PIC.
# The "legacy" nodes are the drivers as they were before the shared CAN service,
# taken from git at CAN_LEGACY_REV (default below), to compare against.
set -e
T="$(cd "$(dirname "$0")" && pwd)"
LIB="$(cd "$T/../.." && pwd)"
DEFS="$(cd "${1:-$LIB/../VLCB-defs}" && pwd)"
[ -f "$DEFS/vlcbdefs_enums.h" ] || { echo "vlcbdefs_enums.h not found in $DEFS"; exit 1; }
B="$T/build"; rm -rf "$B"; mkdir -p "$B/legacy" "$B/defs"
tr -d '\r' < "$DEFS/vlcbdefs_enums.h" > "$B/defs/vlcbdefs_enums.h"
python3 "$T/make_legacy.py" "$LIB" "$B/legacy" "${CAN_LEGACY_REV:-5366386}"
CF="-std=gnu11 -O1 -g -fPIC -shared -Wl,-Bsymbolic -Wall -Wno-unused-function -Wno-unknown-pragmas -Wno-unused-variable -Wno-unused-but-set-variable -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Werror=implicit-function-declaration -Wno-missing-braces -Wno-parentheses"
build() {   # name family mode options...
  local name=$1 fam=$2 mode=$3; shift 3
  local drv famdef inc=""
  if [ "$fam" = q83 ]; then drv=can18_can_2.c; famdef=-D_18FXXQ83_FAMILY_; else drv=can18_ecan.c; famdef=-D_18F66K80_FAMILY_; fi
  if [ "$mode" = legacy ]; then inc="-I$B/legacy"; set -- "$@" -DCAN_TEST_LEGACY; else
    if [ "$fam" = q83 ]; then set -- "$@" "-DCAN1_BUFFERS_BASE_ADDRESS=((uintptr_t)q83_msgram)" "-DCAN_HW_SYNC()=q83_sync()"
    else set -- "$@" "-DCAN_HW_SYNC()=k80_sync()"; fi
  fi
  gcc $CF $famdef -I"$T/$fam" -I"$T" $inc -I"$LIB" -I"$B/defs" -DDRIVER="\"$drv\"" "$@" \
      -include "$T/$fam/msgram.h" "$T/node.c" "$T/$fam/model.c" "$LIB/messageQueue.c" -o "$B/$name.so"
}
for fam in q83 k80; do
  for mode in legacy split; do
    build ${fam}_${mode}            $fam $mode
    build ${fam}_${mode}_chk        $fam $mode -DCAN_ADDITIONAL_CANID_CHECKS
    build ${fam}_${mode}_coe        $fam $mode -DCONSUMED_EVENTS
    build ${fam}_${mode}_chk_coe    $fam $mode -DCAN_ADDITIONAL_CANID_CHECKS -DCONSUMED_EVENTS
  done
done
gcc -O1 -g -Wall -I"$T" "$T/bus.c" -o "$B/bus" -ldl
gcc -std=gnu11 -O0 -w -D_18F66K80_FAMILY_ -I"$T/k80hook" -I"$T" -I"$LIB" -I"$B/defs" -DDRIVER='"can18_ecan.c"' \
    "-DCAN_HW_SYNC()=k80_sync()" "$T/k80_tx_error_window.c" "$T/k80/model.c" "$LIB/messageQueue.c" -o "$B/k80_tx_error_window"
"$B/k80_tx_error_window"
cd "$T" && "$B/bus" "$B"
echo "== again, with the K80 high-water mark at one free buffer"
K80_WM_FREE=1 "$B/bus" "$B" | grep -E "^k80|^q83_split <|checks"
