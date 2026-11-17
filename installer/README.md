# Installers (setup programs)

Two scripts turn the app into the kind of setup you get with commercial software.
Run each one on its own computer: the Windows setup is made on a PC, the Mac installer on a Mac.

| | Windows | Mac |
|---|---|---|
| Run | double-click `make_installer_windows.bat` | double-click `Make Mac Installer.command` (or `bash make_installer_mac.sh`) |
| You get | `installers\CONTRXL25-<version>-Windows-Setup.exe` | `installers/CONTRXL25-<version>-Mac.dmg` |
| First time | builds the app, downloads Inno Setup (free) | builds the app (uses the tools that come with macOS) |
| Takes | about 1 minute (plus the app build the first time) | about 1 minute (plus the app build the first time) |

Both scripts build the app first if it hasn't been built yet. Add `rebuild` to force a fresh build
(`make_installer_windows.bat rebuild`, `bash make_installer_mac.sh rebuild`). The version comes from `CMakeLists.txt`.

## What the person installing sees

**Windows** (`...-Windows-Setup.exe`)
1. Install for all users, or for me only (no administrator password needed)
2. Welcome
3. License agreement (must accept)
4. Install folder (default `C:\Program Files\Illahtainment\CONTRXL 25`)
5. Start Menu folder
6. Create a desktop shortcut (ticked)
7. Ready to install, then Install
8. Finish, with *Launch CONTRXL 25* ticked

It adds CONTRXL 25, its User's Manual and an Uninstall entry to the Start Menu, and an entry in
**Settings > Apps** with the version and publisher. Running a newer setup over an older version updates it in place.
Uninstalling removes the program and asks whether to remove settings, MIDI links and the plug-in list.
The sample library and songs are always kept.

**Mac** (`...-Mac.dmg`) opens a window with:
- **Install CONTRXL 25.pkg**: Introduction, License (Agree), Installation Type, Install, Summary. It installs
  `CONTRXL 25.app` into **Applications** (Intel and Apple silicon, macOS 10.13 or newer).
- **CONTRXL 25 User's Manual.pdf**
- **Uninstall CONTRXL 25.command**: removes the app and, if you say so, the settings. The library is kept.
- **License.txt**

## Giving the installer to other people: the "unknown publisher" warnings

Without a code-signing certificate, both systems warn people the first time:

- **Windows**: "Windows protected your PC". Click **More info**, then **Run anyway**.
- **Mac**: "Install CONTRXL 25.pkg can't be opened because Apple cannot check it". Right-click the .pkg and choose
  **Open**, or on macOS 15 and later go to **System Settings > Privacy & Security** and click **Open Anyway**.

On your own computers there is no warning, because the files never came from the internet.

To remove the warnings for everyone, as commercial software does:

- **Windows**: buy a code-signing certificate (an OV or EV certificate from a certificate authority, typically
  $200 to $400 a year). Before running `make_installer_windows.bat`, set
  `set CONTRXL_PFX=C:\path\to\certificate.pfx` and `set CONTRXL_PFX_PASS=<password>`.
  The script signs the app, the plug-in scanner and the setup.
- **Mac**: join the Apple Developer Program ($99 a year). In Xcode or on developer.apple.com create a
  *Developer ID Application* and a *Developer ID Installer* certificate. Store notarization credentials once with
  `xcrun notarytool store-credentials contrxl`. Then, before running the script:
  ```
  export DEVELOPER_ID_APP="Developer ID Application: Your Name (TEAMID)"
  export DEVELOPER_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
  export NOTARY_PROFILE="contrxl"
  ```
  The script then signs the app with the hardened runtime (`installer/mac/entitlements.plist`: microphone,
  third-party plug-ins), signs the installer, has Apple notarize it and staples the result to the .pkg and the .dmg.

## Before you sell or publish it

- `installer/common/EULA.txt` is a plain starting point written for this app, not legal advice. Have it reviewed,
  and change the company name or terms if needed. Both installers show this file.
- JUCE is free for personal projects and small companies under its own licence, with paid tiers above a
  revenue limit. Check https://juce.com/get-juce before selling the app.
- The installer artwork lives in `installer/windows/` (icon, wizard images) and `installer/mac/resources/`
  (background, welcome and conclusion pages). Replace them to change the look.

## Files

```
make_installer_windows.bat        build the app + Windows setup
make_installer_mac.sh             build the app + Mac installer (.pkg inside a .dmg)
Make Mac Installer.command        double-click version of make_installer_mac.sh
installer/common/EULA.txt         the license agreement both installers show
installer/windows/CONTRXL25.iss   Inno Setup script (pages, files, shortcuts, uninstaller)
installer/windows/*.bmp, icon.ico setup icon and wizard images
installer/mac/distribution.xml    installer pages and requirements
installer/mac/resources/          welcome / conclusion pages, background
installer/mac/entitlements.plist  hardened-runtime permissions (Developer ID signing only)
installer/mac/Uninstall CONTRXL 25.command
docs/CONTRXL 25 User's Manual.pdf included in both installers
```
