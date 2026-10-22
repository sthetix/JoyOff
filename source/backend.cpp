#include "backend.hpp"
#include "debug.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {
constexpr u64 timeoutNs = 2000000000ULL;
constexpr u64 disconnectTimeoutNs = 5000000000ULL;
u64 Now() { return armTicksToNs(armGetSystemTick()); }
bool SameAddress(const BtdrvAddress& a, const BtdrvAddress& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
u8 Expected(int phase) {
    return phase == 1 ? 0x02 : phase == 2 ? 0x07 : phase == 3 ? 0x08 : 0;
}

bool ReadTransport(HidsysUniquePadId padId, u8 side, bool& connected, bool& wired, bool logging = false) {
    connected = wired = false;
    bool queryFailed = false;
    for (unsigned slot = 0; slot < 9; ++slot) {
        const auto id = slot == 8 ? HidNpadIdType_Handheld : static_cast<HidNpadIdType>(slot);
        const u32 styles = hidGetNpadStyleSet(id);
        if (!styles) continue;
        HidsysUniquePadId pads[8]{};
        s32 count = 0;
        const Result rc = hidsysGetUniquePadsFromNpad(id, pads, 8, &count);
        if (logging) DebugLog("Npad slot=%u styles=%08X unique_pads_rc=%08X count=%d", static_cast<unsigned>(id), styles, rc, count);
        if (R_FAILED(rc)) { queryFailed = true; continue; }
        if (!std::any_of(pads, pads + std::clamp(count, 0, 8),
            [&](const HidsysUniquePadId& value) { return value.id == padId.id; })) continue;
        const auto read = [&](const HidNpadCommonState& state, bool split) {
            const u32 attributes = state.attributes;
            const u32 connectedBit = side == 1 ? HidNpadAttribute_IsLeftConnected : HidNpadAttribute_IsRightConnected;
            const u32 wiredBit = side == 1 ? HidNpadAttribute_IsLeftWired : HidNpadAttribute_IsRightWired;
            if (split ? (attributes & connectedBit) != 0 : (attributes & HidNpadAttribute_IsConnected) != 0) {
                connected = true;
                wired = wired || (attributes & (wiredBit | HidNpadAttribute_IsWired)) != 0;
            }
            if (logging) DebugLog("Npad pad=%016llX side=%u attributes=%08X connected=%d wired=%d",
                static_cast<unsigned long long>(padId.id), static_cast<unsigned>(side), attributes, connected, wired);
        };
        HidNpadCommonState state{};
        if ((styles & HidNpadStyleTag_NpadHandheld) && hidGetNpadStatesHandheld(id, &state, 1)) read(state, true);
        if ((styles & HidNpadStyleTag_NpadJoyDual) && hidGetNpadStatesJoyDual(id, &state, 1)) read(state, true);
        if (side == 1 && (styles & HidNpadStyleTag_NpadJoyLeft) && hidGetNpadStatesJoyLeft(id, &state, 1)) read(state, false);
        if (side == 2 && (styles & HidNpadStyleTag_NpadJoyRight) && hidGetNpadStatesJoyRight(id, &state, 1)) read(state, false);
    }
    return connected || !queryFailed;
}

void ReadColors(JoyTarget& target) {
    for (unsigned slot = 0; slot < 9; ++slot) {
        const auto id = slot == 8 ? HidNpadIdType_Handheld : static_cast<HidNpadIdType>(slot);
        const u32 styles = hidGetNpadStyleSet(id);
        if (!styles) continue;
        HidsysUniquePadId pads[8]{};
        s32 count = 0;
        if (R_FAILED(hidsysGetUniquePadsFromNpad(id, pads, 8, &count))) continue;
        if (!std::any_of(pads, pads + std::clamp(count, 0, 8),
            [&](const HidsysUniquePadId& pad) { return pad.id == target.padId.id; })) continue;
        HidNpadControllerColor color{};
        Result rc;
        if (styles & (HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual)) {
            HidNpadControllerColor left{}, right{};
            rc = hidGetNpadControllerColorSplit(id, &left, &right);
            if (R_SUCCEEDED(rc)) color = target.side == 1 ? left : right;
        } else if ((target.side == 1 && (styles & HidNpadStyleTag_NpadJoyLeft)) ||
                   (target.side == 2 && (styles & HidNpadStyleTag_NpadJoyRight))) {
            rc = hidGetNpadControllerColorSingle(id, &color);
        } else continue;
        DebugLog("HID colors pad=%016llX slot=%u side=%u rc=0x%08X body_rgba=%08X buttons_rgba=%08X",
            static_cast<unsigned long long>(target.padId.id), static_cast<unsigned>(id),
            static_cast<unsigned>(target.side), rc, color.main, color.sub);
        if (R_FAILED(rc) || (!color.main && !color.sub)) continue;
        target.colorsKnown = true;
        target.bodyColor = color.main;
        target.buttonsColor = color.sub;
        return;
    }
    DebugLog("HID colors unavailable for %s; using neutral illustration", target.name.c_str());
}
}

