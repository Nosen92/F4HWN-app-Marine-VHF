#!/usr/bin/env bash
# Build one overlay-app blob (.app). Invoked by ../../../compile-app.sh inside
# the uvk1-uvk5v3 Docker image, run from this app's directory, or directly
# (cd App/apps/marine && ./build.sh) with arm-none-eabi-gcc and python3 on PATH.
# The source file is named after the directory (marine -> marine_app.c), and
# the .app after APP_NAME (spaces stripped): MarineVHF.app.
#
# Options, passed through the environment:
#   MARINE_TX=0           receive-only build (PTT is refused)
#   MARINE_VDES_VOICE=1   also offer 24-26 / 84-86 as analogue duplex voice
#   MARINE_LOW_15_17=1    also hold channels 15 and 17 at low power
set -euo pipefail

APP="$(basename "$PWD")"            # marine
APP_NAME="Marine VHF"               # must match APP_TITLE in marine_app.c (self hand-off)
APP_VER="1.3"
APP_API_MIN=4                       # tx_prepare, handoff, vfo_stage (3), tx_power (4)
APP_VMA=${APP_VMA:-0x20000280}      # pinned overlay VMA (Core/py32f071xb.ld)
OUT="${APP_NAME// /}"               # MarineVHF

CC=/opt/toolchain/bin/arm-none-eabi-gcc
OBJCOPY=/opt/toolchain/bin/arm-none-eabi-objcopy
command -v arm-none-eabi-gcc >/dev/null 2>&1 && { CC=arm-none-eabi-gcc; OBJCOPY=arm-none-eabi-objcopy; }

OPTS="-DMARINE_TX=${MARINE_TX:-1} -DMARINE_VDES_VOICE=${MARINE_VDES_VOICE:-0} \
  -DMARINE_LOW_15_17=${MARINE_LOW_15_17:-0}"
CFLAGS="-mcpu=cortex-m0plus -mthumb -Os -std=gnu11 -ffreestanding -fno-builtin -fno-common \
  -fomit-frame-pointer -ffunction-sections -fdata-sections -Wall -Wextra ${OPTS}"
LDFLAGS="-nostdlib -nostartfiles -T app.ld -Wl,--defsym,APP_VMA=${APP_VMA} \
  -Wl,--gc-sections -Wl,-Map=${APP}.map -Wl,--build-id=none -Wl,--no-warn-rwx-segments"

rm -f ./*.app ./*.elf ./*.bin        # drop stale artifacts so discovery is unambiguous

step() { printf '\r  🔨 %-13s [%d/4] %-8s' "$APP_NAME" "$1" "$2"; }
trap 'printf "\r  ❌ %-13s build failed            \n" "$APP_NAME"' ERR

# The big digits (digits.txt) go to the assets, outside the 4 KiB of code.
# --require beam2: beam_get and vfo_stage come with the firmware's BEAM bridge.
step 1 assets  ; python3 ./gen_assets.py "${APP}_assets.bin"
step 2 compile ; "$CC" $CFLAGS $LDFLAGS "${APP}_app.c" -lgcc -o "${APP}.elf"
step 3 objcopy ; "$OBJCOPY" -O binary "${APP}.elf" "${APP}.bin"
step 4 pack    ; python3 ../pack_app.py "${APP}.bin" "${OUT}.app" \
                   --name "$APP_NAME" --ver "$APP_VER" --api-min "$APP_API_MIN" --vma "${APP_VMA}" \
                   --require beam2 --assets "${APP}_assets.bin" >/dev/null
trap - ERR

BYTES=$(wc -c < "${APP}.bin")
if [ "$BYTES" -gt 4096 ]; then
  printf '\r  🚨 %-13s OVERFLOWS 4 KiB (%d B)     \n' "$APP_NAME" "$BYTES"; exit 1
fi
printf '\r  ✅ %-13s %4d B  (%d%% of 4 KiB)  ->  %s.app   \n' \
       "$APP_NAME" "$BYTES" "$(( BYTES * 100 / 4096 ))" "$OUT"
