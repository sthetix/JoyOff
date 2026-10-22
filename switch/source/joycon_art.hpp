#pragma once

// Front-view outline adapted from 0 Noctis 0's Joy-Con illustration (CC BY-SA 4.0).
// See COPYING.ART for the reference, attribution, and adaptation details.
void JoyConOutline(NVGcontext* vg, bool right) {
    nvgSave(vg);
    if (right) { nvgTranslate(vg, 136.512f, 0); nvgScale(vg, -1, 1); }
    nvgBeginPath(vg);
    nvgMoveTo(vg, 136.512f, 3.25f);
    nvgLineTo(vg, 136.512f, 415.937f);
    nvgBezierTo(vg, 136.512f, 416.961f, 136.006f, 417.734f, 134.715f, 417.734f);
    nvgLineTo(vg, 85.075f, 417.734f);
    nvgBezierTo(vg, 33.395f, 417.734f, 0, 372.036f, 0, 332.659f);
    nvgLineTo(vg, 0, 84.719f);
    nvgBezierTo(vg, 0, 26.834f, 56.764f, 0, 84.719f, 0);
    nvgLineTo(vg, 133.509f, 0);
    nvgBezierTo(vg, 135.995f, 0, 136.512f, 1.368f, 136.512f, 3.25f);
    nvgClosePath(vg);
    nvgRestore(vg);
}

NVGcolor ControllerColor(u32 rgba) {
    return nvgRGB(rgba & 0xff, (rgba >> 8) & 0xff, (rgba >> 16) & 0xff);
}

void JoyConIcon(NVGcontext* vg, float x, float y, float scale, bool right,
    bool colorsKnown, u32 bodyColor, u32 buttonsColor) {
    nvgSave(vg);
    nvgTranslate(vg, x, y);
    nvgScale(vg, scale * 0.7f, scale * 0.7f);
    const NVGcolor body = colorsKnown ? ControllerColor(bodyColor) : nvgRGB(76, 88, 96);
    const NVGcolor controls = colorsKnown ? ControllerColor(buttonsColor) : nvgRGB(40, 45, 50);
    const NVGcolor legend = (controls.r * 0.299f + controls.g * 0.587f + controls.b * 0.114f) > 0.55f
        ? nvgRGB(25, 29, 32) : nvgRGB(211, 216, 219);
    // Shoulder cap follows the outer curve, leaving the inner rail edge straight.
    nvgSave(vg);
    if (right) { nvgTranslate(vg, 136.512f, 0); nvgScale(vg, -1, 1); }
    nvgBeginPath(vg);
    nvgMoveTo(vg, 7, 65);
    nvgBezierTo(vg, 11, 22, 47, -7, 86, -7);
    nvgLineTo(vg, 120, -7);
    nvgStrokeColor(vg, controls);
    nvgStrokeWidth(vg, 10);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
    nvgRestore(vg);
    nvgSave(vg);
    nvgTranslate(vg, 3, 5);
    JoyConOutline(vg, right);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 50));
    nvgFill(vg);
    nvgRestore(vg);
    Rectangle(vg, right ? -3 : 134, 17, 5, 380, nvgRGB(48, 54, 58), 2);
    JoyConOutline(vg, right);
    nvgFillColor(vg, body);
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(235, 240, 242, 80));
    nvgStrokeWidth(vg, 1.6f);
    nvgStroke(vg);

    const float centerX = right ? 63.94f : 72.33f;
    const float stickY = right ? 221.34f : 108.34f;
    Circle(vg, centerX, stickY, 32.06f, controls);
    Circle(vg, centerX, stickY, 24.7f, nvgLerpRGBA(controls, nvgRGB(255, 255, 255), 0.07f));
    nvgBeginPath(vg);
    nvgCircle(vg, centerX, stickY, 24.7f);
    nvgStrokeColor(vg, nvgLerpRGBA(controls, nvgRGB(255, 255, 255), 0.28f));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    const float buttonsY = right ? 105.0f : 226.2f;
    const float spacing = 31.31f;
    const float px[] = {centerX, centerX + spacing, centerX, centerX - spacing};
    const float py[] = {buttonsY - spacing, buttonsY, buttonsY + spacing, buttonsY};
    const char* letters[] = {"X", "A", "B", "Y"};
    for (int i = 0; i < 4; ++i) {
        Circle(vg, px[i], py[i], 15.15f, controls);
        if (right) {
            nvgFontFace(vg, "switch-standard");
            nvgFontSize(vg, 14);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, legend);
            nvgText(vg, px[i], py[i], letters[i], nullptr);
        } else {
            nvgSave(vg);
            nvgTranslate(vg, px[i], py[i]);
            nvgRotate(vg, i * 1.570796327f);
            nvgBeginPath(vg);
            nvgMoveTo(vg, 0, -6);
            nvgLineTo(vg, 5, 3);
            nvgLineTo(vg, -5, 3);
            nvgClosePath(vg);
            nvgFillColor(vg, legend);
            nvgFill(vg);
            nvgRestore(vg);
        }
    }
    if (right) {
        Line(vg, 22, 48, 46, 48, controls, 6);
        Line(vg, 34, 36, 34, 60, controls, 6);
        Circle(vg, 41, 300, 17, nvgLerpRGBA(controls, legend, 0.35f));
        Circle(vg, 41, 300, 13.3f, controls);
        nvgBeginPath(vg);
        nvgMoveTo(vg, 33, 300);
        nvgLineTo(vg, 41, 293);
        nvgLineTo(vg, 49, 300);
        nvgLineTo(vg, 47, 300);
        nvgLineTo(vg, 47, 307);
        nvgLineTo(vg, 35, 307);
        nvgLineTo(vg, 35, 300);
        nvgClosePath(vg);
        nvgFillColor(vg, legend);
        nvgFill(vg);
    } else {
        Rectangle(vg, 101, 45, 24, 6, controls, 1);
        Rectangle(vg, 82, 292, 27, 27, controls, 4);
        Circle(vg, 95.5f, 305.5f, 8.5f, legend);
        Circle(vg, 95.5f, 305.5f, 6.2f, controls);
    }
    // Body color is preserved after shutdown; the separate status indicator conveys state.
    nvgRestore(vg);
}
