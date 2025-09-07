# Scoop Auto Updater

A lightweight Windows tray app that quietly keeps your Scoop packages up to date on a schedule — with per-app selection, autostart, and one-click cleanup.

---

## Features

- **Automatic Updates**: Periodically checks for updates to Scoop apps.
- **Manual Update**: Trigger checks/updates instantly from the tray.
- **Clean Cache**: Clear Scoop cache and unused files in one click.
- **Autostart**: Optional autostart on Windows login.
- **Custom Update Strategy**: Update **all** apps or only a selected subset.
- **Tabbed Settings**:
    - **General** — first delay, interval, autostart.
    - **Update Policy** — choose specific apps to update with search & checkboxes.


## Requirements

- **Windows 7 or later**
- **Scoop**


## Build

Supports **MSVC** or **MinGW**, with **Ninja** or **Makefiles**.

#### MSVC + Ninja

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

#### MinGW + Ninja

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

#### MinGW Makefiles

```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

> `-DCMAKE_BUILD_TYPE=Release` enables optimization (`-O3`/`O2`) and `NDEBUG` on single-config generators. For multi-config, use `--config Release`.

## Usage

- **Tray Menu** (right-click):
    - **Check Now** — run an immediate check/update cycle
    - **Clean** — run `scoop cache rm *` and `scoop cleanup *`
    - **Settings** — open the settings dialog
    - **Exit** — quit the application
- **Double-click** the tray icon — triggers a check when idle.


## Configuration

Settings are stored at:

- `%LOCALAPPDATA%\ScoopAutoUpdater\config.ini`

## License

This project is licensed under the **GNU General Public License v3.0** — see the [LICENSE](LICENSE) file for details.
