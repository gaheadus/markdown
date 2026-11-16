// MarkdownOutline plugin for Notepad++
//
// Shows the heading outline of the current Markdown document in a dockable panel.
// Each heading is a tree node, nested by its level (H1 contains H2 contains H3, ...),
// and double-clicking a node jumps to that heading in the editor.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <windows.h>

#include <commctrl.h>

#include <algorithm>
#include <string>
#include <vector>

#include "PluginInterface.h"
#include "Notepad_plus_msgs.h"
#include "Docking.h"

#include "MarkdownParser.h"
#include "OutlinePanel.h"

// ---------------------------------------------------------------------------
// Plugin state
// ---------------------------------------------------------------------------

namespace
{
	NppData g_nppData{};
	bool g_isPanelCreated = false;

	// The dlgID of the docked panel. It must be stable across sessions so that
	// Notepad++ can restore the position and the visibility of the panel.
	const int PANEL_DLG_ID = 1;

	// Delay before the outline is rebuilt after the document changed.
	const UINT REBUILD_DELAY_MS = 400;

	// Index of the command in the function array below.
	const int CMD_TOGGLE_PANEL = 0;
	const int CMD_REFRESH = 1;
	const int CMD_EXPAND_ALL = 2;
	const int CMD_COLLAPSE_ALL = 3;

	MdOutline::OutlinePanel g_panel;

	HWND g_hSciMain = nullptr;
	HWND g_hSciSub = nullptr;

	FuncItem g_funcItems[4] = {};

	// --- helpers ------------------------------------------------------------

