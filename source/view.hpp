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

void TextBox(NVGcontext* vg, float x, float y, float width, const char* text, float size, NVGcolor color, bool alignRight = false) {
    nvgFontFace(vg, "switch-standard");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, (alignRight ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgTextBox(vg, x, y, width, text, nullptr);
}

void Button(NVGcontext* vg, float x, float y, float w, const char* key, const char* label, bool active, bool primary = false) {
    Rectangle(vg, x, y, w, 52, primary && active ? Theme::Accent : Theme::Selected, 12);
    const NVGcolor ink = primary && active ? Theme::Background : active ? Theme::Foreground : Theme::Muted;
    Rectangle(vg, x + 14, y + 12, 28, 28, primary && active ? nvgRGBA(0, 0, 0, 20) : Theme::Panel, 7);
    Text(vg, x + (std::strlen(key) > 1 ? 18 : 22), y + 15, key, std::strlen(key) > 1 ? 14 : 18, ink);
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
    const bool left = side == 1;
    const float textX = x + (left ? 58 : 160);
    const float iconWidth = 136.512f * 0.96f * 0.7f;
    const float iconX = x + (left ? 568 - 32 - iconWidth : 32);
    const auto cardText = [&](float y, const char* text, float size, NVGcolor ink) {
        TextBox(vg, textX, y, 350, text, size, ink, left);
    };
    char label[96];
    if (target) std::snprintf(label, sizeof(label), "%s JOY-CON  #%u", side == 1 ? "LEFT" : "RIGHT", page + 1);
    else std::snprintf(label, sizeof(label), "%s JOY-CON", side == 1 ? "LEFT" : "RIGHT");
    cardText(164, label, 16, Theme::Muted);
    Circle(vg, textX + (left ? 22 : 328), 176, 5, color);
    JoyConIcon(vg, iconX, 193, 0.96f, side == 2, summary.colorsKnown && target && !target->wired && state != CardState::Off, summary.bodyColor, summary.buttonsColor);
    const bool emptyPageSlot = !target && ControllerPageCount(backend) > 1;
    const char* title = emptyPageSlot ? "No device on page" : state == CardState::Attached ? "Detach first" : state == CardState::Ready ? "Ready"
        : state == CardState::Working ? "Working..." : state == CardState::Off ? "Low power"
        : state == CardState::Error ? "Needs attention" : state == CardState::Waiting ? "Connecting..." : "Not connected";
    cardText(226, title, 28, color);
    const char* note = emptyPageSlot ? "No controller on this side of this page. Use L / R to view other devices."
        : state == CardState::Attached ? "Remove from the console rails. Detection updates automatically."
        : state == CardState::Ready ? "Connected over Bluetooth. Ready for storage."
        : state == CardState::Working ? "Please wait while the controller is checked or turned off."
        : state == CardState::Off ? "Shipment low power acknowledged. Bluetooth disconnected."
        : state == CardState::Error ? "A step could not be confirmed. Open Diagnostics for the result."
        : state == CardState::Waiting ? "Waiting for an active wireless connection."
        : "Connect a Joy-Con wirelessly. This screen updates automatically.";
    cardText(276, note, 20, Theme::Muted);
    if (state == CardState::Off) {
        const float checkX = textX + (left ? 335 : 15);
        Circle(vg, checkX, 395, 15, Theme::Selected);
        Line(vg, checkX - 7, 395, checkX - 2, 400, Theme::Accent, 3);
        Line(vg, checkX - 2, 400, checkX + 7, 389, Theme::Accent, 3);
        TextBox(vg, textX + (left ? 0 : 40), 385, 310, "Ready to put away", 18, Theme::Accent, left);
    } else if (state == CardState::Ready) {
        const float badgeX = textX + (left ? 190 : 0);
        Rectangle(vg, badgeX, 383, 160, 32, Theme::Selected, 16);
        Text(vg, badgeX + 16, 389, "Bluetooth ready", 16, Theme::Accent);
    }
    if (summary.count && !summary.colorsKnown)
        cardText(429, "Color unavailable", 15, Theme::Muted);
    if (target) {
        const BtdrvAddress empty{};
        if (std::memcmp(&target->address, &empty, sizeof(empty)) != 0) {
            const auto* address = reinterpret_cast<const u8*>(&target->address);
            std::snprintf(label, sizeof(label), "Device %02X:%02X:%02X:%02X:%02X:%02X",
                address[0], address[1], address[2], address[3], address[4], address[5]);
        } else std::snprintf(label, sizeof(label), "Pad ID %016llX", static_cast<unsigned long long>(target->padId.id));
        cardText(451, label, 14, Theme::Muted);
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
    Text(vg, 56, 606, !backend.LoggingEnabled() ? "Enable TXT logging, then X: Refresh to capture logs"
        : backend.LogAvailable() ? "sdmc:/joyoff-debug.txt" : "Cannot write TXT log to SD", 16,
        backend.LoggingEnabled() && !backend.LogAvailable() ? Theme::Warning : Theme::Muted);
    Button(vg, 56, 644, 228, "X", "Refresh", !backend.Running());
    Button(vg, 304, 644, 320, "-", backend.LoggingEnabled() ? "TXT Logging: ON" : "TXT Logging: OFF", !backend.Running());
    Button(vg, 644, 644, 328, "Y", "Back to JoyOff", true);
    Button(vg, 992, 644, 232, "B", "Back", true);
}

void DrawAbout(NVGcontext* vg) {
    Rectangle(vg, 0, 0, Width, Height, nvgRGBA(0, 0, 0, 210), 0);
    Rectangle(vg, 240, 110, 800, 500, Theme::Panel, 22);
    Text(vg, 280, 140, "About JoyOff", 32, Theme::Accent);
    Text(vg, 280, 200, "Developer: sthetix", 22);
    Text(vg, 280, 238, "Version: " JOYOFF_VERSION, 22, Theme::Muted);
    TextBox(vg, 280, 294, 720,
        "Puts original wireless Joy-Con controllers into shipment low-power mode and disconnects them for shelf storage.", 22, Theme::Foreground);
    TextBox(vg, 280, 388, 720,
        "Saved Bluetooth pairing is cleared. Attach the controllers to a Switch to wake and pair again.", 18, Theme::Muted);
    Text(vg, 280, 470, "github.com/sthetix/JoyOff", 18, Theme::Accent);
    Button(vg, 700, 530, 300, "B", "Close", true);
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
                leftDevice->colorsKnown && !leftDevice->wired && !(leftDevice->done && !leftDevice->failed && leftDevice->phase == 4), leftDevice->bodyColor, leftDevice->buttonsColor);
            if (rightDevice) JoyConIcon(vg, x + 27, 593, 0.095f, true,
                rightDevice->colorsKnown && !rightDevice->wired && !(rightDevice->done && !rightDevice->failed && rightDevice->phase == 4), rightDevice->bodyColor, rightDevice->buttonsColor);
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
    Button(vg, 56, 644, 220, "Y", "Diagnostics", true);
    Button(vg, 293, 644, 220, "ZL", "Colors", !backend.Running());
    Button(vg, 530, 644, 220, "-", action, backend.Ready(), true);
    Button(vg, 767, 644, 220, "ZR", "About", true);
    Button(vg, 1004, 644, 220, "+", "Exit", !backend.Running());
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

struct ColorFileBrowser {
    struct Entry { std::string name; bool directory; };
    bool open = false;
    std::string path = "sdmc:/switch/JoyOff";
    std::string message;
    std::vector<Entry> entries;
    size_t selected = 0;

    bool Read(const std::string& directory) {
        entries.clear();
        selected = 0;
        path = directory;
        DIR* dir = opendir(path.c_str());
        if (!dir) { message = "Cannot open folder. Use Parent to browse another location."; return false; }
        bool complete = true;
        while (true) {
            errno = 0;
            const auto* entry = readdir(dir);
            if (!entry) { complete = errno == 0; break; }
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            const std::string full = path + (path.back() == '/' ? "" : "/") + name;
            struct stat info{};
            if (stat(full.c_str(), &info) != 0) continue;
            const bool folder = S_ISDIR(info.st_mode);
            if (folder || (S_ISREG(info.st_mode) && name.size() >= 4 && name.substr(name.size() - 4) == ".bin"))
                entries.push_back({name, folder});
        }
        closedir(dir);
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.directory != b.directory ? a.directory : a.name < b.name;
        });
        message = !complete ? "Folder listing incomplete. Reopen the folder to retry."
            : entries.empty() ? "No folders or .bin files here." : "Select a folder or an original JoyOff color backup (.bin).";
        return complete;
    }
    void Open() {
        open = true;
        if (!Read("sdmc:/switch/JoyOff")) Read("sdmc:/");
    }
    void Parent() {
        if (path == "sdmc:/") return;
        const size_t slash = path.find_last_of('/');
        Read(slash <= 6 ? "sdmc:/" : path.substr(0, slash));
    }
    std::string SelectedPath() const {
        return path + (path.back() == '/' ? "" : "/") + entries[selected].name;
    }
};

