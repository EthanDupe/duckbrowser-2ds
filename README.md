# Exo — New Nintendo 2DS XL / 3DS Port (`.cia` & `.3dsx`)

A native C99 / `citro2d` / `libctru` / `NDSP` port of **Exo** (the js13k space strategy game by ai Doge & Jack Oatley, with music by Francis Fonye) tailored for the **New Nintendo 2DS XL** dual-screen layout and wireless **FBI Remote Install** without removing your microSD card.

## Features & Dual-Screen Controls

- **Top Screen (400×240)**: Star system viewport, orbital paths, green power-relay network graph, sun/planets/moons, satellites, mining stations, all 5 defense platform types (Lasers, Beam, Rockets, EMP, Lightning), enemy ship swarms, and HUD status bar.
- **Bottom Touch Screen (320×240)**:
  - **Tactical Radar Minimap (Left)**: Tap or drag with the stylus to jump the camera across the solar system or select a celestial body.
  - **Unit Inspector & Upgrade Panel (Right)**: View unit HP/online status, upgrade weapon platform levels, confirm/cancel orbital placement, or toggle audio.
  - **7-Button Construction Dock (Bottom)**: Tap to build Satellites (`30M`), Mining Stations (`50M`), Laser Platforms (`100M`), Beam Platforms (`150M`), Rocket Platforms (`200M`), EMP Platforms (`250M`), or Lightning Platforms (`300M`).
- **Physical Buttons**:
  - **Circle Pad**: Pan camera and world reticle
  - **L / R Triggers**: Zoom out / Zoom in
  - **D-Pad Up / Down**: Increase / decrease orbit radius when placing a station
  - **D-Pad Left / Right**: Cycle target planet/sun when placing a station
  - **A Button**: Select body / Confirm station placement
  - **B Button**: Cancel construction mode
  - **X Button**: Center camera on Base Planet
  - **Y Button**: Pause / Resume game
  - **START**: Exit cleanly to the 2DS XL HOME Menu

## Step 1: Build `exo.cia`

You can build `exo.cia` either locally with [devkitPro](https://devkitpro.org/) or in the cloud for free with GitHub Actions:

### Option A: Local Build (Windows / macOS / Linux)
1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) and install the 3DS development packages:
   ```bash
   sudo dkp-pacman -S 3ds-dev 3ds-citro2d 3ds-citro3d
   ```
2. Place [makerom](https://github.com/3DSGuy/Project_CTR/releases) and [bannertool](https://github.com/diasurgical/bannertool/releases) in your `PATH` (e.g., `/opt/devkitpro/tools/bin` or `C:\devkitPro\tools\bin`).
3. Run:
   ```bash
   make cia
   ```
   This produces `exo.cia` (for your 2DS XL HOME Menu) as well as `exo.3dsx`.

### Option B: Zero-Setup Cloud Build (GitHub Actions)
Push this folder to a GitHub repository. The included `.github/workflows/build_cia.yml` workflow automatically runs the official `devkitpro/devkitarm` container, installs `makerom` and `bannertool`, compiles `exo.cia`, and uploads it as a downloadable build artifact.

## Step 2: Wireless Remote Install to Your 2DS XL (No microSD Removal)

Make sure your PC and your **New Nintendo 2DS XL** are connected to the same Wi-Fi network.

### Method 1: Automatic Push via `remote_install_cia.py` (Recommended)
1. Open **FBI** on your 2DS XL and select **Remote Install** -> **Receive URLs over the network**.
2. Note the IP address displayed on your 2DS XL (e.g., `192.168.1.45`).
3. On your computer, run:
   ```bash
   python3 tools/remote_install_cia.py <YOUR_2DS_IP> exo.cia
   ```
4. Press **A** on your 2DS XL when prompted to confirm and install **Exo** straight to your HOME Menu.

### Method 2: Manual URL Entry in FBI
1. On your computer, start the local server mode:
   ```bash
   python3 tools/remote_install_cia.py --serve-only exo.cia
   ```
2. On your 2DS XL, open **FBI** -> **Remote Install** -> **Manually enter URLs** and type the URL printed in your terminal (e.g., `192.168.1.100:8080/exo.cia`).
