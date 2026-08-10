# OneKVM NanoKVM OLED Extension

English | [简体中文](README.zh-CN.md)

This extension manages the built-in OLED display on NanoKVM PCIe and Cube devices. Once installed, it gives you a visual editor for designing the screen, lets you show live information such as the IP address and video status, and handles rotation, automatic sleep, and burn-in protection.

It is installed separately from OneKVM Core, so you can enable, disable, or remove it without modifying Core itself.

> **Build note:** This repository contains the extension source code only. Release packages are built by the OpenEmbedded recipes in `onekvm-distro`; there is currently no standalone release build in this repository.

## Supported devices

The extension combines the OneKVM machine description with a hardware probe at boot, then selects the correct display automatically. You do not need to configure I2C parameters yourself.

| Device | Display configuration |
| --- | --- |
| NanoKVM PCIe | I2C 5, address `0x3c`, 64×32 display area |
| NanoKVM Cube Alpha | I2C 1, address `0x3d`, 128×64 panel |
| NanoKVM Cube Beta | I2C 5, address `0x3d`, 128×64 panel |
| NanoKVM Lite | No OLED; the extension cannot be enabled |

On NanoKVM Lite, the plugin manager prevents the extension from being enabled and the settings page clearly explains that the device is unsupported.

## What you can do

- Design the OLED screen in your browser with text, lines, rectangles, and custom bitmap graphics
- Manage layers with thumbnails, visibility controls, drag ordering, copy and paste, and keyboard shortcuts
- Select several layers to center, align, or distribute them, and resize text areas, rectangles, and graphics directly on the canvas
- Display the IPv4 address, hostname, codec, FPS, resolution, stream state, and uptime
- Add ETH, HDMI, HID, and Wi-Fi indicators, with separate styles for connected and disconnected states
- Show live-viewing activity with `ON LIVE` text and a blinking status dot
- Mix ASCII and common Simplified Chinese text, with 1×/2× scaling and 3×5, 4×5, or 5×5 Latin letter widths
- Rotate the display to 0°, 90°, 180°, or 270° and preview the selected orientation accurately in the browser
- Import or export the complete layout as versioned JSON, or restore the default layout for the current device
- Put the panel to sleep after no visible state changes and periodically shift the output by one pixel to reduce burn-in

Restoring the default layout only replaces the layer stack. It keeps the display, rotation, sleep, and burn-in settings unchanged.

## Install and enable

The package name is `onekvm-extension-nanokvm-oled`. Install it through your OneKVM distribution package or image, then use the plugin manager to enable or disable it:

```sh
onekvm-plugin-manager enable nanokvm-oled
onekvm-plugin-manager disable nanokvm-oled
```

When the extension is disabled, it clears and powers off the panel, removes the display instance, and stops the OLED process instead of leaving it running in the background.

## How it works

- The C++20 display runtime reads live data from `org.onekvm.Core1.Status` through `sd-bus` and drives the panel through `i2c-dev`.
- The web designer uses the Vue and Naive UI environments provided by OneKVM UI instead of bundling another copy of either framework.
- The NanoKVM OLED does not provide a useful brightness control, so the extension uses the controller's fixed contrast.
- Burn-in shifts are applied only to the physical panel. The design preview stays in place.

The browser preview and C++ renderer share the same font data so their output remains consistent. Font source, licensing, and regeneration details are documented in [`fonts/README.md`](fonts/README.md).

## Development note

OneKVM includes the extension version in web asset URLs and marks those assets as immutable. Whenever files under `web/` change, increment the manifest/package version as well. Increasing only the package revision can leave browsers using an older cached version of the canvas code.

Install the pinned web dependencies, then build and run the native and browser
tests with:

```sh
npm ci
make test
```

The native build requires a C++20 compiler and the `libsystemd` development
files. `make test` also type-checks and bundles the Vue page before running the
C++ and Node.js tests.

## License

This project is licensed under the GNU General Public License v3.0. See [`LICENSE`](LICENSE).
