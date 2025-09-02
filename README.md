# Scoop Auto Updater

**Scoop Auto Updater** is a tray application for automatically updating Scoop-managed applications. It periodically checks for updates and allows the user to manually trigger updates or clean the cache. The application runs in the background with an easy-to-use system tray interface.

## Features

- **Automatic Updates**: Periodically checks for updates to Scoop apps.
- **Manual Update**: Manually trigger updates via tray menu.
- **Clean Cache**: Allows cleaning up Scoop cache and unused files.
- **Autostart**: Can be configured to start automatically when Windows starts.
- **Custom Update Strategy**: Select specific apps to update instead of updating everything.
- **Tabbed Settings**: Settings are organized into two tabs:
    - **General**: Configure update delay and interval, autostart option.
    - **Update Policy**: Select specific apps to update or use the `*` wildcard.

## Installation

1. **Clone or Download** the repository.
2. **Build the Application**:
    ```
    windres scoop_auto_updater.rc -O coff -o scoop_auto_updater.res
    ```

    ### MinGW (GNU toolchain)
     - Ensure you have a working MinGW toolchain installed.
     - Run the following commands to compile:
       ```
       g++ scoop_auto_updater.cpp scoop_auto_updater.res -Ofast -municode -mwindows -lole32 -lshell32 -lgdi32 -lcomctl32 -luuid -lgdiplus -o "Scoop Auto Updater.exe"
       ```

     ### MSVC (Microsoft Visual C++)
     - Ensure you have MSVC installed (via Visual Studio or Build Tools).
     - Open the **Developer Command Prompt for Visual Studio**.
     - Run the following commands to compile:
       ```
       cl /EHsc scoop_auto_updater.cpp scoop_auto_updater.res /link ole32.lib shell32.lib gdi32.lib comctl32.lib uuid.lib gdiplus.lib /out:"Scoop Auto Updater.exe"
       ```

3. **Run the Application**: After building, you can run the executable `Scoop Auto Updater.exe` directly.

## Usage

- **Tray Menu**:
    - **Check Now**: Immediately checks for updates for all Scoop apps.
    - **Clean**: Cleans up unused files in Scoop's cache.
    - **Settings**: Opens the settings window to adjust configuration.
    - **Exit**: Closes the application.

- **Settings**:
    - **General Tab**: Set update interval and first check delay. Toggle autostart on/off.
    - **Update Policy Tab**: Choose whether to update all apps or only selected apps. Filter apps with a search bar, and select which apps to update using checkboxes.

## Customization

- **Update Strategy**:
    - `All`: Update all installed apps using `scoop update *`.
    - `Custom`: Select specific apps to update by checking the boxes next to each app in the list.

- **Autostart**: Enable or disable autostart for the application.

## Building the Icon

If you want to change the application's icon or generate your own:
1. Create the icon files (e.g., `scoop-auto-updater.ico`, `idle.png`, `check.png`, etc.).
2. Modify the `icons/` folder with your custom icon files.
3. The application uses a `.rc` file for including the icon resources.

## License

This project is licensed under the GNU General Public License, version 3 - see the [LICENSE](LICENSE) file for details.