JoyBackend::~JoyBackend() {
    DebugLog("Backend exit: hidsys=%d btm=%d btdrv=%d mapped=%d", hidReady, btmReady, driverReady, mapped);
    if (mapped) shmemClose(&memory);
    if (driverReady) btdrvExit();
    if (btmReady) btmExit();
    if (hidReady) hidsysExit();
}

Result JoyBackend::Open() {
    DebugLog("Open: btm=%d btdrv=%d mapped=%d", btmReady, driverReady, mapped);
    if (!hosversionAtLeast(13, 0, 0)) return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
    if (!hidReady) {
        Result rc = hidsysInitialize();
        DebugLog("hidsysInitialize rc=0x%08X", rc);
        if (R_FAILED(rc)) return rc;
        hidReady = true;
    }
    if (!btmReady) {
        Result rc = btmInitialize();
        DebugLog("btmInitialize rc=0x%08X", rc);
        btmReady = R_SUCCEEDED(rc);
    }
    if (!driverReady) {
        Result rc = btdrvInitialize();
        DebugLog("btdrvInitialize rc=0x%08X", rc);
        if (R_FAILED(rc)) return rc;
        driverReady = true;
    }
    if (!mapped) {
        Handle handle = INVALID_HANDLE;
        // Get the report memory only. Do not register/clear HID events or consume its queue.
        Result rc = serviceDispatch(btdrvGetServiceSession(), 38,
            .out_handle_attrs = { SfOutHandleAttr_HipcCopy },
            .out_handles = &handle);
        DebugLog("btdrv command 38 (report shared memory) rc=0x%08X", rc);
        if (R_FAILED(rc)) return rc;
        // The kernel requires the service's exact remote permission; access stays const below.
        shmemLoadRemote(&memory, handle, 0x3000, Perm_Rw);
        rc = shmemMap(&memory);
        DebugLog("shmemMap Perm_Rw (passive observer) rc=0x%08X", rc);
        if (R_FAILED(rc)) { shmemClose(&memory); return rc; }
        mapped = true;
        ring = static_cast<const BtdrvCircularBuffer*>(shmemGetAddr(&memory));
    }
    return 0;
}

bool JoyBackend::LoggingEnabled() const {
    return debugLoggingEnabled;
}

void JoyBackend::SetLogging(bool enabled) {
    if (running) return;
    debugLoggingEnabled = enabled;
    logAvailable = enabled && DebugLog("=== TXT logging enabled | build %s %s | Refresh to capture discovery ===", __DATE__, __TIME__);
}

