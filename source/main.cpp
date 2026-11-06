// Display initialization and drawing helpers adapted from Switch 90DNS Tester.
// Original project: Nexrem and contributors; distributed under LGPLv3 (COPYING).
#include <switch.h>
#include <nanovg_dk.h>
#include <nanovg/framework/CMemPool.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <optional>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include "backend.hpp"

namespace {

constexpr unsigned Width = 1280, Height = 720;

namespace Theme {
const NVGcolor Background = nvgRGB(17, 31, 40);
const NVGcolor Panel = nvgRGB(18, 36, 48);
const NVGcolor Selected = nvgRGB(20, 49, 68);
const NVGcolor Line = nvgRGB(51, 94, 119);
const NVGcolor Foreground = nvgRGB(251, 251, 251);
const NVGcolor Muted = nvgRGB(190, 208, 214);
const NVGcolor Accent = nvgRGB(50, 255, 207);
const NVGcolor Blocked = nvgRGB(105, 255, 143);
const NVGcolor Error = nvgRGB(250, 58, 93);
const NVGcolor Warning = nvgRGB(255, 202, 105);
}

void Rectangle(NVGcontext *vg, float x, float y, float w, float h, NVGcolor color, float radius = 4) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void Text(NVGcontext *vg, float x, float y, const char *text, float size = 20, NVGcolor color = Theme::Foreground) {
    nvgFontFace(vg, "switch-standard");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgText(vg, x, y, text, nullptr);
}

#include "view.hpp"

class Display {
    char atmosphere[48] = "AMS unknown | ?";
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    dk::UniqueCmdBuf commands;
    dk::UniqueSwapchain swapchain;
    std::optional<CMemPool> images, code, data;
    CMemPool::Handle commandMemory, stencilMemory;
    std::array<CMemPool::Handle, 2> framebufferMemory;
    dk::Image stencil;
    std::array<dk::Image, 2> framebuffers;
    std::optional<nvg::DkRenderer> renderer;
    NVGcontext *vg = nullptr;

public:
    Display() {
        if (R_SUCCEEDED(splInitialize())) {
            u64 amsVersion = 0, emummcType = 0;
            const Result versionResult = splGetConfig(static_cast<SplConfigItem>(65000), &amsVersion);
            const Result modeResult = splGetConfig(static_cast<SplConfigItem>(65007), &emummcType);
            const char mode = R_SUCCEEDED(modeResult) ? (emummcType != 0 ? 'E' : 'S') : '?';
            if (R_SUCCEEDED(versionResult)) {
                snprintf(atmosphere, sizeof(atmosphere), "AMS %u.%u.%u | %c",
                    static_cast<unsigned>((amsVersion >> 56) & 0xFF),
                    static_cast<unsigned>((amsVersion >> 48) & 0xFF),
                    static_cast<unsigned>((amsVersion >> 40) & 0xFF), mode);
            } else {
                snprintf(atmosphere, sizeof(atmosphere), "AMS unknown | %c", mode);
            }
            splExit();
        }
        device = dk::DeviceMaker{}.create();
        queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();
        images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 24*1024*1024);
        code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
        data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1024*1024);
        commands = dk::CmdBufMaker{device}.create();
        commandMemory = data->allocate(0x1000);
        commands.addMemory(commandMemory.getMemBlock(), commandMemory.getOffset(), commandMemory.getSize());

