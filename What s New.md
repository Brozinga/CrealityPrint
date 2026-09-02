## What's New

### Calibration
- **VFA (Vibration/Frequency Analysis) calibration** is now available directly in the Calibration menu, no longer hidden behind the experimental/alpha flag.
- Fixed a layout bug where several calibration dialogs (PA, Temperature, Max Volumetric Speed, VFA, Retraction Test, Retraction Speed, and others) could render without their **OK / Start** button, cutting off the bottom of the dialog. This was most visible on Linux/GTK.
- Fixed a further Linux-only issue where the **Pressure Advance** and **Temperature** calibration dialogs specifically could still open without their OK button the very first time they were shown in a session (closing and reopening used to work around it). The dialogs now recompute their layout once the window has actually finished being realized, instead of before, so the button shows up correctly right away.
- Renamed the **Speed calib** and **Acceleration calib** submenus to **Speed** and **Acceleration** for consistency with the other Calibration submenus.

### Linux stability fixes
- Fixed a GTK console error (`gtk_cell_layout_get_cells: assertion 'GTK_IS_CELL_LAYOUT (cell_layout)' failed`) that was triggered every time a preset combo box updated (e.g., switching presets).
- Fixed dozens of GTK warnings (`gtk_widget_set_size_request: assertion 'width >= -1' failed`) shown at startup, caused by text input fields receiving a negative width before being fully initialized.
- Fixed a startup crash on Linux caused by the AppImage bundling incompatible OpenGL dispatch libraries (`libOpenGL.so`, `libGLdispatch.so`) from the build container. The app now correctly uses the system's own GPU driver libraries.
- Fixed a crash on native Wayland sessions (`Gdk-Message: Error 22 dispatching to Wayland display`) that could happen right after the homepage finished loading. The AppImage now automatically launches using the X11 backend (via XWayland), which is stable, instead of the native Wayland backend.
- Fixed a crash (X11 `BadMatch` error on a GLX request) that happened when toggling **Lite Mode** on/off in the G-code Preview legend after slicing a plate.

### Build & CI
- Added a Docker-based Linux build pipeline (Ubuntu 26.04) to produce the Linux AppImage in a reproducible environment.

### Version
- Updated application version to **7.2.1.5477**.

## Part 2

### Device & Camera
- Fixed the Camera panel's **Refresh** button doing nothing after the first successful connection (most noticeable on Linux/Klipper printers) — it now always forces a fresh reconnect, the same as opening the Device tab for the first time, instead of silently no-op'ing if a previous connection attempt got stuck.
- Added support for Klipper printers with a custom webcam managed by Moonraker (e.g. `mjpegstreamer`/crowsnest) — the app now asks the printer for its actual webcam configuration and uses it automatically, instead of always assuming Creality's built-in WebRTC camera.
- Fixed a crash on Linux when opening the Device page or reconnecting the camera, caused by overlapping connection attempts to the printer's camera stream racing each other.
- Fixed a related bug where, if the app crashed, saving the diagnostic crash dump could itself fail and abort the process (when `/tmp` is on a different filesystem/mount than the user's config folder) — crashes now get properly logged instead of turning into a harder-to-diagnose abort.
- Added a page-reload button to the top-right corner of the Device tab that fully refreshes the tab from scratch, useful if the camera or any other panel gets stuck.

### Linux stability fixes
- Fixed a crash (X11 `BadMatch` error on a GLX request) that happened when switching between plates in the Preview tab's plate list after slicing.

### Cloud & Account
- Fixed self-built AppImages defaulting to Creality's internal staging cloud environment instead of production, which broke Creality Cloud login, direct print sending, and printer-preset compatibility (e.g. for the K1C 2025 CFS-C).

### Calibration
- The **Speed** calibration submenu (Limit speed, Speed tower, Jitter speed, Fan speed) is now available outside alpha builds too.

### Version
- Updated application version to **7.2.1.5478**.

## Part 3

### Offline Mode & Cloud
- Added an **Offline Mode** toggle in **Preferences → General → Network** ("Access Creality Cloud servers", on by default). When it is off, the app no longer contacts Creality's servers at all — login, Model Library, AI Cloud, AI Chat, cloud print/G-code uploads, the cloud-sync MQTT channel, update checks and the community home page are all blocked *before* any request is made. LAN features keep working.
- Added a **"Show camera preview in Send to Printer"** toggle in the same section (on by default). When off, the *Send to LAN Printer* dialog shows no camera preview and the backend never opens a WebRTC/RTSP connection to the printer.
- Turning either toggle off now also **tears down connections that are already open** (camera streams, cloud web views, cloud-sync MQTT); turning it back on reconnects automatically.

### Device & Camera
- Added diagnostic logging around the camera preview to explain freezes: the app now logs when a WebRTC stream stalls (no new frame for several seconds while still "connected") and when it recovers, when a reconnect attempt is silently ignored, when the RTC connection fails, and the start/end of each `/videostream` session. Everything goes to the persistent app log.
- **The camera stream now recovers on its own after it stalls.** On some printers (e.g. K2 Plus) a dropped WebRTC stream would stay dead — reloading the video page did nothing and only a full app restart brought it back. Three bugs in `WebRTCDecoder` combined to cause it and are now fixed:
  - A frame arriving with a non-positive size made `receiveFrame()` return while still holding the frame lock, so every later access to the decoder blocked forever.
  - `startPlay()` treated "connected to the same URL" as "already playing", so a reload never rebuilt a connection the peer had dropped silently; `stopPlay()` never reset the status.
  - `startPlay()` held the frame lock across the teardown of the old session and the start of the new receive thread — which was itself waiting on that same lock.
  The control path now has its own lock, the frame lock guards only the frame buffer (RAII), and `getFrameData()` returns a copy taken under the lock. On top of that the decoder tracks the time of the last decoded frame: a stream silent for 8 seconds counts as dead, `startPlay()` rebuilds it instead of returning early, and a watchdog thread reconnects on its own so a stalled stream recovers with no user action (logged as `webrtc stream stalled, reconnecting: <url>`). Closing the camera panel suppresses the watchdog.

### Plate view controls
- Added two icons to each plate's on-bed icon column, matching the existing lock / arrange / settings style:
  - **Hide grid lines** — hides just that plate's grid lines.
  - **Hide plate and its objects** — hides the plate and everything on it (including its wipe tower). This is purely visual: slicing and export are unaffected.
- Both are session-only view toggles and are not saved into the project file.

### Build
- `libslic3r_version.h` is now regenerated on every CMake configure, so the version shown inside the application always matches `version.inc` (previously it could stay pinned to the first value it was generated with).
- `run_gettext.sh` is tracked with its executable bit set. `BuildLinux.sh -s` calls it directly, so without it the Docker build failed with `Permission denied` right after the binary linked.

### Version
- Updated application version to **7.2.1.5481**.