void JoyBackend::Refresh() {
    if (running) return;
    const u32 version = hosversionGet();
    logAvailable = DebugLog("=== Refresh | JoyOff HID discovery + TXT diagnostics | build %s %s | FW %u.%u.%u | applet type=%u ===",
        __DATE__, __TIME__, HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version),
        static_cast<unsigned>(appletGetAppletType()));
    targets.clear();
    diagnostics.clear();
    identifying = false;
    Result rc = Open();
    if (R_FAILED(rc)) {
        char text[160];
        std::snprintf(text, sizeof(text), "Controller service access unavailable (0x%08X). Requires CFW and firmware 13+.", rc);
        message = text;
        return;
    }
    snapshotValid = ReadConnections(connectionSnapshot);
    nextConnectionCheck = Now() + 500000000ULL;
    BtmConnectedDeviceV13 connected[8]{};
    s32 count = 0;
    rc = btmReady ? btmGetDeviceCondition(BtmProfile_Hid, connected, 8, &count)
        : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    DebugLog("btmGetDeviceCondition HID profile=%u rc=0x%08X count=%d entry_size=%zu name_offset=%zu",
        static_cast<unsigned>(BtmProfile_Hid), rc, count, sizeof(connected[0]), offsetof(BtmConnectedDeviceV13, name));
    BtmConnectedDeviceV13 allProfiles[8]{};
    s32 allCount = 0;
    const Result allResult = btmReady ? btmGetDeviceCondition(BtmProfile_None, allProfiles, 8, &allCount)
        : MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    DebugLog("Diagnostic-only enumeration ALL profiles rc=0x%08X count=%d", allResult, allCount);
    if (R_SUCCEEDED(allResult)) {
        for (s32 i = 0; i < std::clamp(allCount, 0, 8); ++i) DebugDevice("ALL", allProfiles[i]);
    }
    const s32 visibleCount = R_SUCCEEDED(rc) ? std::clamp(count, 0, 8) : 0;
    for (s32 i = 0; i < visibleCount; ++i) {
        DebugDevice("BTM comparison only", connected[i]);
    }

    HidsysUniquePadId pads[32]{};
    s32 padCount = 0;
    const Result padsResult = hidsysGetUniquePadIds(pads, 32, &padCount);
    DebugLog("hidsysGetUniquePadIds rc=0x%08X count=%d", padsResult, padCount);
    if (R_FAILED(padsResult)) {
        char text[96];
        std::snprintf(text, sizeof(text), "Cannot list HID controllers (0x%08X). See /joyoff-debug.txt.", padsResult);
        message = text;
        return;
    }
    for (s32 i = 0; i < std::clamp(padCount, 0, 32); ++i) {
        HidsysUniquePadType type{};
        HidNpadInterfaceType interfaceType{};
        const Result typeResult = hidsysGetUniquePadType(pads[i], &type);
        const Result interfaceResult = hidsysGetUniquePadInterface(pads[i], &interfaceType);
        DebugLog("HID pad=%016llX type_rc=0x%08X type=%u interface_rc=0x%08X interface=%u",
            static_cast<unsigned long long>(pads[i].id), typeResult, static_cast<unsigned>(type),
            interfaceResult, static_cast<unsigned>(interfaceType));
        if (R_FAILED(typeResult)) {
            DebugLog("HID index=%d skipped: cannot read controller type", i);
            continue;
        }
        if (type != HidsysUniquePadType_LeftController && type != HidsysUniquePadType_RightController) {
            DebugLog("HID index=%d skipped: not a left/right controller", i);
            continue;
        }
        const u8 side = type == HidsysUniquePadType_LeftController ? 1 : 2;
        bool connectedNow = false, wiredNow = false;
        const bool transportKnown = ReadTransport(pads[i], side, connectedNow, wiredNow, true);
        DebugLog("HID index=%d active_transport_known=%d connected=%d wired=%d metadata_interface=%u",
            i, transportKnown, connectedNow, wiredNow, static_cast<unsigned>(interfaceType));
        if (!transportKnown || !connectedNow || wiredNow) {
            JoyTarget target;
            target.padId = pads[i];
            target.side = side;
            target.wired = wiredNow;
            target.name = side == 1 ? "Joy-Con (L)" : "Joy-Con (R)";
            target.done = true;
            target.status = target.wired ? "Attached to tablet. Detach to turn off."
                : "Waiting for an active Bluetooth connection...";
            targets.push_back(target);
            continue;
        }
        BtdrvAddress address{};
        const Result addressResult = hidsysGetUniquePadBluetoothAddress(pads[i], &address);
        DebugLog("HID index=%d Bluetooth address rc=0x%08X", i, addressResult);
        if (R_FAILED(addressResult)) continue;
        DebugBytes("HID Bluetooth address", &address, sizeof(address));
        const BtdrvAddress empty{};
        if (SameAddress(address, empty)) {
            DebugLog("HID index=%d skipped: Bluetooth address is zero", i);
            continue;
        }
        if (std::any_of(targets.begin(), targets.end(), [&](const JoyTarget& t) {
            return SameAddress(t.address, address);
        })) {
            DebugLog("HID index=%d skipped: duplicate Bluetooth address", i);
            continue;
        }
        DebugLog("HID index=%d accepted as identity candidate", i);
        JoyTarget target;
        target.padId = pads[i];
        target.address = address;
        target.side = type == HidsysUniquePadType_LeftController ? 1 : 2;
        target.name = target.side == 1 ? "Joy-Con (L)" : "Joy-Con (R)";
        target.status = "Checking controller identity...";
        ReadColors(target);
        targets.push_back(target);
    }
    const auto wirelessCount = std::count_if(targets.begin(), targets.end(), [](const JoyTarget& t) { return !t.done; });
    diagnostics = "HID pads: " + std::to_string(padCount) + "; active wireless L/R: " + std::to_string(wirelessCount) +
        "; BTM comparison: " + (R_SUCCEEDED(rc) ? std::to_string(visibleCount) : "unavailable");
    DebugLog("Detection: %s", diagnostics.c_str());
    if (targets.empty()) {
        message = "No Bluetooth L/R controllers reported by HID. Detach Joy-Cons and Refresh; see /joyoff-debug.txt.";
        DebugLog("Detection stopped: %s", message.c_str());
        return;
    }
    if (!wirelessCount) {
        message = "Detach Joy-Cons and connect wirelessly. The list refreshes automatically; touch controls remain available.";
        DebugLog("Waiting: %s", message.c_str());
        return;
    }
    if (!Observe()) return;
    identifying = running = true;
    message = "Identifying Joy-Con L/R. Only Device Info requests are sent during detection.";
    for (auto& target : targets) {
        if (target.done) continue;
        target.counter = static_cast<u8>(armGetSystemTick() & 0x0f);
        target.phase = 1;
        Send(target, 0x02, 0);
    }
}

