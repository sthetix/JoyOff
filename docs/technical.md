# JoyOff for Nintendo Switch

A homebrew application for original Joy-Con L/R connected over Bluetooth. Requires CFW, firmware 13.0.0 or newer, and access to the `hid:sys` and `btdrv` services. BTM access is optional for diagnostic comparisons. Use title override/full application mode for the graphical interface. MissionControl is not required.

## Usage

1. Connect the Joy-Cons wirelessly and detach them from the rails.
2. Open JoyOff in the Homebrew Menu. **X / Refresh** refreshes the connected device list. Detection uses HID unique pad IDs, controller types, Bluetooth interfaces, and Bluetooth addresses directly; it does not depend on Bluetooth names. A read-only Device Info request verifies the reported L/R identity before Power Off becomes available. The screen shows HID pad and wireless L/R counts; BTM names are recorded only as diagnostic comparisons in the TXT log.
3. **Minus (-) / Turn Off All** opens a confirmation listing the persistent effects. Press Minus again or tap Confirm to proceed; B cancels.
4. Read the per-device results. Tap Exit after the controllers disconnect; touch remains available.

The home screen footer contains Diagnostics (Y), About (ZL), Turn Off All (Minus), and Exit (Plus). About opens a modal popup showing the developer, build version, and a short description. Tap Close or press B / Plus to dismiss it; ZL only opens the popup. The Diagnostics footer contains Refresh (X), TXT Logging (Minus), Back to JoyOff (Y), and Back (B). All footer controls support touch.

The main screen uses vector Joy-Con illustrations with separate left/right states: detached input pending, attached, checking/working, ready, low power, or error. Press **Minus (-)** to open the confirmation and press it again to execute. **B / Plus (+)** cancels or exits. Touch offers the same actions, including when only a right Joy-Con is available. **Y / Diagnostics** opens technical details, controller addresses, per-device results, firmware, and TXT log status; B, Plus, or Y returns to the main screen. Diagnostics supports four devices per page using L/R or tapping the left/right halves of its panel. X / Refresh remains available when idle. The Bluetooth backend and command sequence are unchanged by this visual update.

The illustrations sit toward the center, with left status text to the left and right status text to the right. Front-view proportions follow 0 Noctis 0's Wikimedia illustration; the drawings retain the real L/R button arrangements, directional arrows, ABXY labels, shoulder details, and distinct Home/Capture buttons. See `COPYING.ART` for attribution and the illustration license.

Body and button colors are read from the matching Npad slot using `hidGetNpadControllerColorSplit` for handheld/paired controllers and `hidGetNpadControllerColorSingle` for standalone L/R controllers. These are optional reads from HID state, with no additional Bluetooth or SPI commands. Failed color reads use neutral gray and do not block Turn Off. After low power and disconnect are confirmed, the controller illustrations and miniature legend return to neutral gray. Stored color values remain available in Diagnostics. Diagnostics shows the RGB values and TXT logs record the raw HID values. Stored colors may differ from replacement shells.

Each home-screen card represents one physical controller with its own color, status, and address (or pad ID when no address is available). The home screen shows one left and one right device per page, sorted by Bluetooth address and then pad ID. A single L/R pair needs only one page. Extra devices add pages controlled by L/R, the miniature page legend, or horizontal swipes. Page positions do not establish pairing between controllers. Empty slots are labeled when one side has fewer devices. Turn Off All and its confirmation show the number of ready devices across every page and process all of them, not just the current page. The global result message also considers every device. Page navigation does not send controller commands.

When there is more than one controller page, a miniature Joy-Con legend shows the devices on each page in their reported colors, with the current page outlined in mint. Tap a miniature page directly, use L/R, or swipe horizontally across the large controller cards. A swipe changes one page on release and does not activate Turn Off or confirmation buttons. Empty L/R slots are omitted from the miniatures. A single-page setup keeps the simpler layout without the legend.

While idle, the app checks HID pad IDs, types, and interfaces every 500 ms and automatically refreshes detection when they change, including rail/Bluetooth transitions. Each identity check allows up to three attempts (the original Device Info request plus two timeout retries); after those attempts fail, detection waits for manual Refresh or a connection change. Auto-refresh pauses during confirmation and active operations; confirmation rechecks the connection list before sending Turn Off commands. Results remain visible after the requested disconnects. Pressing Minus while unavailable explains the current reason on screen and in the TXT log. Manual X / Refresh remains available when idle.

Input configuration includes standalone Joy-Con Left and Right (`NpadStandard`), in addition to paired and handheld styles. Each pad ID is matched to its current Npad slot using `hidsysGetUniquePadsFromNpad`. Connected/wired attributes from the matching Npad state are checked alongside the HID interface metadata. Attached controllers remain visible with a detach instruction and cannot receive Turn Off commands. Detached controllers without an active matching input connection remain pending. Connection polling includes these active-state attributes, so detaching/reconnecting can refresh detection even when a pad ID stays unchanged. An active wireless input state still requires a successful Device Info acknowledgement before Turn Off is enabled.

The numeric value returned by `hidsysGetUniquePadInterface` is logged but does not decide wired/wireless readiness or disconnect success. On the observed FW 23.0.1 setup its values disagreed with the assumed `HidNpadInterfaceType` interpretation and BTM connectivity. The active Npad connected/wired attributes are authoritative for these decisions; a failed interface-metadata query cannot block them.

