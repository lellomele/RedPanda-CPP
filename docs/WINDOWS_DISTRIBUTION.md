# Windows installation and distribution

**[Release beta v3.5.0-beta.2-mele.1](https://github.com/lellomele/RedPanda-CPP/releases/tag/v3.5.0-beta.2-mele.1)** — Windows 10/11 x64.

| Package | Download |
| --- | --- |
| Windows x64 installer | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-mele.1/RedPandaCPP-3.5-beta2-rmele-Setup-x64.exe) |
| Portable ZIP | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-mele.1/RedPandaCPP-3.5-beta2-rmele-Portable-x64.zip) |
| Portable EXE | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-mele.1/RedPandaCPP-3.5-beta2-rmele-Portable-x64.exe) |
| Source code | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-mele.1/RedPandaCPP-3.5-beta2-rmele-Sources.zip) |
| SHA-256 checksums | [Download](https://github.com/lellomele/RedPanda-CPP/releases/download/v3.5.0-beta.2-mele.1/SHA256SUMS.txt) |

Downloadable package names use `rmele`. Only the GitHub attachments have been renamed: installation folders and credits inside the packages retain the names documented below.

[Italiano](DISTRIBUZIONE_WINDOWS_FORK.md) · [Features](FEATURES.md) · [README](../README.md)

## Choose a package

| Format | Installation | Preferences and recovery |
|---|---|---|
| Setup x64 EXE | Current user, normally under `%LOCALAPPDATA%/Programs/RedPandaCPP-Mele`; Start menu entry and optional desktop shortcut | User-profile application-data folder |
| Portable x64 ZIP | Extract the whole folder to a writable location | `app/config` inside the extracted package |
| Portable x64 EXE | Extracts the same portable layout; no installed-application registration or shortcuts | `app/config` inside the extracted package |

Use the Windows x64 packages on Windows 10/11. Qt 6.11.2 and the runtime dependencies are included. A compiler (GCC/Clang) and debugger (GDB/LLDB) are not bundled: configure them separately in the IDE's preferences.

### Launch and folder layout

```text
RedPandaCPP/
  RedPandaIDE.exe             Main launcher: start this file
  COPYRIGHT.txt
  LICENSE
  LEGGIMI.txt
  licenses/                  Third-party license notices
  app/
    RedPandaIDE-core.exe      Internal IDE
    Qt6*.dll                 Private runtime libraries
    qt.conf
    plugins/
    templates/
    config/                  Created for portable preferences/recovery
```

Keep the whole structure. The launcher forwards arguments and preserves the caller's environment/working directory, without adding private Qt DLLs to PATH. Its icon matches the core executable and installer. About includes **© 2006 Ing. prof. Raffaele Mele**, an Italian flag and the original project credits.

## Install, update and remove

The setup and portable extractor require an empty destination to avoid overwriting an existing installation or unrelated files. To update an installed copy, close the IDE and uninstall it first, then run the new setup. The uninstaller removes an explicit list of distributed files and only empty directories; it retains user-profile preferences and personal files.

For a portable update, close the old copy, preserve `app/config`, extract the new package into an empty writable folder and restore your configuration there. Always transfer the entire package when moving it to another machine. Configure toolchain paths again if that machine uses different compiler/debugger locations.

Packages are unsigned. A local antivirus scan is a check of those files at that time, not a guarantee about future security software decisions. The fork remains beta-derived; consult [compatibility and limits](FORK_CHANGES.md#compatibility-scope).

## Building distributions

Requirements: CMake/Ninja, MinGW-w64 GCC, Qt 6.11.2 (base, SVG, tools and translations), Python 3, NSIS and astyle. Packaging sources are in [`platform/windows/fork-packaging`](../platform/windows/fork-packaging).

1. Configure two IDE builds: `PORTABLE_CONFIG=oui` for portable and `PORTABLE_CONFIG=non` for installed; use `FILESYSTEM_LAYOUT=flat` and `MAKE_INTERFACE=mingw32` for both.
2. Install each build into `payload/portable/app` or `payload/installed/app`, add astyle and name the deployed backend `app/RedPandaIDE-core.exe`.
3. Compile `launcher.rc` with windres from the packaging source directory. Compile `launcher.cpp` with its resource object using `-std=c++17 -O2 -Wall -Wextra -Werror -static -municode -mwindows -lshell32`. Copy it as `payload/*/RedPandaIDE.exe`.
4. Deploy Qt with `windeployqt --release --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime --no-patchqt --plugindir <app>/plugins --dir <app> <app>/RedPandaIDE-core.exe`. Set `Prefix=.` and `Plugins=plugins` in `app/qt.conf`.
5. Run `complete-runtime.py` with payload root, Qt prefix, compiler prefix and both build directories. Its recursive PE-import check must report no missing DLLs; include the matching third-party license notices.
6. Generate a deletion manifest with `Delete` for each distributed file and nonrecursive `RMDir` entries for subfolders in reverse depth order. Exclude personal preferences/history/documents from both payloads.
7. Compile `distribution.nsi` with definitions `PAYLOAD`, `OUTPUT_FILE`, `APP_VERSION`, `APP_ICON` and `DELETE_MANIFEST`. Add `PORTABLE_PACKAGE` for the portable extractor. Also make a ZIP of the portable folder and an archive of the corresponding fork sources.

Verify launcher/core names, extraction, startup without a Qt SDK in PATH, settings separation and uninstallation with unrelated source files left in the target. The installer supports `/TESTMODE` for isolated extraction/uninstallation checks without real application registration or shortcuts. Such a test does not exercise every real registered-installation scenario.

References: [Qt Windows deployment](https://doc.qt.io/qt-6/windows-deployment.html), [NSIS](https://nsis.sourceforge.io/), [MSYS2 NSIS package](https://packages.msys2.org/packages/mingw-w64-x86_64-nsis).