struct ColorEditor {
    bool open = false;
    bool confirm = false;
    bool reload = false;
    bool advanced = false;
    int sliderChannel = -1;
    ColorFileBrowser browser;
    bool restore = false;
    std::string restorePath;
    std::array<u8, 6> beforeRestore{};
    std::string inputMessage;
    size_t target = 0;
    unsigned channel = 0;
    std::array<u8, 6> rgb{128, 128, 128, 40, 40, 40};

    u32 Body() const { return rgb[0] | (u32(rgb[1]) << 8) | (u32(rgb[2]) << 16); }
    u32 Buttons() const { return rgb[3] | (u32(rgb[4]) << 8) | (u32(rgb[5]) << 16); }
    void Select(const JoyBackend& backend, size_t index) {
        target = index;
        if (index >= backend.Targets().size()) return;
        const auto& device = backend.Targets()[index];
        if (!device.colorsKnown) { rgb = {128, 128, 128, 40, 40, 40}; return; }
        for (unsigned i = 0; i < 3; ++i) {
            rgb[i] = static_cast<u8>(device.bodyColor >> (i * 8));
            rgb[i + 3] = static_cast<u8>(device.buttonsColor >> (i * 8));
        }
    }
    void Adjust(int amount) { rgb[channel] = static_cast<u8>(std::clamp(int(rgb[channel]) + amount, 0, 255)); }
    void EditValue() {
        SwkbdConfig keyboard{};
        if (R_FAILED(swkbdCreate(&keyboard, 0))) {
            inputMessage = "Numeric keyboard unavailable. Use the +/- buttons instead.";
            return;
        }
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
        swkbdConfigSetHeaderText(&keyboard, "RGB value");
        swkbdConfigSetGuideText(&keyboard, "Enter a value from 0 to 255");
        swkbdConfigSetStringLenMin(&keyboard, 1);
        swkbdConfigSetStringLenMax(&keyboard, 3);
        char text[16]{};
        std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(rgb[channel]));
        while (true) {
            swkbdConfigSetInitialText(&keyboard, text);
            if (R_FAILED(swkbdShow(&keyboard, text, sizeof(text)))) break;
            unsigned value = 0;
            bool valid = text[0] != '\0';
            for (const char* digit = text; *digit; ++digit) {
                if (*digit < '0' || *digit > '9') { valid = false; break; }
                value = value * 10 + static_cast<unsigned>(*digit - '0');
                if (value > 255) { valid = false; break; }
            }
            if (valid) { rgb[channel] = static_cast<u8>(value); break; }
            swkbdConfigSetGuideText(&keyboard, "Invalid value. Enter digits from 0 to 255");
        }
        swkbdClose(&keyboard);
    }
    bool CanApply(const JoyBackend& backend) const {
        if (backend.Running() || target >= backend.Targets().size()) return false;
        const auto& device = backend.Targets()[target];
        return !device.wired && !device.failed && !device.done && device.phase == 0 && device.side;
    }
};