	LRESULT sendNppMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0)
	{
		return ::SendMessage(g_nppData._nppHandle, message, wParam, lParam);
	}

	HWND getCurrentScintilla()
	{
		int view = 0;
		if (!sendNppMessage(NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&view)))
			return nullptr;

		if (view == 0)
			return g_hSciMain;
		if (view == 1)
			return g_hSciSub;
		return nullptr;
	}

	std::wstring getCurrentFilePath()
	{
		wchar_t buffer[MAX_PATH] = { '\0' };
		if (!sendNppMessage(NPPM_GETFULLCURRENTPATH, 0, reinterpret_cast<LPARAM>(buffer)))
			return std::wstring();
		return std::wstring(buffer);
	}

	// Reads the whole content of the given Scintilla view as UTF-16.
	std::wstring getScintillaText(HWND hSci)
	{
		if (!hSci)
			return std::wstring();

		const LRESULT length = ::SendMessage(hSci, SCI_GETLENGTH, 0, 0);
		if (length <= 0)
			return std::wstring();

		const LRESULT codepage = ::SendMessage(hSci, SCI_GETCODEPAGE, 0, 0);
		const UINT cp = (codepage == 0) ? CP_ACP : static_cast<UINT>(codepage);

		std::vector<char> buffer(static_cast<size_t>(length) + 1, '\0');
		Sci_TextRangeFull range{};
		range.chrg.cpMin = 0;
		range.chrg.cpMax = length;
		range.lpstrText = buffer.data();
		::SendMessage(hSci, SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));

		size_t byteCount = 0;
		while (byteCount < buffer.size() && buffer[byteCount] != '\0')
			++byteCount;

		if (byteCount == 0)
			return std::wstring();

		const int charCount = ::MultiByteToWideChar(cp, 0, buffer.data(), static_cast<int>(byteCount), nullptr, 0);
		if (charCount <= 0)
			return std::wstring();

		std::wstring result(static_cast<size_t>(charCount), L'\0');
		::MultiByteToWideChar(cp, 0, buffer.data(), static_cast<int>(byteCount), &result[0], charCount);
		return result;
	}

	void jumpToLine(int line)
	{
		HWND hSci = getCurrentScintilla();
		if (!hSci)
			return;

		::SendMessage(hSci, SCI_GOTOLINE, line, 0);
		::SendMessage(hSci, SCI_ENSUREVISIBLE, line, 0);
		::SetFocus(hSci);
	}

	// --- outline ------------------------------------------------------------

	void rebuildOutline()
	{
		if (!g_isPanelCreated || !g_panel.getHSelf())
			return;

		const std::wstring filePath = getCurrentFilePath();

		if (!MdOutline::isMarkdownFile(filePath))
		{
			g_panel.showMessage(L"Not a Markdown document");
			return;
		}

		HWND hSci = getCurrentScintilla();
		if (!hSci)
		{
			g_panel.showMessage(L"No document");
			return;
		}

		const std::wstring text = getScintillaText(hSci);
		std::vector<MdOutline::Heading> headings;
		MdOutline::parseHeadings(text, headings);

		if (headings.empty())
		{
			g_panel.showMessage(L"No headings found");
			return;
		}

		g_panel.setHeadings(headings);
	}

	void scheduleRebuild()
	{
		if (!g_isPanelCreated || !g_panel.getHSelf())
			return;

		g_panel.scheduleRefresh(REBUILD_DELAY_MS);
	}

	// --- panel ---------------------------------------------------------------

	void showPanel()
	{
		if (!g_isPanelCreated)
		{
			if (!g_panel.create(g_nppData._nppHandle, ::GetModuleHandle(nullptr)))
				return;

			g_panel.setJumpHandler(jumpToLine);
			g_panel.setRefreshHandler(rebuildOutline);

			DockedWidgetData data{};
			data.hClient = g_panel.getHSelf();
			data.pszName = MdOutline::PANEL_TITLE;
			data.dlgID = PANEL_DLG_ID;
			data.pszModuleName = MdOutline::MODULE_NAME;
			data.uMask = DWS_DF_CONT_RIGHT | DWS_ICONTAB;

			sendNppMessage(NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data));
			g_isPanelCreated = true;
		}

		sendNppMessage(NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(g_panel.getHSelf()));
		rebuildOutline();
	}

	void hidePanel()
	{
		if (!g_isPanelCreated)
			return;

		sendNppMessage(NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(g_panel.getHSelf()));
	}

	// --- plugin commands -----------------------------------------------------

	void togglePanel()
	{
		if (!g_isPanelCreated || !::IsWindowVisible(g_panel.getHSelf()))
			showPanel();
		else
			hidePanel();
	}

	void refreshOutline()
	{
		rebuildOutline();
	}

	void expandAllOutline()
	{
		if (g_isPanelCreated && g_panel.getHSelf())
			g_panel.expandAll();
	}

	void collapseAllOutline()
	{
		if (g_isPanelCreated && g_panel.getHSelf())
			g_panel.collapseAll();
	}

	// --- toolbar icon --------------------------------------------------------

	// Draws the "outline" glyph: a vertical spine with three branches, each of
	// them followed by a horizontal bar standing for the heading text.
	//
	// The pixels are written directly into a 32 bits ARGB bitmap so that the
	// background stays fully transparent.
	HBITMAP createOutlineBitmap(int size, COLORREF color)
	{
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = size;
		info.bmiHeader.biHeight = -size; // top-down
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;

		void* bits = nullptr;
		HDC screen = ::GetDC(nullptr);
		HDC dc = ::CreateCompatibleDC(screen);
		HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		::ReleaseDC(nullptr, screen);
		::DeleteDC(dc);

		if (!bitmap || !bits)
		{
			if (bitmap)
				::DeleteObject(bitmap);
			return nullptr;
		}

		auto* pixels = static_cast<unsigned char*>(bits);
		const int stride = size * 4;

		auto setPixel = [&](int x, int y)
		{
			if (x < 0 || y < 0 || x >= size || y >= size)
				return;
			unsigned char* p = pixels + y * stride + x * 4;
			p[0] = static_cast<unsigned char>(color & 0xFF);         // blue
			p[1] = static_cast<unsigned char>((color >> 8) & 0xFF);  // green
			p[2] = static_cast<unsigned char>((color >> 16) & 0xFF); // red
			p[3] = 0xFF;                                           // alpha
		};

		auto hLine = [&](int x1, int x2, int y)
		{
			for (int x = std::min(x1, x2); x <= std::max(x1, x2); ++x)
				setPixel(x, y);
		};

		auto vLine = [&](int x, int y1, int y2)
		{
			for (int y = std::min(y1, y2); y <= std::max(y1, y2); ++y)
				setPixel(x, y);
		};

		const int margin = std::max(2, size / 8);
		const int branch = std::max(4, size / 4);
		const int rows = 3;
		const int rowHeight = (size - 2 * margin) / rows;
		const int thickness = std::max(1, size / 16);

		// Vertical spine.
		for (int t = 0; t < thickness; ++t)
			vLine(margin + t, margin, size - margin);

		for (int row = 0; row < rows; ++row)
		{
			const int y = margin + row * rowHeight + rowHeight / 2;

			// Branch towards the heading text.
			for (int t = 0; t < thickness; ++t)
				hLine(margin, margin + branch, y + t);

			// A small gap, then the heading text itself.
			const int textStart = margin + branch + std::max(2, size / 12);
			for (int t = 0; t < thickness; ++t)
				hLine(textStart, size - margin, y + t);
		}

		return bitmap;
	}

	// Builds an icon (with its monochrome mask) out of a 32 bits ARGB bitmap.
	// A pixel of the bitmap which is not fully opaque is transparent.
	HICON createIconFromBitmap(HBITMAP bitmap)
	{
		if (!bitmap)
			return nullptr;

		BITMAP info{};
		if (!::GetObject(bitmap, sizeof(info), &info))
			return nullptr;

		const int width = info.bmWidth;
		const int height = info.bmHeight;
		if (width <= 0 || height <= 0)
			return nullptr;

		// The mask is a monochrome bitmap: a set bit means "transparent".
		BITMAPINFO maskInfo{};
		maskInfo.bmiHeader.biSize = sizeof(maskInfo.bmiHeader);
		maskInfo.bmiHeader.biWidth = width;
		maskInfo.bmiHeader.biHeight = -height; // top-down
		maskInfo.bmiHeader.biPlanes = 1;
		maskInfo.bmiHeader.biBitCount = 1;
		maskInfo.bmiHeader.biCompression = BI_RGB;

		void* maskBits = nullptr;
		HDC screen = ::GetDC(nullptr);
		HDC dc = ::CreateCompatibleDC(screen);
		HBITMAP mask = ::CreateDIBSection(dc, &maskInfo, DIB_RGB_COLORS, &maskBits, nullptr, 0);
		::ReleaseDC(nullptr, screen);
		::DeleteDC(dc);

		if (!mask || !maskBits)
		{
			if (mask)
				::DeleteObject(mask);
			return nullptr;
		}

		// Make a copy of the color bitmap whose pixels can be read directly.
		BITMAPINFO colorInfo{};
		colorInfo.bmiHeader.biSize = sizeof(colorInfo.bmiHeader);
		colorInfo.bmiHeader.biWidth = width;
		colorInfo.bmiHeader.biHeight = -height;
		colorInfo.bmiHeader.biPlanes = 1;
		colorInfo.bmiHeader.biBitCount = 32;
		colorInfo.bmiHeader.biCompression = BI_RGB;

		void* colorBits = nullptr;
		screen = ::GetDC(nullptr);
		dc = ::CreateCompatibleDC(screen);
		HBITMAP colorCopy = ::CreateDIBSection(dc, &colorInfo, DIB_RGB_COLORS, &colorBits, nullptr, 0);
		::ReleaseDC(nullptr, screen);
		::DeleteDC(dc);

		if (!colorCopy || !colorBits)
		{
			if (colorCopy)
				::DeleteObject(colorCopy);
			::DeleteObject(mask);
			return nullptr;
		}

		dc = ::CreateCompatibleDC(nullptr);
		HGDIOBJ old = ::SelectObject(dc, bitmap);
		HGDIOBJ oldCopy = ::SelectObject(dc, colorCopy);
		::BitBlt(dc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
		::SelectObject(dc, oldCopy);
		::SelectObject(dc, old);
		::DeleteDC(dc);

		const int colorStride = width * 4;
		const int maskStride = ((width + 31) / 32) * 4;
		auto* maskPixels = static_cast<unsigned char*>(maskBits);
		auto* colorPixels = static_cast<unsigned char*>(colorBits);

		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				const unsigned char alpha = colorPixels[y * colorStride + x * 4 + 3];
				if (alpha == 0)
				{
					unsigned char* p = maskPixels + y * maskStride + (x / 8);
					*p |= static_cast<unsigned char>(0x80 >> (x % 8));
				}
			}
		}

		::DeleteObject(colorCopy);

		ICONINFO icon{};
		icon.fIcon = TRUE;
		icon.hbmColor = bitmap;
		icon.hbmMask = mask;

		HICON result = ::CreateIconIndirect(&icon);
		::DeleteObject(mask);
		return result;
	}

	void registerToolbarIcon()
	{
		const int cmdID = g_funcItems[CMD_TOGGLE_PANEL]._cmdID;
		if (cmdID == 0)
			return; // Notepad++ did not assign the command id yet.

		const int size = ::GetSystemMetrics(SM_CXSMICON) * 2;

		HBITMAP bmp = createOutlineBitmap(size, RGB(0, 0, 0));
		HBITMAP bmpDark = createOutlineBitmap(size, RGB(255, 255, 255));
		HICON icon = createIconFromBitmap(bmp);
		HICON iconDark = createIconFromBitmap(bmpDark);

		if (!bmp || !bmpDark || !icon || !iconDark)
			return;

		toolbarIconsWithDarkMode icons{};
		icons.hToolbarBmp = bmp;
		icons.hToolbarIcon = icon;
		icons.hToolbarIconDarkMode = iconDark;

		sendNppMessage(NPPM_ADDTOOLBARICON_FORDARKMODE, static_cast<UINT>(cmdID), reinterpret_cast<LPARAM>(&icons));
	}
}

