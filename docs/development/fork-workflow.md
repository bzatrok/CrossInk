---
title: Fork Workflow
parent: Development
nav_order: 5
---

# Fork Workflow

How this fork tracks upstream, builds the X4 Pro image, and flashes it from a
local Inky. Written for macOS on Apple Silicon.

## Remotes

```
origin    git@github.com:bzatrok/CrossInk.git   (your fork; pushes go here)
upstream  https://github.com/uxjulia/CrossInk.git (fetch only)
```

## Pull the latest upstream

```sh
git fetch upstream
git rev-list --left-right --count main...upstream/main   # ahead/behind
git checkout main
git pull --ff-only upstream main
git submodule update --init --recursive                   # freeink-sdk, assets/tabler-icons
```

If the fast-forward is refused, main has local commits. Rebase the feature
branch instead and follow the conflict rule in `AGENTS.md`: read the upstream
PR behind each conflicting commit before choosing a side.

After a pull, check the platform pin. Upstream v1.5.1 requires PlatformIO Core
6.1.18 or newer; upgrade the bundled core with:

```sh
~/.platformio/penv/bin/pio upgrade
```

## Build

PlatformIO lives in its own virtualenv and is not on `PATH`:

```sh
~/.platformio/penv/bin/pio run -e x4-pro        # X4 Pro image
~/.platformio/penv/bin/pio run -e default       # C3 image (X3/X4)
~/.platformio/penv/bin/pio run -e sticky        # reTerminal Sticky
```

Outputs land in `.pio/build/<env>/firmware.bin`.

### Simulator on macOS 15

Apple's clang marks float `std::from_chars` unavailable before macOS 26, which
breaks `lib/Epub/Epub/css/CssParser.cpp`. Build with Homebrew's LLVM 20 and its
libc++ instead; PlatformIO honors `CC`/`CXX`, and `PLATFORMIO_BUILD_FLAGS` is
appended to the env's flags:

```sh
export CC=/opt/homebrew/opt/llvm@20/bin/clang CXX=/opt/homebrew/opt/llvm@20/bin/clang++
export PLATFORMIO_BUILD_FLAGS="-D_LIBCPP_DISABLE_AVAILABILITY -nostdinc++ -isystem /opt/homebrew/opt/llvm@20/include/c++/v1 -L/opt/homebrew/opt/llvm@20/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm@20/lib/c++"
~/.platformio/penv/bin/pio run -e x4-pro-simulator -t clean   # once, after switching compilers
~/.platformio/penv/bin/pio run -e x4-pro-simulator
.pio/build/x4-pro-simulator/program                            # opens the SDL window
python3 scripts/run_simulator_smoke_test.py --env x4-pro-simulator --theme lyra
```

Requires `brew install llvm@20 sdl2`. Icon generation additionally needs
`brew install librsvg` for `rsvg-convert`.

`scripts/run_simulator_smoke_test.py` rebuilds unless `--no-build` is given,
and it rebuilds with whatever `CC`/`CXX` are exported. Export the LLVM
variables first or pass `--no-build`, or the Apple clang objects poison the
build and the next LLVM build needs a clean.

### Scripted screenshots

The simulator takes synthetic input and saves screenshots without a window,
which is the quickest way to check a screen. Times are milliseconds after
boot; touch coordinates are logical portrait pixels (480x800):

```sh
mkdir -p /tmp/sim/fs_/.crosspoint && cd /tmp/sim
echo '{"uiTheme":8}' > fs_/.crosspoint/crossink-settings.json     # optional: preset settings
SDL_VIDEODRIVER=dummy \
CROSSPOINT_SIM_INPUT_SCRIPT="3500:DOWN;4000:DOWN;4500:DOWN;5000:CONFIRM;6500:CONFIRM;12500:TAP:352,714;20000:QUIT" \
CROSSPOINT_SIM_SCREENSHOTS="3000:/tmp/sim/home.bmp;13300:/tmp/sim/hue.bmp" \
~/dev/CrossInk/.pio/build/x4-pro-simulator/program
```

Keys: `BACK`, `CONFIRM`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `POWER`, `HOME`,
`TAP:x,y`, `SWIPE:x1,y1,x2,y2`, `QUIT`. On the Lyra home screen the Home
Control entry is three `DOWN` presses from the top. Hue fixtures pair on the
third attempt (about six seconds), tado approves on the third poll. Convert the
BMPs with `magick home.bmp home.png`.

## Flash from a local Inky

The hosted Inky at inky.crossink.dev flashes GitHub releases only. The
self-hosted Inky can also serve one local X4 Pro build.

One-time setup:

```sh
cd ~/dev
git clone --depth 1 https://github.com/uxjulia/inky-self-hosted.git
cd inky-self-hosted
brew install cairo unar
cp .env.example .env
npm install          # installs the frontend deps and a Python venv for the API
npm run build
```

Each time you want to flash a build:

```sh
mkdir -p ~/inky-fw
cp ~/dev/CrossInk/.pio/build/x4-pro/firmware.bin ~/inky-fw/firmware-x4-pro.bin
cd ~/dev/inky-self-hosted
INKY_HOST=127.0.0.1 INKY_PORT=8000 INKY_DEV_FIRMWARE_DIR=$HOME/inky-fw npm run start
```

Open http://127.0.0.1:8000 in Chrome or Edge, go to Flash Tools, pick
Xteink X4 Pro, and choose the entry tagged `local-x4-pro` under the pre-release
channel. Binding to `127.0.0.1` keeps Inky off the LAN. The file name must be
exactly `firmware-x4-pro.bin`; Inky re-reads the folder on each request, so
replacing the file and reloading the page is enough.

Without Inky, `~/.platformio/penv/bin/pio run -e x4-pro -t upload` flashes over
USB with the same esptool.

## Serial log

`pio device monitor` needs a real terminal. To capture to a file instead:

```sh
~/.platformio/penv/bin/python - <<'EOF' > serial.log
import serial, sys, time
with serial.Serial('/dev/cu.usbmodem1101', 115200, timeout=1) as port:
    end = time.time() + 540
    while time.time() < end:
        line = port.readline()
        if line:
            sys.stdout.write(line.decode('utf-8', 'replace')); sys.stdout.flush()
EOF
```

Find the port with `ls /dev/cu.usbmodem*`. The reader enumerates as an
Espressif device (vendor `0x303a`); if nothing appears, use a data cable
directly in the Mac, or hold Boot while pressing Reset for download mode.

## Recovery

Flashing writes only the app partition. If a build boot-loops: press Reset,
then hold Back and Power to boot to Home. Holding Down while pressing Power at
boot opens the SD-card firmware picker, so a stable `firmware-x4-pro-*.bin` on
the card can always be installed without USB.