bool JoyBackend::Ready() const {
    return mapped && !running && std::any_of(targets.begin(), targets.end(), [](const JoyTarget& t) {
        return !t.failed && !t.done && t.side != 0 && t.phase == 0;
    });
}

bool JoyBackend::ReadConnections(std::string& snapshot) {
    HidsysUniquePadId pads[32]{};
    s32 count = 0;
    if (R_FAILED(hidsysGetUniquePadIds(pads, 32, &count))) return false;
    count = std::clamp(count, 0, 32);
    std::sort(pads, pads + count, [](const HidsysUniquePadId& a, const HidsysUniquePadId& b) { return a.id < b.id; });
    std::string current;
    for (s32 i = 0; i < count; ++i) {
        HidsysUniquePadType type{};
        HidNpadInterfaceType interfaceType{};
        if (R_FAILED(hidsysGetUniquePadType(pads[i], &type))) return false;
        const Result interfaceResult = hidsysGetUniquePadInterface(pads[i], &interfaceType);
        current += std::to_string(pads[i].id) + ":" + std::to_string(static_cast<unsigned>(type)) +
            ":" + std::to_string(interfaceResult) + ":" + std::to_string(static_cast<unsigned>(interfaceType)) + ";";
        if (type == HidsysUniquePadType_LeftController || type == HidsysUniquePadType_RightController) {
            bool connected = false, wired = false;
            if (!ReadTransport(pads[i], type == HidsysUniquePadType_LeftController ? 1 : 2, connected, wired)) return false;
            current += connected ? "connected:" : "disconnected:";
            current += wired ? "wired;" : "wireless;";
        }
    }
    snapshot = current;
    return true;
}

void JoyBackend::AutoRefresh(bool paused) {
    if (paused || running || !hidReady || Now() < nextConnectionCheck) return;
    nextConnectionCheck = Now() + 500000000ULL;
    std::string current;
    if (!ReadConnections(current)) return;
    if (!snapshotValid) {
        DebugLog("Auto-refresh: active controller state is available again");
        Refresh();
    } else if (current != connectionSnapshot) {
        DebugLog("Auto-refresh: HID connection/type/interface changed; old=%s new=%s",
            connectionSnapshot.c_str(), current.c_str());
        Refresh();
    }
}

void JoyBackend::ExplainUnavailable() {
    if (running) message = identifying ? "Checking controller identity. Wait for detection to finish."
        : "Turn Off is already running. Wait for the per-controller results.";
    else if (targets.empty()) message = "No wireless Joy-Con ready. Detach and connect them; detection refreshes automatically.";
    else {
        const auto failed = std::find_if(targets.begin(), targets.end(), [](const JoyTarget& t) { return t.failed; });
        message = failed != targets.end() ? failed->name + ": " + failed->status + " Press X / Refresh to retry detection."
            : std::any_of(targets.begin(), targets.end(), [](const JoyTarget& t) { return t.wired; })
                ? "Detach Joy-Cons from the tablet first. Wait for Bluetooth connection and automatic detection."
                : "No controller ready. Connect wirelessly; press X / Refresh to retry detection.";
    }
    DebugLog("Minus unavailable: %s", message.c_str());
}

