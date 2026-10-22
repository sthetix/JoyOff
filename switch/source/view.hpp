#pragma once

enum class CardState { Missing, Attached, Waiting, Ready, Working, Off, Error };

struct CardSummary {
    CardState state = CardState::Missing;
    unsigned count = 0;
    bool colorsKnown = false;
    u32 bodyColor = 0;
    u32 buttonsColor = 0;
};

CardSummary Summarize(const JoyBackend& backend, u8 side) {
    CardSummary summary;
    unsigned attached = 0, waiting = 0, ready = 0, working = 0, off = 0, errors = 0;
    for (const auto& target : backend.Targets()) {
        if (target.side != side) continue;
        ++summary.count;
        if (target.failed) ++errors;
        else if (target.wired) ++attached;
        else if (!target.done && target.phase != 0) ++working;
        else if (target.done && target.phase == 4) ++off;
        else if (!target.done && target.phase == 0) ++ready;
        else ++waiting;
    }
    summary.state = working ? CardState::Working : errors ? CardState::Error
        : ready ? CardState::Ready : attached ? CardState::Attached
        : waiting ? CardState::Waiting : off ? CardState::Off : CardState::Missing;
    return summary;
}

NVGcolor StateColor(CardState state) {
    return state == CardState::Error ? Theme::Error : state == CardState::Attached ? Theme::Warning
        : state == CardState::Ready || state == CardState::Off || state == CardState::Working
        ? Theme::Accent : Theme::Muted;
}

std::vector<const JoyTarget*> SideTargets(const JoyBackend& backend, u8 side) {
    std::vector<const JoyTarget*> result;
    for (const auto& target : backend.Targets()) if (target.side == side) result.push_back(&target);
    std::sort(result.begin(), result.end(), [](const JoyTarget* a, const JoyTarget* b) {
        const int addressOrder = std::memcmp(&a->address, &b->address, sizeof(a->address));
        return addressOrder ? addressOrder < 0 : a->padId.id < b->padId.id;
    });
    return result;
}

unsigned ControllerPageCount(const JoyBackend& backend) {
    return std::max(1u, static_cast<unsigned>(std::max(SideTargets(backend, 1).size(), SideTargets(backend, 2).size())));
}

constexpr float LegendCellWidth = 48;
constexpr float LegendTop = 589;
constexpr float LegendBottom = 632;

float ControllerLegendStart(unsigned pages) {
    return 640 - pages * LegendCellWidth * 0.5f;
}

unsigned ReadyCount(const JoyBackend& backend) {
    return static_cast<unsigned>(std::count_if(backend.Targets().begin(), backend.Targets().end(), [](const JoyTarget& target) {
        return !target.failed && !target.done && !target.wired && target.side && target.phase == 0;
    }));
}

void Circle(NVGcontext* vg, float x, float y, float radius, NVGcolor color) {
    nvgBeginPath(vg);
    nvgCircle(vg, x, y, radius);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void Line(NVGcontext* vg, float x1, float y1, float x2, float y2, NVGcolor color, float width = 2) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, x1, y1);
    nvgLineTo(vg, x2, y2);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
}

#include "joycon_art.hpp"

void TextBox(NVGcontext* vg, float x, float y, float width, const char* text, float size, NVGcolor color) {
    nvgFontFace(vg, "switch-standard");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgTextBox(vg, x, y, width, text, nullptr);
}

void Button(NVGcontext* vg, float x, float y, float w, const char* key, const char* label, bool active, bool primary = false) {
    Rectangle(vg, x, y, w, 52, primary && active ? Theme::Accent : Theme::Selected, 12);
    const NVGcolor ink = primary && active ? Theme::Background : active ? Theme::Foreground : Theme::Muted;
    Rectangle(vg, x + 14, y + 12, 28, 28, primary && active ? nvgRGBA(0, 0, 0, 20) : Theme::Panel, 7);
    Text(vg, x + 22, y + 15, key, 18, ink);
    Text(vg, x + 54, y + 15, label, 19, ink);
}

