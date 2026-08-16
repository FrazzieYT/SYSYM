#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

using namespace Gdiplus;

namespace Notepad {

	constexpr UINT WM_NP_APPEND = WM_APP + 410;

	// Create / Show / Hide editor inside tab
	void EnsureUI(HWND parent);
	void Show(const RectF& area);
	void Hide();
	bool IsActive();

	// Toolbar & document tabs rendering + editor positioning
	void Draw(Graphics& g, const RectF& area, Font& contentFont);

	// Toolbar and document tabs clicks
	bool OnClick(int x, int y, const RectF& area);

	// Documents
	void NewDoc();
	void OpenFile(const std::wstring& path);

	// WndProc utilities
	void AppendOutput(wchar_t* buffer);
	void OnEditChanged();
	HBRUSH GetDarkBrush();

}