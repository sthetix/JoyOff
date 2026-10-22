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

    void Draw(const JoyBackend& backend, bool confirm, bool diagnostics, unsigned page, unsigned controllerPage) {
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
        Text(vg, 76, 37, diagnostics ? "Diagnostics" : "JoyOff", 36);
        if (diagnostics) DrawDiagnostics(vg, backend, atmosphere, page);
        else DrawHome(vg, backend, controllerPage);
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
        bool touching = false, confirm = false, diagnostics = false;
        unsigned page = 0;
        unsigned controllerPage = 0;
        bool trackingSwipe = false;
        int swipeStartX = 0, swipeStartY = 0, swipeLastX = 0, swipeLastY = 0;
        while (appletMainLoop()) {
            backend.Update();
            backend.AutoRefresh(confirm);
            const unsigned controllerPages = ControllerPageCount(backend);
            controllerPage = std::min(controllerPage, controllerPages - 1);
            padUpdate(&pad);
            u64 down = padGetButtonsDown(&pad);
            HidTouchScreenState touch{};
            hidGetTouchScreenStates(&touch, 1);
            const bool pressed = touch.count && !touching;
            const bool released = !touch.count && touching;
            touching = touch.count != 0;
            if (confirm || diagnostics || touch.count > 1) trackingSwipe = false;
            if (pressed && !confirm && !diagnostics && touch.count == 1) {
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
            if (pressed) {
                const auto x = touch.touches[0].x;
                const auto y = touch.touches[0].y;
                if (confirm && y >= 450 && y <= 502) {
                    if (x >= 200 && x <= 600) down |= HidNpadButton_B;
                    if (x >= 640 && x <= 1080) down |= HidNpadButton_Minus;
                } else if (!confirm && y >= 644 && y <= 696) {
                    if (x >= 56 && x <= 284) down |= diagnostics ? HidNpadButton_X : HidNpadButton_Y;
                    if (x >= 304 && x <= 884) down |= diagnostics ? HidNpadButton_Y : HidNpadButton_Minus;
                    if (x >= 904 && x <= 1224) down |= diagnostics ? HidNpadButton_B : HidNpadButton_Plus;
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
            if (diagnostics) {
                if (down & (HidNpadButton_Y | HidNpadButton_B | HidNpadButton_Plus)) diagnostics = false;
                else if ((down & HidNpadButton_X) && !backend.Running()) backend.Refresh();
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
            if (!diagnostics && !confirm) {
                if ((down & HidNpadButton_L) && controllerPage) --controllerPage;
                if ((down & HidNpadButton_R) && controllerPage + 1 < controllerPages) ++controllerPage;
            }
            display.Draw(backend, confirm, diagnostics, page, controllerPage);
        }
        // Complete any already-started persistent command sequence before releasing services.
        while (backend.Running()) { backend.Update(); svcSleepThread(1000000); }
    }
    plExit();
    romfsExit();
    return 0;
}