bool JoyBackend::Observe() {
    const s32 offset = __atomic_load_n(&ring->write_offset, __ATOMIC_ACQUIRE);
    observedPackets = observedData = observedReplies = ringProblems = 0;
    DebugLog("Observe: initialized=%u write=%d read=%d capacity=%zu",
        static_cast<unsigned>(ring->initialized), offset,
        __atomic_load_n(&ring->read_offset, __ATOMIC_ACQUIRE), sizeof(ring->data));
    if (offset < 0 || static_cast<size_t>(offset) >= sizeof(ring->data)) {
        message = "Unsupported Bluetooth report buffer. No commands sent.";
        for (auto& target : targets) {
            if (!target.done) Fail(target, message.c_str());
        }
        return false;
    }
    cursor = static_cast<size_t>(offset);
    return true;
}

void JoyBackend::Fail(JoyTarget& target, const char* text, Result rc) {
    const auto* address = reinterpret_cast<const u8*>(&target.address);
    DebugLog("FAIL address=%02X:%02X:%02X:%02X:%02X:%02X name=%s phase=%d side=%u retries=%u rc=0x%08X reason=%s packets=%u data=%u replies=%u ring_errors=%u cursor=%zu write=%d",
        address[0], address[1], address[2], address[3], address[4], address[5],
        target.name.c_str(), target.phase, static_cast<unsigned>(target.side), static_cast<unsigned>(target.identityRetries), rc, text,
        observedPackets, observedData, observedReplies, ringProblems, cursor,
        __atomic_load_n(&ring->write_offset, __ATOMIC_ACQUIRE));
    target.failed = true;
    target.done = true;
    target.status = text;
    if (R_FAILED(rc)) {
        char code[32];
        std::snprintf(code, sizeof(code), " (0x%08X)", rc);
        target.status += code;
    }
}

bool JoyBackend::Send(JoyTarget& target, u8 command, u8 argument) {
    BtdrvHidReport report{};
    report.size = 49;
    report.data[0] = 0x01;
    report.data[1] = target.counter++ & 0x0f;
    const u8 neutral[] = {0x00, 0x01, 0x40, 0x40};
    std::memcpy(report.data + 2, neutral, sizeof(neutral));
    std::memcpy(report.data + 6, neutral, sizeof(neutral));
    report.data[10] = command;
    report.data[11] = argument;
    target.sentTick = armGetSystemTick();
    target.deadline = Now() + (command == 0x06 ? disconnectTimeoutNs : timeoutNs);
    const Result rc = btdrvWriteHidData(target.address, &report);
    const auto* address = reinterpret_cast<const u8*>(&target.address);
    DebugLog("SEND address=%02X:%02X:%02X:%02X:%02X:%02X name=%s phase=%d subcmd=%02X arg=%02X counter=%u rc=0x%08X",
        address[0], address[1], address[2], address[3], address[4], address[5], target.name.c_str(),
        target.phase, command, argument, static_cast<unsigned>(report.data[1]), rc);
    if (R_FAILED(rc)) {
        Fail(target, target.phase <= 1 ? "Identity request failed; no changes sent."
            : target.phase == 2 ? "Pairing reset send failed; state unconfirmed."
            : target.phase == 3 ? "Pairing cleared; low power send failed."
            : "Low power set; disconnect send failed.", rc);
        return false;
    }
    return true;
}

void JoyBackend::Start() {
    DebugLog("Turn Off requested: ready=%d candidates=%zu", Ready(), targets.size());
    if (!Ready()) { ExplainUnavailable(); return; }
    std::string current;
    if (!ReadConnections(current)) {
        message = "Cannot recheck controller connections. Press X / Refresh; no Turn Off commands sent.";
        DebugLog("Turn Off cancelled: connection recheck failed");
        return;
    }
    if (!snapshotValid || current != connectionSnapshot) {
        DebugLog("Turn Off cancelled: connections changed during confirmation; refreshing detection");
        Refresh();
        return;
    }
    if (!Observe()) return;
    identifying = false;
    running = true;
    message = "Working. Keep the Joy-Cons detached. Results appear below.";
    for (auto& target : targets) {
        target.attempted = false;
        if (target.failed || target.done || !target.side || target.phase != 0) continue;
        target.attempted = true;
        target.failed = target.done = false;
        target.counter = static_cast<u8>(armGetSystemTick() & 0x0f);
        target.identityRetries = 0;
        target.phase = 1;
        target.status = "Checking controller identity...";
        Send(target, 0x02, 0);
    }
}

