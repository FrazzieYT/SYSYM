#pragma once
#include <windows.h>
#include <gdiplus.h>
using namespace Gdiplus;

void DrawTaskManagerContent(Graphics& g, const RectF& contentArea, Font& contentFont);
bool OnTaskManagerRightClick(int x, int y, const RectF& contentArea);
bool OnTaskManagerMouseMove(int x, int y, const RectF& contentArea);
bool OnTaskManagerDblClick(int x, int y, const RectF& contentArea);
bool OnTaskManagerClick(int x, int y, const RectF& contentArea);
bool OnTaskManagerKey(UINT msg, WPARAM wParam, LPARAM lParam);
bool OnTaskManagerLButtonUp();
void TaskManagerOnTimer();