The app sends an identity request (`02`), resets saved pairing (`07`), enables shipment low power (`08 01`), and disconnects (`06 00`). Each device advances independently as matching acknowledgements arrive. Failed or unconfirmed steps stop that device; other devices continue. Successful Bluetooth disconnect is checked using HID pad IDs and current connected/wired attributes for up to five seconds after the disconnect request. This wait does not resend commands. A new Refresh is required before another run. Partial failure can leave pairing cleared without completing low power/disconnect.

Only a missing Device Info (`02`) acknowledgement is automatically retried, at most twice with the existing two-second timeout per attempt. The current wireless input connection is checked before a retry. Send failures, rejected commands, and identity mismatches stop immediately. Pairing reset (`07`), shipment mode (`08`), and disconnect (`06`) are never automatically retried. The completed Turn Off result reports succeeded, failed, and skipped devices across all pages. Devices excluded before the operation, such as attached or identity-failed controllers, count as skipped.

Attach the Joy-Cons to a Switch to wake and pair again. Controllers still on the rails, Pro Controllers, Joy-Con 2, and third-party/emulated controllers are outside scope. Identity and device-type checks reduce accidental targeting but cannot authenticate a controller against clones or emulation.

## TXT diagnostics

TXT logging is disabled at each launch. In Diagnostics, press Minus or tap the logging toggle while idle to enable or disable it. Enable logging, then use X / Refresh to capture discovery. Existing log files are preserved when disabled.

While logging is enabled, each Refresh appends a timestamped diagnostic section to `sdmc:/joyoff-debug.txt` (the SD card root). The screen indicates whether the initial log write succeeded. The file is closed after each log entry so completed entries remain available after exiting the app. Entries include build date/time, firmware, applet type, service results, HID and all-profile connected-device counts, Bluetooth addresses, names and raw name bytes, filtering decisions, command results, matching acknowledgements, retries, ring errors, timeouts, and operation results. Routine input reports and unmatched replies are not written during ACK processing, reducing synchronous SD writes. Rejected-command replies retain a short raw dump. It does not log Bluetooth link keys. The all-profile enumeration is diagnostic only and does not change target selection. Refresh appends a new section without deleting previous logs.

Build the updated source, open JoyOff, enable TXT logging in Diagnostics, wait for detection to finish, tap Refresh once, then exit and inspect `/joyoff-debug.txt`. Turning off is not required to diagnose detection.

## Bluetooth implementation

The backend detects devices using libnx `hidsysGetUniquePadIds`, `hidsysGetUniquePadType`, `hidsysGetUniquePadInterface`, and `hidsysGetUniquePadBluetoothAddress`. Only left/right controllers on Bluetooth with a nonzero address become candidates. BTM enumeration is optional and diagnostic only; its failure cannot block HID discovery. Commands use `btdrvWriteHidData`. The app maps the report ring through btdrv command 38 using `Perm_Rw`, matching the service's remote permission as required by the kernel. It accesses the ring through a const pointer, follows its own cursor, and filters reports by controller address, timestamp, report ID, and subcommand. It never writes to the ring, registers/clears the system HID event, consumes ring entries, changes the shared read offset, initializes/finalizes the HID stack, or changes Bluetooth enable/pairing settings on the console.

The report-ring observer is experimental. Concurrent writers, firmware layout changes, or an overwritten response may cause an unconfirmed result. It stops without retrying persistent commands when an acknowledgement cannot be verified. Service success alone is not treated as a controller acknowledgement. Low power acknowledgement plus disconnect does not measure current draw or prove physical power-off. Hardware validation is required, including with and without MissionControl, reattachment/wake, partial failures, and both controller sides.

## Build

Requires devkitPro/devkitA64, libnx, deko3d, GLM headers, and `uam`. The NanoVG/deko3d renderer is bundled with its original notices.

```sh
make
```

Outputs `JoyOff.nro` and `JoyOff-1.0.0.zip`, containing only `switch/JoyOff.nro`. `JoyOff-1.0.0-source.zip` contains the corresponding buildable application and renderer sources with license notices.

The initial development build succeeded with devkitPro's `devkita64:20260219` Docker image. Compiler warnings occur in the bundled GLM/STB/fontstash renderer dependencies. Basic shutdown and the graphical interface have been tested on hardware during development. Device Info timeout recovery has also been observed in the hardware log. The five-second disconnect observation window and final release UI still require hardware validation.

## Credits and license

The graphics initialization and UI helpers are adapted from the local Switch 90DNS Tester, originally by Nexrem and contributors, licensed LGPLv3; this adapted Switch application is distributed under LGPLv3 (see COPYING, with GPLv3 terms in COPYING.GPL). The display is adapted for JoyOff; DNS functionality is not included.

Bundled NanoVG/deko3d code retains the licenses and copyright notices under `nanovg/`. The renderer credits Adubbz, Mikko Mononen, and fincs.

MissionControl by ndeadly was consulted for Bluetooth report flow; no MissionControl source is copied into this application. The controller backend is implemented directly against libnx. Protocol references: [CTCaer/jc_toolkit](https://github.com/CTCaer/jc_toolkit), [dekuNukem's HID subcommand notes](https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md), and [libnx](https://github.com/switchbrew/libnx).
