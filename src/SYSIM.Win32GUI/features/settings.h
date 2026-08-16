#pragma once
#include <windows.h>
#include <gdiplus.h>
using namespace Gdiplus;

void DrawSettingsContent(
    Graphics& g,
    const RectF& contentArea,
    Font& contentFont
);

bool OnSettingsClick(
    int x,
    int y,
    const RectF& contentArea
);

void ApplyDefaultSettings();