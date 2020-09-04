# Imgoverlay

A Vulkan and OpenGL overlay rendering images from shared memory.

Started as [MangoHud](https://github.com/flightlessmango/MangoHud) fork.

## Building

#### Dependencies
* python3
* Xlib
* Qt5 - QtWebEngineWidgets

#### Build
```sh
meson build
meson install
```
ArchLinux: [PKGBUILD](dist/PKGBUILD)

# Usage

## Run Vulkan/OpenGL app
```sh
# Vulkan
IMGOVERLAY=1 /path/to/app
# OpenGL
TODO
```

#### Configuration (app)
* `IMGOVERLAY_CONFIGFILE` env variable
* `~/.config/imgoverlay/<APP>.conf` (eg. `~/.config/imgoverlay/vkcube.conf`)
* `~/.config/imgoverlay/overlay.conf`

```ini
control=/tmp/imgoverlay.socket
toggle_overlay=Shift_R+F12
```

## Run client
```sh
imgoverlayclient [--tray] [config-file]
```
* `--tray` start minimized in system tray
* `config-file` path to config file (default `~/.config/imgoverlayclient.conf`)

#### Configuration (client)

```ini
[General]
Socket=/tmp/imgoverlay.socket

[Github_example]
Url=https://github.com/nowrep/imgoverlay
X=0
Y=0
Width=200
Height=200

[Another_site]
Url=https://google.com
X=210
Y=210
Width=100
Height=100
```