void DrawCard(NVGcontext* vg, const JoyBackend& backend, u8 side, float x, unsigned page) {
    const auto devices = SideTargets(backend, side);
    const JoyTarget* target = page < devices.size() ? devices[page] : nullptr;
    CardSummary summary;
    if (target) {
        summary.count = 1;
        summary.colorsKnown = target->colorsKnown;
        summary.bodyColor = target->bodyColor;
        summary.buttonsColor = target->buttonsColor;
        summary.state = target->failed ? CardState::Error : target->wired ? CardState::Attached
            : !target->done && target->phase != 0 ? CardState::Working
            : target->done && target->phase == 4 ? CardState::Off
            : !target->done && target->phase == 0 ? CardState::Ready : CardState::Waiting;
    }
    const auto state = summary.state;
    const NVGcolor color = StateColor(state);
    Rectangle(vg, x, 142, 568, 350, Theme::Panel, 20);
    const float textX = x + (side == 1 ? 26 : 160);
    const float iconX = x + (side == 1 ? 450 : 32);
    char label[96];
    if (target) std::snprintf(label, sizeof(label), "%s JOY-CON  #%u", side == 1 ? "LEFT" : "RIGHT", page + 1);
    else std::snprintf(label, sizeof(label), "%s JOY-CON", side == 1 ? "LEFT" : "RIGHT");
    Text(vg, textX, 164, label, 16, Theme::Muted);
    Circle(vg, textX + 328, 176, 5, color);
    JoyConIcon(vg, iconX, 193, 0.96f, side == 2, summary.colorsKnown, summary.bodyColor, summary.buttonsColor);
    const bool emptyPageSlot = !target && ControllerPageCount(backend) > 1;
    const char* title = emptyPageSlot ? "No device on page" : state == CardState::Attached ? "Detach first" : state == CardState::Ready ? "Ready"
        : state == CardState::Working ? "Working..." : state == CardState::Off ? "Low power"
        : state == CardState::Error ? "Needs attention" : state == CardState::Waiting ? "Connecting..." : "Not connected";
    Text(vg, textX, 226, title, 28, color);
    const char* note = emptyPageSlot ? "No controller on this side of this page. Use L / R to view other devices."
        : state == CardState::Attached ? "Remove from the console rails. Detection updates automatically."
        : state == CardState::Ready ? "Connected over Bluetooth. Ready for storage."
        : state == CardState::Working ? "Please wait while the controller is checked or turned off."
        : state == CardState::Off ? "Shipment low power acknowledged. Bluetooth disconnected."
        : state == CardState::Error ? "A step could not be confirmed. Open Diagnostics for the result."
        : state == CardState::Waiting ? "Waiting for an active wireless connection."
        : "Connect a Joy-Con wirelessly. This screen updates automatically.";
    TextBox(vg, textX, 276, 350, note, 20, Theme::Muted);
    if (state == CardState::Off) {
        Circle(vg, textX + 15, 395, 15, Theme::Selected);
        Line(vg, textX + 8, 395, textX + 13, 400, Theme::Accent, 3);
        Line(vg, textX + 13, 400, textX + 22, 389, Theme::Accent, 3);
        Text(vg, textX + 40, 385, "Ready to put away", 18, Theme::Accent);
    } else if (state == CardState::Ready) {
        Rectangle(vg, textX, 383, 160, 32, Theme::Selected, 16);
        Text(vg, textX + 16, 389, "Bluetooth ready", 16, Theme::Accent);
    }
    if (summary.count && !summary.colorsKnown)
        Text(vg, textX, 429, "Color unavailable", 15, Theme::Muted);
    if (target) {
        const BtdrvAddress empty{};
        if (std::memcmp(&target->address, &empty, sizeof(empty)) != 0) {
            const auto* address = reinterpret_cast<const u8*>(&target->address);
            std::snprintf(label, sizeof(label), "Device %02X:%02X:%02X:%02X:%02X:%02X",
                address[0], address[1], address[2], address[3], address[4], address[5]);
        } else std::snprintf(label, sizeof(label), "Pad ID %016llX", static_cast<unsigned long long>(target->padId.id));
        Text(vg, textX, 451, label, 14, Theme::Muted);
    }
}

