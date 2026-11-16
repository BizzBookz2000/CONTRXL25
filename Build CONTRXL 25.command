#!/bin/bash
# Double-click in Finder to build CONTRXL 25 (opens Terminal and runs build_mac.sh).
cd "$(dirname "$0")"
bash ./build_mac.sh
echo
read -n 1 -s -r -p "Press any key to close this window..."
