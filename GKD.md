# GKD Bubble ARM64 SDL Build

This notes the command used to cross-compile `cheapbin-sdl-aarch64` for the
GKD Bubble and why the link step allows unresolved transitive SDL shared
library dependencies.

## Successful Build

The working compiler was not `aarch64-linux-gnu-gcc`; it was:

```sh
/usr/bin/aarch64-linux-gnu-gcc-12
```

The sysroot used for SDL2 and ALSA headers/libs was:

```sh
/tmp/cheapbin-arm64-sysroot
```

The successful build command was:

```sh
make -f Makefile.sdl linux-arm64 \
  ARM64_CC=/usr/bin/aarch64-linux-gnu-gcc-12 \
  arm64_CFLAGS="-std=c11 -Wall -Wextra -Wno-unused-parameter -O2 -D_DEFAULT_SOURCE --sysroot=/tmp/cheapbin-arm64-sysroot -I/tmp/cheapbin-arm64-sysroot/usr/include/SDL2 -I/tmp/cheapbin-arm64-sysroot/usr/include -I/tmp/cheapbin-arm64-sysroot/usr/include/aarch64-linux-gnu" \
  arm64_LIBS="-L/tmp/cheapbin-arm64-sysroot/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,/tmp/cheapbin-arm64-sysroot/usr/lib/aarch64-linux-gnu -Wl,--allow-shlib-undefined -lSDL2 -lasound -lpthread -lm"
```

The output is:

```sh
cheapbin-sdl-aarch64
```

The build target verified it as:

```text
ELF 64-bit LSB pie executable, ARM aarch64, dynamically linked,
interpreter /lib/ld-linux-aarch64.so.1, for GNU/Linux 3.7.0
```

## Why The Overrides Are Needed

`Makefile.sdl linux-arm64` defaults to:

```sh
aarch64-linux-gnu-gcc
```

On this machine that binary was missing, but `/usr/bin/aarch64-linux-gnu-gcc-12`
exists and works.

The default ARM64 build also expects `aarch64-linux-gnu-pkg-config` to provide
SDL2 and ALSA flags. In this environment it did not return usable flags, so the
build needed explicit sysroot include and library paths:

```sh
--sysroot=/tmp/cheapbin-arm64-sysroot
-I/tmp/cheapbin-arm64-sysroot/usr/include/SDL2
-I/tmp/cheapbin-arm64-sysroot/usr/include
-I/tmp/cheapbin-arm64-sysroot/usr/include/aarch64-linux-gnu
-L/tmp/cheapbin-arm64-sysroot/usr/lib/aarch64-linux-gnu
```

`-Wl,-rpath-link,/tmp/cheapbin-arm64-sysroot/usr/lib/aarch64-linux-gnu` tells the
cross linker where to look for direct shared-library references while linking.

## Why `--allow-shlib-undefined`

The sysroot has `libSDL2.so` and `libasound.so`, but it does not include every
desktop/video/audio library that the shared SDL2 build was linked against.
Examples reported by the linker included:

```text
libpulse.so.0
libsamplerate.so.0
libX11.so.6
libXext.so.6
libXcursor.so.1
libXi.so.6
libXfixes.so.3
libXrandr.so.2
libdrm.so.2
libgbm.so.1
libwayland-client.so.0
libxkbcommon.so.0
libdecor-0.so.0
```

Those are transitive dependencies of `libSDL2.so`. The cheapbin binary itself
does not call those libraries directly; it links directly to SDL2 and ALSA.
On the GKD Bubble, SDL2's runtime loader should resolve the transitive shared
library dependency chain from the device rootfs.

Without this flag, the cross linker tries to fully resolve all of SDL2's shared
library internals at build time and fails with many undefined references from
inside `libSDL2.so`, such as X11, Wayland, PulseAudio, libdrm, gbm, and
libdecor symbols.

This flag is the important part:

```sh
-Wl,--allow-shlib-undefined
```

It means: allow unresolved symbols that live inside linked shared libraries.
It does not allow unresolved symbols from cheapbin's own object files.

## Failed Attempts

Plain ARM64 target failed because `aarch64-linux-gnu-gcc` was not present:

```sh
make -f Makefile.sdl linux-arm64
```

Using gcc-12 without sysroot flags found the compiler but not SDL headers:

```sh
make -f Makefile.sdl linux-arm64 ARM64_CC=/usr/bin/aarch64-linux-gnu-gcc-12
```

Adding sysroot SDL/ALSA paths compiled but failed at link time because SDL2's
transitive shared dependencies were not all present in the sysroot link search
path. Adding `-Wl,--allow-shlib-undefined` fixed that.


## Installation

Be careful about the names because in gxmenu the filename tells the order in the menu section:

* Maybe you need to `mount -o remount,rw /`
* Copy the `cheapbin-aarch64-linux` to `/usr/bin/cheapbin`
* Copy the ini file from `src/sdl/cheapbin.gxmenu` to `/storage/miniplus/sections/01arcade/08cheapbin`
* Copy the assets/cheapbin-icon.png to `/storage/miniplus/skins/Default/icons/cheapbin.png`

```
[Desktop Entry]
Name=Cheapbin
Description=Binary-to-chiptune music player
exec=/usr/bin/cheapbin
params=/usr/bin/ls
icon=icons/cheapbin.png
selectordir=/
wrapper=true
type=Application
Categories=Audio;Music;Game;
selectorbrowser=true
#selectorfilter=.*
```