void DrawDiagnostics(NVGcontext* vg, const JoyBackend& backend, const char* atmosphere, unsigned page) {
    char info[192];
    const u32 version = hosversionGet();
    std::snprintf(info, sizeof(info), "FW %u.%u.%u  |  %s", HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version), atmosphere);
    Text(vg, 56, 110, info, 17, Theme::Muted);
    Rectangle(vg, 56, 146, 1168, 390, Theme::Panel, 16);
    TextBox(vg, 80, 168, 1120, backend.Diagnostics().c_str(), 18, Theme::Muted);
    const auto& targets = backend.Targets();
    const unsigned totalPages = std::max(1u, static_cast<unsigned>((targets.size() + 3) / 4));
    page = std::min(page, totalPages - 1);
    for (size_t i = page * 4; i < std::min(targets.size(), static_cast<size_t>(page * 4 + 4)); ++i) {
        const auto& target = targets[i];
        const float y = 220 + (i % 4) * 73;
        const auto* address = reinterpret_cast<const u8*>(&target.address);
        std::snprintf(info, sizeof(info), "%s  |  %02X:%02X:%02X:%02X:%02X:%02X  |  phase %d",
            target.name.c_str(), address[0], address[1], address[2], address[3], address[4], address[5], target.phase);
        Text(vg, 80, y, info, 17, target.failed ? Theme::Error : Theme::Foreground);
        TextBox(vg, 80, y + 26, 1100, target.status.c_str(), 18, Theme::Muted);
        if (target.colorsKnown) {
            std::snprintf(info, sizeof(info), "Body #%02X%02X%02X  |  Buttons #%02X%02X%02X",
                target.bodyColor & 0xff, (target.bodyColor >> 8) & 0xff, (target.bodyColor >> 16) & 0xff,
                target.buttonsColor & 0xff, (target.buttonsColor >> 8) & 0xff, (target.buttonsColor >> 16) & 0xff);
        } else std::snprintf(info, sizeof(info), "Controller colors unavailable");
        Text(vg, 80, y + 48, info, 14, Theme::Muted);
    }
    if (targets.empty()) Text(vg, 80, 242, "No controller entries. Refresh to check again.", 20, Theme::Muted);
    std::snprintf(info, sizeof(info), "Page %u / %u   |   L / R: change page", page + 1, totalPages);
    Text(vg, 80, 505, info, 16, Theme::Muted);
    TextBox(vg, 56, 552, 1168, backend.Message().c_str(), 17, Theme::Muted);
    Text(vg, 56, 606, backend.LogAvailable() ? "TXT log: sdmc:/joyoff-debug.txt" : "TXT log unavailable: cannot write to SD", 16,
        backend.LogAvailable() ? Theme::Muted : Theme::Warning);
    Button(vg, 56, 644, 228, "X", "Refresh", !backend.Running());
    Button(vg, 304, 644, 580, "Y", "Back to JoyOff", true);
    Button(vg, 904, 644, 320, "B", "Back", true);
}

