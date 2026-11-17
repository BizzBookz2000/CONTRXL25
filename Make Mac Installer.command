#!/bin/bash
# Double-click in Finder to build the Mac installer (.dmg)
cd "$(dirname "$0")" && bash make_installer_mac.sh "$@"
echo; read -r -p "  Press Return to close. " _
