#!/bin/sh
# CONTRXL 25 - MPC Sample edition. (c) 2026 ILLAHTAINMENT. All rights reserved.
# Started by the Illah Choice menu from <card>/mods/CONTRXL25 (firmware 1.4, Developer Mode).
# The card is mounted noexec, so the program is copied into RAM (/run) and started from there.
# It runs in the background; the menu waits for "contrxl25" (watch.txt) to quit, then comes back.
MOD=$(pwd -P)
case "$MOD" in
    */mods/*) CARD=${MOD%%/mods/*} ;;
    *) CARD=$(cd "$MOD/.." && pwd -P) ;;
esac
RUN=/run/contrxl25
mkdir -p "$RUN" || exit 1
cp "$MOD/contrxl25" "$RUN/contrxl25.new" && chmod 700 "$RUN/contrxl25.new" && mv -f "$RUN/contrxl25.new" "$RUN/contrxl25" || exit 1
cd "$RUN" || exit 1
"$RUN/contrxl25" --card "$CARD" > "$RUN/contrxl25.out" 2>&1 &
exit 0