void DrawHome(NVGcontext* vg, const JoyBackend& backend, unsigned page) {
    Text(vg, 56, 103, "A little rest for your Joy-Cons.", 20, Theme::Muted);
    Rectangle(vg, 946, 49, 278, 32, Theme::Selected, 16);
    Text(vg, 967, 55, "LOW POWER / STORAGE", 15, Theme::Accent);
    const unsigned pages = ControllerPageCount(backend);
    page = std::min(page, pages - 1);
    DrawCard(vg, backend, 1, 56, page);
    DrawCard(vg, backend, 2, 656, page);
    const auto left = Summarize(backend, 1).state;
    const auto right = Summarize(backend, 2).state;
    const bool error = left == CardState::Error || right == CardState::Error;
    const bool off = (left == CardState::Off || right == CardState::Off) &&
        (left == CardState::Off || left == CardState::Missing) &&
        (right == CardState::Off || right == CardState::Missing);
    const bool attached = left == CardState::Attached || right == CardState::Attached;
    const char* lead = backend.Running() ? "Taking care of your controllers..." : error ? "Check the controller result"
        : backend.Ready() ? "Ready when you are" : off ? "All set for storage" : attached ? "Detach your Joy-Cons first" : "Waiting for your Joy-Cons";
    const char* hint = backend.Running() ? "Keep the controllers detached until this operation finishes."
        : error ? "Press Y for Diagnostics. You can refresh to retry detection."
        : backend.Ready() ? "Press - or tap Turn Off All, then confirm. Saved pairing will be cleared."
        : off ? "Attach to a Switch to wake and pair again. Tap Exit when finished."
        : "Connect over Bluetooth. Detection refreshes automatically.";
    Text(vg, 56, 519, lead, 23, error ? Theme::Warning : Theme::Foreground);
    const bool hasResult = !backend.Running() && std::any_of(backend.Targets().begin(), backend.Targets().end(),
        [](const JoyTarget& target) { return target.attempted; });
    TextBox(vg, 56, 553, 1168, hasResult ? backend.Message().c_str() : hint, 18, Theme::Muted);
    Text(vg, 56, 605, "X: Refresh", 17, backend.Running() ? Theme::Muted : Theme::Foreground);
    if (pages > 1) {
        const auto leftDevices = SideTargets(backend, 1);
        const auto rightDevices = SideTargets(backend, 2);
        const float start = ControllerLegendStart(pages);
        Text(vg, start - 116, 602, "Controllers", 16, Theme::Muted);
        for (unsigned i = 0; i < pages; ++i) {
            const float x = start + i * LegendCellWidth;
            Rectangle(vg, x + 2, LegendTop, LegendCellWidth - 4, LegendBottom - LegendTop,
                i == page ? Theme::Selected : Theme::Panel, 8);
            if (i == page) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x + 2, LegendTop, LegendCellWidth - 4, LegendBottom - LegendTop, 8);
                nvgStrokeColor(vg, Theme::Accent);
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
            }
            const JoyTarget* leftDevice = i < leftDevices.size() ? leftDevices[i] : nullptr;
            const JoyTarget* rightDevice = i < rightDevices.size() ? rightDevices[i] : nullptr;
            if (leftDevice) JoyConIcon(vg, x + 11, 593, 0.095f, false,
                leftDevice->colorsKnown, leftDevice->bodyColor, leftDevice->buttonsColor);
            if (rightDevice) JoyConIcon(vg, x + 27, 593, 0.095f, true,
                rightDevice->colorsKnown, rightDevice->bodyColor, rightDevice->buttonsColor);
            char number[16];
            std::snprintf(number, sizeof(number), "%u", i + 1);
            Text(vg, x + 20, 621, number, 10, i == page ? Theme::Accent : Theme::Muted);
        }
        Text(vg, start + pages * LegendCellWidth + 18, 602, "Swipe / L-R", 16, Theme::Muted);
    }
    char action[64];
    if (backend.Running()) std::snprintf(action, sizeof(action), "Working...");
    else if (ReadyCount(backend)) std::snprintf(action, sizeof(action), "Turn Off All (%u)", ReadyCount(backend));
    else std::snprintf(action, sizeof(action), "Turn Off All");
    Rectangle(vg, 56, 644, 228, 52, Theme::Selected, 12);
    Text(vg, 76, 659, "Y: Diagnostics", 19);
    Button(vg, 304, 644, 580, "-", action, backend.Ready(), true);
    Button(vg, 904, 644, 320, "+", "Exit", !backend.Running());
}

void DrawConfirmation(NVGcontext* vg, const JoyBackend& backend) {
    Rectangle(vg, 0, 0, Width, Height, nvgRGBA(0, 0, 0, 210), 0);
    Rectangle(vg, 160, 170, 960, 380, Theme::Panel, 22);
    char title[96];
    std::snprintf(title, sizeof(title), "Turn off all %u ready wireless Joy-Cons?", ReadyCount(backend));
    Text(vg, 200, 205, title, 28, Theme::Accent);
    Text(vg, 200, 267, "Saved Bluetooth pairing will be cleared.", 22, Theme::Warning);
    TextBox(vg, 200, 310, 865, "The ready controllers will enter shipment low power mode and disconnect. Attach them to a Switch to wake and pair again.", 21, Theme::Foreground);
    Text(vg, 200, 397, "Includes ready controllers on every page. Touch remains available.", 18, Theme::Muted);
    Button(vg, 200, 450, 400, "B", "Cancel", true);
    Button(vg, 640, 450, 440, "-", "Confirm Turn Off", true, true);
}
