#!/bin/bash
# Removes CONTRXL 25 from Applications. Your songs and sample library are never touched.
clear
echo "=========================================================="
echo "  Uninstall CONTRXL 25"
echo "=========================================================="
APP="/Applications/CONTRXL 25.app"
if [ ! -d "$APP" ]; then echo; echo "  CONTRXL 25 is not in Applications - nothing to remove."; echo; read -r -p "  Press Return to close. " _; exit 0; fi
echo
read -r -p "  Remove CONTRXL 25 from Applications? (y/n) " a
case "$a" in y|Y|yes|YES) ;; *) echo "  Nothing removed."; exit 0;; esac
osascript -e 'quit app "CONTRXL 25"' >/dev/null 2>&1; sleep 1
if ! rm -rf "$APP" 2>/dev/null; then
    echo "  Your Mac password is needed to remove it from Applications:"
    sudo rm -rf "$APP" || { echo "  Could not remove it."; exit 1; }
fi
sudo pkgutil --forget com.illahtainment.contrxl25 >/dev/null 2>&1
echo "  CONTRXL 25 removed."
DATA="$HOME/Library/Application Support/CONTRXL25"
if [ -d "$DATA" ]; then
    echo
    read -r -p "  Also remove your settings, MIDI links and plug-in list? Your sample library is kept. (y/n) " b
    case "$b" in y|Y|yes|YES) find "$DATA" -maxdepth 1 -type f -delete; echo "  Settings removed.";; *) echo "  Settings kept.";; esac
fi
echo; read -r -p "  Done. Press Return to close. " _