void JoyBackend::Response(const BtdrvHidReportEventInfo& event, size_t size, u64 tick) {
    const auto& data = event.data_report.v9;
    const size_t reportOffset = offsetof(BtdrvHidReportEventInfo, data_report.v9.report.data);
    if (size < reportOffset + 15 || data.res != 0 || data.report.size < 15 ||
        data.report.size > sizeof(data.report.data) || data.report.size > size - reportOffset) {
        if (observedData <= 8) DebugLog("RX ignored: size/layout invalid event_size=%zu report_offset=%zu res=0x%08X report_size=%u",
            size, reportOffset, data.res, static_cast<unsigned>(data.report.size));
        return;
    }
    const auto* reply = data.report.data;
    if (reply[0] != 0x21) return;
    ++observedReplies;
    for (auto& target : targets) {
        if (target.done || !SameAddress(target.address, data.addr) || tick < target.sentTick ||
            reply[14] != Expected(target.phase)) continue;
        const auto* address = reinterpret_cast<const u8*>(&data.addr);
        DebugLog("ACK address=%02X:%02X:%02X:%02X:%02X:%02X phase=%d subcmd=%02X ack=%02X",
            address[0], address[1], address[2], address[3], address[4], address[5], target.phase, reply[14], reply[13]);
        if (!(reply[13] & 0x80)) {
            DebugBytes("Rejected command reply", reply, data.report.size);
            Fail(target, "Controller rejected command; stopped.");
            continue;
        }
        if (target.phase == 1) {
            if (data.report.size < 25 || (reply[17] != 1 && reply[17] != 2) ||
                (target.side && reply[17] != target.side)) {
                Fail(target, "Identity mismatch; no changes sent.");
                continue;
            }
            target.side = reply[17];
            target.name = target.side == 1 ? "Joy-Con (L)" : "Joy-Con (R)";
            DebugLog("Identity verified: %s type=%u", target.name.c_str(), static_cast<unsigned>(target.side));
            if (identifying && !target.colorsKnown) ReadColors(target);
            if (identifying) {
                target.phase = 0;
                target.status = "Identity verified. Ready to turn off.";
                continue;
            }
            target.phase = 2;
            target.status = "Clearing saved pairing...";
            Send(target, 0x07, 0);
        } else if (target.phase == 2) {
            target.phase = 3;
            target.status = "Pairing cleared. Setting low power...";
            Send(target, 0x08, 1);
        } else if (target.phase == 3) {
            if (data.report.size < 16 || reply[15] != 0) {
                Fail(target, "Pairing cleared; low power rejected/unconfirmed.");
                continue;
            }
            target.phase = 4;
            target.status = "Low power acknowledged. Disconnecting...";
            Send(target, 0x06, 0);
        }
    }
}

void JoyBackend::Receive() {
    struct Header { u8 type; u8 pad[7]; u64 tick; u64 size; };
    static_assert(sizeof(Header) == 0x18);
    const size_t capacity = sizeof(ring->data);
    const s32 write = __atomic_load_n(&ring->write_offset, __ATOMIC_ACQUIRE);
    if (write < 0 || static_cast<size_t>(write) >= capacity) {
        if (++ringProblems <= 8) DebugLog("RX invalid write offset=%d capacity=%zu", write, capacity);
        return;
    }
    size_t budget = capacity;
    while (cursor != static_cast<size_t>(write) && budget >= sizeof(Header)) {
        if (capacity - cursor < sizeof(Header)) { cursor = 0; budget -= sizeof(Header); continue; }
        Header header{};
        std::memcpy(&header, ring->data + cursor, sizeof(header));
        if (header.type == 0xff) { cursor = 0; budget -= sizeof(Header); continue; }
        if (header.size > sizeof(BtdrvHidReportEventInfo) || header.size > capacity - cursor - sizeof(Header)) {
            if (++ringProblems <= 8) DebugLog("RX malformed packet cursor=%zu write=%d type=%u size=%llu",
                cursor, write, static_cast<unsigned>(header.type), static_cast<unsigned long long>(header.size));
            cursor = static_cast<size_t>(write);
            return;
        }
        BtdrvHidReportEventInfo event{};
        std::memcpy(&event, ring->data + cursor + sizeof(Header), header.size);
        Header verify{};
        std::memcpy(&verify, ring->data + cursor, sizeof(verify));
        if (std::memcmp(&header, &verify, sizeof(header)) != 0) {
            if (++ringProblems <= 8) DebugLog("RX packet changed while reading cursor=%zu write=%d", cursor, write);
            cursor = static_cast<size_t>(write);
            return;
        }
        const size_t advance = sizeof(Header) + header.size;
        cursor += advance;
        if (cursor >= capacity) cursor = 0;
        budget = advance > budget ? 0 : budget - advance;
        ++observedPackets;
        if (header.type == BtdrvHidEventType_Data) {
            ++observedData;
            Response(event, header.size, header.tick);
        }
    }
}

