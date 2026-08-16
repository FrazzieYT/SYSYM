#pragma once
#include <windows.h>
#include <gdiplus.h>
using namespace Gdiplus;

void DrawMonitorContent(Graphics& g, const RectF& contentArea, Font& contentFont);
bool OnMonitorClick(int x, int y, const RectF& contentArea);
bool OnMonitorRightClick(int x, int y, const RectF& contentArea);
bool OnMonitorDblClick(int x, int y, const RectF& contentArea);