        dk::ImageLayout layout;
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_S8).setDimensions(Width, Height).initialize(layout);
        stencilMemory = images->allocate(layout.getSize(), layout.getAlignment());
        stencil.initialize(layout, stencilMemory.getMemBlock(), stencilMemory.getOffset());
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(Width, Height).initialize(layout);
        std::array<DkImage const *, 2> targets;
        for (unsigned i = 0; i < framebuffers.size(); i++) {
            framebufferMemory[i] = images->allocate(layout.getSize(), layout.getAlignment());
            framebuffers[i].initialize(layout, framebufferMemory[i].getMemBlock(), framebufferMemory[i].getOffset());
            targets[i] = &framebuffers[i];
        }
        swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), targets}.create();
        renderer.emplace(Width, Height, device, queue, *images, *code, *data);
        vg = nvgCreateDk(&*renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!vg) fatalThrow(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
        PlFontData font{};
        Result rc = plGetSharedFontByType(&font, PlSharedFontType_Standard);
        if (R_FAILED(rc)) fatalThrow(rc);
        if (nvgCreateFontMem(vg, "switch-standard", static_cast<u8 *>(font.address), font.size, 0) < 0)
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
    }
    ~Display() {
        queue.waitIdle();
        nvgDeleteDk(vg);
        renderer.reset();
        commands.clear();
        swapchain.destroy();
        commands.destroy();
        commandMemory.destroy();
        for (auto &memory : framebufferMemory) memory.destroy();
        stencilMemory.destroy();
    }

    void Draw(const JoyBackend& backend, bool confirm, bool diagnostics, bool about, unsigned page, unsigned controllerPage, const ColorEditor& editor) {
        queue.waitIdle();
        commands.clear();
        const int slot = queue.acquireImage(swapchain);
        dk::ImageView color{framebuffers[slot]}, depth{stencil};
        commands.bindRenderTargets(&color, &depth);
        commands.setViewports(0, {{0, 0, Width, Height, 0, 1}});
        commands.setScissors(0, {{0, 0, Width, Height}});
        commands.clearColor(0, DkColorMask_RGBA, 0, 0, 0, 1);
        commands.clearDepthStencil(true, 1, 0xFF, 0);
        dk::RasterizerState rasterizer;
        dk::ColorState colors;
        dk::ColorWriteState writes;
        commands.bindRasterizerState(rasterizer);
        commands.bindColorState(colors);
        commands.bindColorWriteState(writes);
        queue.submitCommands(commands.finishList());
        nvgBeginFrame(vg, Width, Height, 1);
        Rectangle(vg, 0, 0, Width, Height, Theme::Background, 0);
        Rectangle(vg, 56, 42, 4, 40, Theme::Accent, 2);
        Text(vg, 76, 37, editor.open ? "Joy-Con Colors" : diagnostics ? "Diagnostics" : "JoyOff", 36);
        if (editor.open) DrawColors(vg, backend, editor);
        else if (diagnostics) DrawDiagnostics(vg, backend, atmosphere, page);
        else DrawHome(vg, backend, controllerPage);
        if (about) DrawAbout(vg);
        if (confirm) DrawConfirmation(vg, backend);
        nvgEndFrame(vg);
        queue.presentImage(swapchain, slot);
    }
};
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Result rc = romfsInit();
    if (R_FAILED(rc)) fatalThrow(rc);
    rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) { romfsExit(); fatalThrow(rc); }
    {
        Display display;
        JoyBackend backend;
        PadState pad;
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        padInitializeAny(&pad);
        hidInitializeTouchScreen();
        backend.Refresh();
        bool touching = false, confirm = false, diagnostics = false, about = false;
        ColorEditor editor;
        unsigned page = 0;
        unsigned controllerPage = 0;
        bool trackingSwipe = false;
        int swipeStartX = 0, swipeStartY = 0, swipeLastX = 0, swipeLastY = 0;
        while (appletMainLoop()) {
            backend.Update();
            backend.AutoRefresh(confirm || editor.open);
            if (editor.reload && !backend.Running()) {
                editor.Select(backend, 0);
                editor.reload = false;
            }
            const unsigned controllerPages = ControllerPageCount(backend);
            controllerPage = std::min(controllerPage, controllerPages - 1);
            padUpdate(&pad);
            u64 down = padGetButtonsDown(&pad);
            HidTouchScreenState touch{};
            hidGetTouchScreenStates(&touch, 1);
            const bool pressed = touch.count && !touching;
            const bool released = !touch.count && touching;
            touching = touch.count != 0;
            if (!editor.open || editor.browser.open || editor.confirm || editor.advanced || backend.Running() || touch.count != 1) {
                editor.sliderChannel = -1;
            } else {
                const int x = static_cast<int>(touch.touches[0].x);
                const int y = static_cast<int>(touch.touches[0].y);
                if (pressed && x >= 788 && x <= 1112 && y >= 218 && y < 494 && (y - 218) % 46 < 40) {
                    editor.sliderChannel = (y - 218) / 46;
                    editor.channel = static_cast<unsigned>(editor.sliderChannel);
                }
                if (editor.sliderChannel >= 0) {
                    editor.rgb[editor.sliderChannel] = static_cast<u8>((std::clamp(x - 800, 0, 300) * 255 + 150) / 300);
                }
            }
            if (editor.open || confirm || diagnostics || about || touch.count > 1) trackingSwipe = false;
            if (pressed && !editor.open && !confirm && !diagnostics && !about && touch.count == 1) {
                swipeStartX = swipeLastX = static_cast<int>(touch.touches[0].x);
                swipeStartY = swipeLastY = static_cast<int>(touch.touches[0].y);
                trackingSwipe = controllerPages > 1 && swipeStartX >= 56 && swipeStartX <= 1224 &&
                    swipeStartY >= 142 && swipeStartY <= 492;
            }
            if (trackingSwipe && touch.count == 1) {
                swipeLastX = static_cast<int>(touch.touches[0].x);
                swipeLastY = static_cast<int>(touch.touches[0].y);
            }
            if (released && trackingSwipe) {
                const int dx = swipeLastX - swipeStartX;
                const int dy = swipeLastY - swipeStartY;
                if (dy >= -70 && dy <= 70) {
                    if (dx <= -80) down |= HidNpadButton_R;
                    else if (dx >= 80) down |= HidNpadButton_L;
                }
                trackingSwipe = false;
            }
            if (editor.open && (pressed || down)) editor.inputMessage.clear();
            if (pressed) {
                const auto x = touch.touches[0].x;
                const auto y = touch.touches[0].y;
                if (editor.open) {
                    if (!backend.Running()) {
                        if (editor.browser.open) {
                            if (y >= 644 && y <= 696) {
                                if (x >= 56 && x <= 276) down |= HidNpadButton_L;
                                if (x >= 293 && x <= 513) down |= HidNpadButton_R;
                                if (x >= 530 && x <= 750) down |= HidNpadButton_X;
                                if (x >= 767 && x <= 987) down |= HidNpadButton_A;
                                if (x >= 1004 && x <= 1224) down |= HidNpadButton_B;
                            } else if (x >= 76 && x <= 1204 && y >= 190 && y < 526 && (y - 190) % 48 < 44) {
                                const size_t index = editor.browser.selected / 7 * 7 + (y - 190) / 48;
                                if (index < editor.browser.entries.size()) {
                                    editor.browser.selected = index;
                                    down |= HidNpadButton_A;
                                }
                            }
                        } else if (editor.confirm) {
                            if (y >= 450 && y <= 502) {
                                if (x >= 200 && x <= 600) down |= HidNpadButton_B;
                                if (x >= 640 && x <= 1080) down |= HidNpadButton_A;
                            }
                        } else if (x >= 992 && x <= 1224 && y >= 42 && y <= 94) {
                            down |= HidNpadButton_Minus;
                        } else if (y >= 644 && y <= 696) {
                            if (x >= 56 && x <= 276) down |= HidNpadButton_X;
                            if (x >= 293 && x <= 513) down |= HidNpadButton_Y;
                            if (x >= 530 && x <= 750) down |= HidNpadButton_Plus;
                            if (x >= 767 && x <= 987) down |= HidNpadButton_A;
                            if (x >= 1004 && x <= 1224) down |= HidNpadButton_B;
                        } else if (y >= 136 && y <= 166) down |= x < 640 ? HidNpadButton_L : HidNpadButton_R;
                        else if (x >= 550 && x <= 1182 && y >= 218 && y < 494) {
                            editor.channel = (y - 218) / 46;
                            if (editor.advanced && x >= 846 && x <= 910) editor.Adjust(-1);
                            if (editor.advanced && x >= 1054 && x <= 1118) editor.Adjust(1);
                            if (editor.advanced && x >= 924 && x <= 1038 && (y - 218) % 46 >= 3 && (y - 218) % 46 <= 37) {
                                editor.EditValue();
                                down = 0;
                                padUpdate(&pad);
                            }
                        }
                    }
                } else if (about) {
                    if (x >= 700 && x <= 1000 && y >= 530 && y <= 582) down |= HidNpadButton_B;
                } else if (confirm && y >= 450 && y <= 502) {
                    if (x >= 200 && x <= 600) down |= HidNpadButton_B;
                    if (x >= 640 && x <= 1080) down |= HidNpadButton_Minus;
                } else if (!confirm && y >= 644 && y <= 696) {
                    if (diagnostics) {
                        if (x >= 56 && x <= 284) down |= HidNpadButton_X;
                        if (x >= 304 && x <= 624) down |= HidNpadButton_Minus;
                        if (x >= 644 && x <= 972) down |= HidNpadButton_Y;
                        if (x >= 992 && x <= 1224) down |= HidNpadButton_B;
                    } else {
                        if (x >= 56 && x <= 276) down |= HidNpadButton_Y;
                        if (x >= 293 && x <= 513) down |= HidNpadButton_ZL;
                        if (x >= 530 && x <= 750) down |= HidNpadButton_Minus;
                        if (x >= 767 && x <= 987) down |= HidNpadButton_ZR;
                        if (x >= 1004 && x <= 1224) down |= HidNpadButton_Plus;
                    }
                } else if (!confirm && !diagnostics && x >= 56 && x <= 240 && y >= 594 && y <= 630) {
                    down |= HidNpadButton_X;
                } else if (!confirm && !diagnostics && controllerPages > 1 && y >= LegendTop && y <= LegendBottom &&
                    x >= ControllerLegendStart(controllerPages) &&
                    x < ControllerLegendStart(controllerPages) + controllerPages * LegendCellWidth) {
                    controllerPage = static_cast<unsigned>((x - ControllerLegendStart(controllerPages)) / LegendCellWidth);
                } else if (!confirm && diagnostics && y >= 146 && y <= 536) {
                    down |= x < 640 ? HidNpadButton_L : HidNpadButton_R;
                }
            }
            if (editor.open) {
                if (!backend.Running()) {
                    if (editor.browser.open) {
                        auto& browser = editor.browser;
                        if (down & (HidNpadButton_B | HidNpadButton_Plus)) browser.open = false;
                        else if (down & HidNpadButton_X) browser.Parent();
                        else if ((down & HidNpadButton_Up) && browser.selected) --browser.selected;
                        else if ((down & HidNpadButton_Down) && browser.selected + 1 < browser.entries.size()) ++browser.selected;
                        else if ((down & HidNpadButton_L) && browser.selected >= 7) browser.selected -= 7;
                        else if ((down & HidNpadButton_R) && (browser.selected / 7 + 1) * 7 < browser.entries.size())
                            browser.selected = std::min(browser.selected + 7, browser.entries.size() - 1);
                        else if ((down & HidNpadButton_A) && !browser.entries.empty()) {
                            const std::string path = browser.SelectedPath();
                            if (browser.entries[browser.selected].directory) browser.Read(path);
                            else if (editor.CanApply(backend)) {
                                std::array<u8, 6> colors{};
                                if (backend.ReadColorBackup(backend.Targets()[editor.target].padId, path, colors)) {
                                    editor.beforeRestore = editor.rgb;
                                    editor.rgb = colors;
                                    editor.restorePath = path;
                                    editor.restore = editor.confirm = true;
                                    browser.open = false;
                                } else browser.message = backend.ColorMessage();
                            } else browser.message = "Select a verified wireless Joy-Con before restoring.";
                        }
                    } else if (editor.confirm) {
                        if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                            if (editor.restore) editor.rgb = editor.beforeRestore;
                            editor.restore = editor.confirm = false;
                        } else if (down & HidNpadButton_A) {
                            editor.confirm = false;
                            if (editor.CanApply(backend)) {
                                if (editor.restore) backend.RestoreColors(backend.Targets()[editor.target].padId, editor.restorePath, editor.rgb);
                                else backend.ApplyColors(backend.Targets()[editor.target].padId, editor.Body(), editor.Buttons());
                            }
                            editor.restore = false;
                        }
                    } else if (down & HidNpadButton_B) editor.open = false;
                    else if ((down & HidNpadButton_Plus) && editor.CanApply(backend)) editor.browser.Open();
                    else if (down & HidNpadButton_Minus) {
                        editor.advanced = !editor.advanced;
                        editor.sliderChannel = -1;
                    } else if (down & HidNpadButton_X) {
                        backend.Refresh();
                        editor.Select(backend, 0);
                        editor.reload = backend.Running();
                    } else if ((down & HidNpadButton_Y) && editor.CanApply(backend)) {
                        backend.BackupColors(backend.Targets()[editor.target].padId);
                    } else if ((down & HidNpadButton_A) && editor.CanApply(backend)) editor.confirm = true;
                    else {
                        if ((down & HidNpadButton_L) && editor.target) editor.Select(backend, editor.target - 1);
                        if ((down & HidNpadButton_R) && editor.target + 1 < backend.Targets().size()) editor.Select(backend, editor.target + 1);
                        if ((down & HidNpadButton_Up) && editor.channel) --editor.channel;
                        if ((down & HidNpadButton_Down) && editor.channel < 5) ++editor.channel;
                        if (down & HidNpadButton_Left) editor.Adjust(-1);
                        if (down & HidNpadButton_Right) editor.Adjust(1);
                        if (down & HidNpadButton_ZL) editor.Adjust(-16);
                        if (down & HidNpadButton_ZR) editor.Adjust(16);
                    }
                }
            } else if (about) {
                if (down & (HidNpadButton_B | HidNpadButton_Plus)) about = false;
            } else if (diagnostics) {
                if (down & (HidNpadButton_Y | HidNpadButton_B | HidNpadButton_Plus)) diagnostics = false;
                else if ((down & HidNpadButton_X) && !backend.Running()) backend.Refresh();
                else if ((down & HidNpadButton_Minus) && !backend.Running()) backend.SetLogging(!backend.LoggingEnabled());
                const unsigned pages = std::max(1u, static_cast<unsigned>((backend.Targets().size() + 3) / 4));
                page = std::min(page, pages - 1);
                if ((down & HidNpadButton_L) && page) --page;
                if ((down & HidNpadButton_R) && page + 1 < pages) ++page;
            } else if (confirm) {
                if (down & (HidNpadButton_B | HidNpadButton_Plus)) confirm = false;
                else if (down & HidNpadButton_Minus) {
                    confirm = false;
                    backend.Start();
                }
            } else if ((down & HidNpadButton_ZL) && !backend.Running()) {
                editor.open = true;
                editor.Select(backend, 0);
            } else if (down & HidNpadButton_ZR) {
                about = true;
            } else if (down & HidNpadButton_Y) {
                diagnostics = true;
                page = 0;
            } else if (!backend.Running()) {
                if (down & (HidNpadButton_B | HidNpadButton_Plus)) break;
                if (down & HidNpadButton_X) backend.Refresh();
                else if (down & HidNpadButton_Minus) {
                    if (backend.Ready()) confirm = true;
                    else backend.ExplainUnavailable();
                }
            } else if (down & HidNpadButton_Minus) {
                backend.ExplainUnavailable();
            }
            if (!editor.open && !diagnostics && !confirm && !about) {
                if ((down & HidNpadButton_L) && controllerPage) --controllerPage;
                if ((down & HidNpadButton_R) && controllerPage + 1 < controllerPages) ++controllerPage;
            }
            display.Draw(backend, confirm, diagnostics, about, page, controllerPage, editor);
        }
        // Complete any already-started persistent command sequence before releasing services.
        while (backend.Running()) { backend.Update(); svcSleepThread(1000000); }
    }
    plExit();
    romfsExit();
    return 0;
}