// ---------------------------------------------------------------------------
// Notepad++ plugin interface
// ---------------------------------------------------------------------------

extern "C" __declspec(dllexport) void setInfo(NppData nppData)
{
	g_nppData = nppData;
	g_hSciMain = nppData._scintillaMainHandle;
	g_hSciSub = nppData._scintillaSecondHandle;
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
	return L"Markdown Outline";
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbFcts)
{
	g_funcItems[CMD_TOGGLE_PANEL] = { L"Show/Hide Markdown Outline", reinterpret_cast<PFUNCPLUGINCMD>(&togglePanel), 0, false, nullptr };
	g_funcItems[CMD_REFRESH] = { L"Refresh Markdown Outline", reinterpret_cast<PFUNCPLUGINCMD>(&refreshOutline), 0, false, nullptr };
	g_funcItems[CMD_EXPAND_ALL] = { L"Expand All", reinterpret_cast<PFUNCPLUGINCMD>(&expandAllOutline), 0, false, nullptr };
	g_funcItems[CMD_COLLAPSE_ALL] = { L"Collapse All", reinterpret_cast<PFUNCPLUGINCMD>(&collapseAllOutline), 0, false, nullptr };

	*nbFcts = static_cast<int>(sizeof(g_funcItems) / sizeof(g_funcItems[0]));
	return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode)
{
	if (!notifyCode)
		return;

	switch (notifyCode->nmhdr.code)
	{
		case NPPN_READY:
		{
			// Ask for the modification events which are not forwarded by default.
			sendNppMessage(NPPM_ADDSCNMODIFIEDFLAGS, 0, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
			rebuildOutline();
			break;
		}

		case NPPN_TBMODIFICATION:
		{
			// Notepad++ is about to (re)create the toolbar: this is the moment to
			// add our own button to it.
			registerToolbarIcon();
			break;
		}

		case NPPN_BUFFERACTIVATED:
		case NPPN_FILEOPENED:
		case NPPN_LANGCHANGED:
		case NPPN_FILELOADFAILED:
		{
			rebuildOutline();
			break;
		}

		case NPPN_SHUTDOWN:
		{
			g_isPanelCreated = false;
			break;
		}

		case SCN_MODIFIED:
		{
			if (notifyCode->nmhdr.hwndFrom == g_hSciMain || notifyCode->nmhdr.hwndFrom == g_hSciSub)
				scheduleRebuild();
			break;
		}
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
	return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
	return TRUE;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		INITCOMMONCONTROLSEX icc{};
		icc.dwSize = sizeof(icc);
		icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_WIN95_CLASSES;
		::InitCommonControlsEx(&icc);
	}
	return TRUE;
}