void JoyBackend::Update() {
    if (!running) return;
    Receive();
    bool checkConnections = false;
    for (auto& target : targets) {
        if (target.done || target.phase == 0) continue;
        if (target.phase == 4) checkConnections = true;
        else if (Now() >= target.deadline) {
            if (target.phase == 1 && target.identityRetries < 2) {
                bool connected = false, wired = false;
                if (!ReadTransport(target.padId, target.side, connected, wired) || !connected || wired) {
                    Fail(target, "Wireless connection changed during identity check; no changes sent.");
                    continue;
                }
                ++target.identityRetries;
                target.status = "Retrying Device Info (attempt " + std::to_string(target.identityRetries + 1) + "/3)...";
                DebugLog("Device Info timeout: retry=%u/2 name=%s; only subcommand 02 is retried",
                    static_cast<unsigned>(target.identityRetries), target.name.c_str());
                Send(target, 0x02, 0);
                continue;
            }
            Fail(target, target.phase == 1 ? "Identity ACK missing; no changes sent."
                : target.phase == 2 ? "Pairing reset ACK missing; state unconfirmed."
                : "Pairing cleared; low power ACK missing.");
        }
    }
    if (checkConnections) {
        HidsysUniquePadId connected[32]{};
        s32 count = 0;
        const Result rc = hidsysGetUniquePadIds(connected, 32, &count);
        for (auto& target : targets) {
            if (target.done || target.phase != 4) continue;
            bool present = true;
            Result checkResult = rc;
            if (R_SUCCEEDED(rc)) {
                present = std::any_of(connected, connected + std::clamp(count, 0, 32),
                    [&](const HidsysUniquePadId& id) { return id.id == target.padId.id; });
                if (present) {
                    bool connectedNow = false, wiredNow = false;
                    if (ReadTransport(target.padId, target.side, connectedNow, wiredNow))
                        present = connectedNow && !wiredNow;
                    else checkResult = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
                }
            }
            if (!present) {
                const auto* address = reinterpret_cast<const u8*>(&target.address);
                DebugLog("Disconnect confirmed: %s address=%02X:%02X:%02X:%02X:%02X:%02X",
                    target.name.c_str(), address[0], address[1], address[2], address[3], address[4], address[5]);
                target.done = true;
                target.status = "Low power acknowledged; disconnected.";
            } else if (Now() >= target.deadline) {
                Fail(target, "Low power set; disconnect not confirmed.", checkResult);
            }
        }
    }
    running = std::any_of(targets.begin(), targets.end(), [](const JoyTarget& t) { return !t.done && t.phase != 0; });
    if (!running) {
        if (identifying) {
            identifying = false;
            message = Ready() ? "Ready. Turn Off All clears pairing and enables shipment low power mode."
                : "No Joy-Con identity verified. See each device result; no pairing changes were sent.";
        } else {
            unsigned succeeded = 0, failed = 0, skipped = 0;
            for (const auto& target : targets) {
                if (!target.attempted) ++skipped;
                else if (target.done && !target.failed && target.phase == 4) ++succeeded;
                else ++failed;
            }
            message = "Finished: " + std::to_string(succeeded) + " succeeded, " + std::to_string(failed) +
                " failed, " + std::to_string(skipped) + " skipped. ";
            message += failed ? "See Diagnostics for device results. Refresh before retrying."
                : "Attach to a Switch to wake and pair again.";
            // Keep the operation results visible after the disconnections we requested.
            snapshotValid = ReadConnections(connectionSnapshot);
            nextConnectionCheck = Now() + 500000000ULL;
        }
        DebugLog("Operation finished: %s", message.c_str());
    }
}