void DrawColors(NVGcontext* vg, const JoyBackend& backend, const ColorEditor& editor) {
    Button(vg, 992, 42, 232, "-", editor.advanced ? "Sliders" : "Advanced", !backend.Running());
    Rectangle(vg, 56, 116, 1168, 410, Theme::Panel, 20);
    const JoyTarget* device = editor.target < backend.Targets().size() ? &backend.Targets()[editor.target] : nullptr;
    char label[128];
    if (device) std::snprintf(label, sizeof(label), "L / R: Controller %u of %u - %s",
        static_cast<unsigned>(editor.target + 1), static_cast<unsigned>(backend.Targets().size()), device->name.c_str());
    else std::snprintf(label, sizeof(label), "No Joy-Con detected. Connect and Refresh.");
    Text(vg, 86, 136, label, 23, Theme::Accent);
    Text(vg, 86, 174, device && device->wired ? "Attached: detach and connect over Bluetooth, then Refresh."
        : "Apply requires a verified wireless Joy-Con. Keep it connected until verification finishes.", 18, Theme::Muted);
    JoyConIcon(vg, 155, 225, 0.85f, device && device->side == 2, true, editor.Body(), editor.Buttons());
    char hex[80];
    std::snprintf(hex, sizeof(hex), "Body #%02X%02X%02X   Buttons #%02X%02X%02X",
        editor.rgb[0], editor.rgb[1], editor.rgb[2], editor.rgb[3], editor.rgb[4], editor.rgb[5]);
    Text(vg, 86, 490, hex, 18);
    const char* names[] = {"Body Red", "Body Green", "Body Blue", "Buttons Red", "Buttons Green", "Buttons Blue"};
    for (unsigned i = 0; i < editor.rgb.size(); ++i) {
        const float y = 218 + i * 46;
        Rectangle(vg, 550, y, 632, 40, i == editor.channel ? Theme::Selected : Theme::Panel, 8);
        Text(vg, 568, y + 8, names[i], 20, i == editor.channel ? Theme::Accent : Theme::Foreground);
        char value[8];
        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(editor.rgb[i]));
        if (editor.advanced) {
            Rectangle(vg, 846, y + 3, 64, 34, Theme::Selected, 6);
            Text(vg, 871, y + 6, "-", 24);
            Rectangle(vg, 924, y + 3, 114, 34, Theme::Background, 6);
            Text(vg, 948, y + 8, value, 20);
            Rectangle(vg, 1054, y + 3, 64, 34, Theme::Selected, 6);
            Text(vg, 1077, y + 6, "+", 24);
        } else {
            const unsigned shift = (i % 3) * 8;
            const u32 low = (i < 3 ? editor.Body() : editor.Buttons()) & ~(0xffu << shift);
            const u32 high = low | (0xffu << shift);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, 800, y + 15, 300, 10, 5);
            nvgFillPaint(vg, nvgLinearGradient(vg, 800, y + 20, 1100, y + 20,
                ControllerColor(low), ControllerColor(high)));
            nvgFill(vg);
            const float thumb = 800 + editor.rgb[i] * 300.0f / 255;
            Circle(vg, thumb, y + 20, 12, Theme::Foreground);
            Circle(vg, thumb, y + 20, 8, i == editor.channel ? Theme::Accent : Theme::Selected);
            Text(vg, 1128, y + 8, value, 20);
        }
    }
    TextBox(vg, 56, 540, 1168, editor.inputMessage.empty() ? backend.ColorMessage().c_str() : editor.inputMessage.c_str(), 17, Theme::Muted);
    Text(vg, 56, 610, editor.advanced
        ? "Tap a number to type (0-255)   Left/Right: +/-1   ZL/ZR: +/-16   Minus: Sliders"
        : "Drag RGB sliders to preview   L/R: controller   Minus: Advanced (+/-)", 17);
    Button(vg, 56, 644, 220, "X", "Refresh", !backend.Running());
    Button(vg, 293, 644, 220, "Y", "Backup Colors", editor.CanApply(backend));
    Button(vg, 530, 644, 220, "+", "Restore Colors", editor.CanApply(backend));
    Button(vg, 767, 644, 220, "A", backend.Running() ? "Working..." : "Apply Colors", editor.CanApply(backend), true);
    Button(vg, 1004, 644, 220, "B", "Back", !backend.Running());
    if (editor.confirm) {
        Rectangle(vg, 0, 0, Width, Height, nvgRGBA(0, 0, 0, 210), 0);
        Rectangle(vg, 160, 170, 960, 380, Theme::Panel, 22);
        Text(vg, 200, 205, editor.restore ? "Restore these colors to this Joy-Con?" : "Apply colors to this Joy-Con?", 28, Theme::Accent);
        if (editor.restore) {
            nvgSave(vg);
            nvgScissor(vg, 200, 250, 745, 52);
            TextBox(vg, 200, 250, 745, editor.restorePath.c_str(), 16, Theme::Muted);
            nvgRestore(vg);
            JoyConIcon(vg, 1000, 260, 0.35f, device && device->side == 2, true, editor.Body(), editor.Buttons());
        }
        TextBox(vg, 200, editor.restore ? 313 : 267, editor.restore ? 745 : 865, editor.restore
            ? "The preview shows the selected backup. Current colors will be backed up before restoring."
            : "This permanently changes the body and button colors stored in the selected Joy-Con. Original colors are backed up to /switch/JoyOff/ before writing.", 22, Theme::Foreground);
        TextBox(vg, 200, 370, 865, "Keep the Joy-Con wireless and connected. System colors may refresh after reconnecting.", 20, Theme::Warning);
        Button(vg, 200, 450, 400, "B", "Cancel", true);
        Button(vg, 640, 450, 440, "A", editor.restore ? "Confirm Restore" : "Confirm Apply", true, true);
    }
    if (editor.browser.open) {
        const auto& browser = editor.browser;
        Rectangle(vg, 0, 0, Width, Height, Theme::Background, 0);
        Text(vg, 56, 37, "Select color backup", 36, Theme::Accent);
        Rectangle(vg, 56, 116, 1168, 500, Theme::Panel, 20);
        nvgSave(vg);
        nvgScissor(vg, 80, 132, 1120, 48);
        TextBox(vg, 80, 132, 1120, browser.path.c_str(), 18, Theme::Muted);
        nvgRestore(vg);
        const size_t first = browser.selected / 7 * 7;
        for (size_t i = first; i < browser.entries.size() && i < first + 7; ++i) {
            const float y = 190 + (i - first) * 48;
            Rectangle(vg, 76, y, 1128, 44, i == browser.selected ? Theme::Selected : Theme::Panel, 8);
            const std::string name = (browser.entries[i].directory ? "[Folder] " : "") + browser.entries[i].name;
            nvgSave(vg);
            nvgScissor(vg, 90, y, 1100, 44);
            Text(vg, 94, y + 11, name.c_str(), 19, i == browser.selected ? Theme::Accent : Theme::Foreground);
            nvgRestore(vg);
        }
        TextBox(vg, 80, 540, 1120, browser.message.c_str(), 18, Theme::Muted);
        Button(vg, 56, 644, 220, "L", "Previous", first != 0);
        Button(vg, 293, 644, 220, "R", "Next", first + 7 < browser.entries.size());
        Button(vg, 530, 644, 220, "X", "Parent", browser.path != "sdmc:/");
        Button(vg, 767, 644, 220, "A", "Open / Select", !browser.entries.empty(), true);
        Button(vg, 1004, 644, 220, "B", "Cancel", true);
    }
}
