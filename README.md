# JoyOff

A simple Nintendo Switch homebrew app that puts original Joy-Con controllers into shipment low-power mode and disconnects them, ready for long-term shelf storage. No PC required.

![JoyOff on Nintendo Switch](assets/joyoff-screenshot.jpg)

## Features

- Automatic detection of connected wireless Joy-Con L/R controllers.
- Individual controller colors, connection states, and operation results.
- Multiple controller pages with a miniature Joy-Con legend, L/R navigation, and touch swipes.
- Turn off all ready controllers across every page with one confirmation.
- Touch controls remain available after the controllers disconnect.
- TXT diagnostics saved to `sdmc:/joyoff-debug.txt`.

## Requirements

- A Nintendo Switch running custom firmware and firmware 13.0.0 or newer.
- Original Joy-Con L/R controllers connected wirelessly.
- Homebrew Menu in full application mode (title override).

MissionControl is not required. Pro Controllers, Joy-Con 2, and third-party controllers are outside the scope of this app.

## Usage

1. Copy `JoyOff.nro` to `sdmc:/switch/JoyOff/JoyOff.nro` and launch it through the Homebrew Menu.
2. Detach your Joy-Cons and connect them wirelessly. Detection refreshes automatically.
3. Press **Minus (-)** or tap **Turn Off All**, then confirm.
4. Check the per-controller results. Use the touchscreen to exit after the controllers disconnect.

**Saved Bluetooth pairing will be cleared.** JoyOff resets pairing, enables shipment low-power mode, and disconnects the controllers. Attach them to a Switch to wake and pair again. If an operation fails partway through, pairing may already have been cleared; check each controller's result.

| Control | Action |
| --- | --- |
| Minus (-) | Open the Turn Off All confirmation / confirm |
| B / Plus (+) | Cancel, return, or exit |
| X | Refresh detection |
| Y | Open diagnostics |
| L / R | Previous / next controller page |
| Touch | Buttons, miniature page legend, and horizontal page swipes |

Controller pages group left and right devices for display; they do not establish controller pairs. Colors come from stored HID color information and may differ from replacement shells.

## Build

Requires devkitPro/devkitA64, libnx, deko3d, GLM headers, and `uam`. The NanoVG/deko3d renderer is included.

```sh
cd switch
make -j4 release
```

The output includes `JoyOff.nro`, an SD-card package, and a corresponding source archive. See the [technical README](switch/README.md) for implementation details and diagnostic information.

## Credits and license

The Switch application is distributed under [LGPLv3](switch/COPYING), with the accompanying [GPLv3 terms](switch/COPYING.GPL). Graphics initialization and UI helpers are adapted from Switch 90DNS Tester by Nexrem and contributors. Bundled NanoVG/deko3d code retains its original notices and credits Adubbz, Mikko Mononen, and fincs.

Joy-Con illustrations are adapted from [0 Noctis 0's Wikimedia illustration](https://commons.wikimedia.org/wiki/File:Nintendo_Switch_Joy-Con_illustration.svg), licensed CC BY-SA 4.0. See [art attribution](switch/COPYING.ART).

Protocol and implementation references include [jc_toolkit](https://github.com/CTCaer/jc_toolkit), [dekuNukem's reverse-engineering notes](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering), [libnx](https://github.com/switchbrew/libnx), and [MissionControl](https://github.com/ndeadly/MissionControl).

## Support My Work

If you find this project useful, please consider supporting me by buying me a coffee!

<a href="https://www.buymeacoffee.com/sthetixofficial" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" >
</a>
