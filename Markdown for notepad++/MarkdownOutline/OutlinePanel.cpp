// MarkdownOutline plugin for Notepad++
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "OutlinePanel.h"

#include <algorithm>

namespace MdOutline
{
	namespace
	{
		const wchar_t* const WINDOW_CLASS = L"MarkdownOutlinePanelClass";
		const int ID_TREE = 1001;
		const int ID_EXPAND_ALL = 1002;
		const int ID_COLLAPSE_ALL = 1003;
		const UINT_PTR TIMER_REFRESH = 1;
		const UINT_PTR TIMER_TIMEOUT = 2;

		// The height of the row which holds the "expand all" / "collapse all" buttons.
		const int TOOLBAR_HEIGHT = 24;

		// The separator used to build the key which identifies a heading.
		const wchar_t KEY_SEPARATOR = L'\x1f';
	}

	bool OutlinePanel::create(HWND parent, HINSTANCE hInst)
	{
		WNDCLASSEXW wc{};
		wc.cbSize = sizeof(wc);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = OutlinePanel::windowProc;
		wc.hInstance = hInst;
		wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wc.lpszClassName = WINDOW_CLASS;

		// The class may already exist when the plugin is reloaded.
		if (!::GetClassInfoExW(hInst, WINDOW_CLASS, &wc))
		{
			if (!::RegisterClassExW(&wc))
				return false;
		}

		_hSelf = ::CreateWindowExW(
			0, WINDOW_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
			0, 0, 200, 200, parent, nullptr, hInst, nullptr);

		if (!_hSelf)
			return false;

		::SetWindowLongPtr(_hSelf, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		_hExpandAll = ::CreateWindowExW(
			0, L"BUTTON", L"Expand All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 0, 0, _hSelf, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EXPAND_ALL)), hInst, nullptr);

		_hCollapseAll = ::CreateWindowExW(
			0, L"BUTTON", L"Collapse All", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 0, 0, _hSelf, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_COLLAPSE_ALL)), hInst, nullptr);

		_hTree = ::CreateWindowExW(
			WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
			0, 0, 0, 0, _hSelf, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TREE)), hInst, nullptr);

		if (!_hTree)
		{
			destroy();
			return false;
		}

		::SendMessage(_hTree, TVM_SETUNICODEFORMAT, TRUE, 0);

		layout();
		return true;
	}

	void OutlinePanel::destroy()
	{
		if (_hSelf)
		{
			::KillTimer(_hSelf, TIMER_REFRESH);
			::DestroyWindow(_hSelf);
			_hSelf = nullptr;
			_hTree = nullptr;
			_hExpandAll = nullptr;
			_hCollapseAll = nullptr;
		}
	}

	void OutlinePanel::layout()
	{
		if (!_hSelf || !_hTree)
			return;

		RECT rc{};
		::GetClientRect(_hSelf, &rc);

		const int buttonWidth = std::max(60, static_cast<int>(rc.right / 2) - 4);
		if (_hExpandAll)
			::MoveWindow(_hExpandAll, 2, 2, buttonWidth, TOOLBAR_HEIGHT - 6, TRUE);
		if (_hCollapseAll)
			::MoveWindow(_hCollapseAll, 4 + buttonWidth, 2, buttonWidth, TOOLBAR_HEIGHT - 6, TRUE);

		::MoveWindow(_hTree, 0, TOOLBAR_HEIGHT, rc.right, std::max(0, static_cast<int>(rc.bottom) - TOOLBAR_HEIGHT), TRUE);
	}

	void OutlinePanel::scheduleRefresh(UINT delayMs)
	{
		if (!_hSelf)
			return;

		::SetTimer(_hSelf, TIMER_REFRESH, delayMs, nullptr);
	}

	void OutlinePanel::scheduleTimeout(UINT delayMs)
	{
		if (!_hSelf)
			return;

		::SetTimer(_hSelf, TIMER_TIMEOUT, delayMs, nullptr);
	}

	void OutlinePanel::refreshNow()
	{
		if (_hSelf)
			::KillTimer(_hSelf, TIMER_REFRESH);

		if (_refreshHandler)
			_refreshHandler();
	}

	void OutlinePanel::setHeadings(const std::vector<Heading>& headings)
	{
		if (!_hTree)
			return;

		// Remember which nodes were expanded before the tree is thrown away.
		captureExpandedState();

		::SendMessage(_hTree, WM_SETREDRAW, FALSE, 0);

		HTREEITEM root = reinterpret_cast<HTREEITEM>(TVI_ROOT);
		::SendMessage(_hTree, TVM_DELETEITEM, reinterpret_cast<WPARAM>(root), reinterpret_cast<LPARAM>(TVI_ROOT));
		_lineNumbers.clear();
		_stateKeys.clear();

		// The tree is built by level: a heading is attached to the closest previous
		// heading with a strictly smaller level.
		std::vector<std::pair<int, HTREEITEM>> levels; // (level, item)

		// The nodes which have just been inserted, together with their depth.
		std::vector<std::pair<int, HTREEITEM>> inserted;

		// Counts the headings sharing the same text, so that two identical
		// headings do not share the same expansion state.
		std::map<std::wstring, int> occurrences;

		for (const Heading& heading : headings)
		{
			TVINSERTSTRUCTW insert{};
			insert.hParent = root;
			insert.hInsertAfter = TVI_LAST;
			insert.itemex.mask = TVIF_TEXT | TVIF_PARAM;
			insert.itemex.pszText = const_cast<LPWSTR>(heading._text.c_str());
			insert.itemex.lParam = static_cast<LPARAM>(_lineNumbers.size());
			_lineNumbers.push_back(heading._line);

			// The key identifies the heading, even when the same text is used
			// several times in the same document.
			const int occurrence = occurrences[heading._text]++;
			_stateKeys.push_back(heading._text + KEY_SEPARATOR + std::to_wstring(occurrence));

			// Drop the levels which are not lower than the current one.
			while (!levels.empty() && levels.back().first >= heading._level)
				levels.pop_back();

			int depth = static_cast<int>(levels.size());
			if (!levels.empty())
				insert.hParent = levels.back().second;

			HTREEITEM item = reinterpret_cast<HTREEITEM>(::SendMessage(_hTree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
			if (item)
			{
				levels.push_back(std::make_pair(heading._level, item));
				inserted.push_back(std::make_pair(depth, item));
			}
		}

		applyExpandedState(inserted);

		::SendMessage(_hTree, WM_SETREDRAW, TRUE, 0);
		::InvalidateRect(_hTree, nullptr, TRUE);
	}

	void OutlinePanel::showMessage(const std::wstring& message)
	{
		if (!_hTree)
			return;

		::SendMessage(_hTree, WM_SETREDRAW, FALSE, 0);
		::SendMessage(_hTree, TVM_DELETEITEM, reinterpret_cast<WPARAM>(TVI_ROOT), reinterpret_cast<LPARAM>(TVI_ROOT));
		_lineNumbers.clear();
		_stateKeys.clear();

		TVINSERTSTRUCTW insert{};
		insert.hParent = reinterpret_cast<HTREEITEM>(TVI_ROOT);
		insert.hInsertAfter = TVI_LAST;
		insert.itemex.mask = TVIF_TEXT;
		insert.itemex.pszText = const_cast<LPWSTR>(message.c_str());
		::SendMessage(_hTree, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert));

		::SendMessage(_hTree, WM_SETREDRAW, TRUE, 0);
		::InvalidateRect(_hTree, nullptr, TRUE);
	}

	void OutlinePanel::expandAll()
	{
		if (!_hTree)
			return;

		std::vector<std::pair<int, HTREEITEM>> nodes;
		collectNodes(reinterpret_cast<HTREEITEM>(TVI_ROOT), 0, nodes);

		::SendMessage(_hTree, WM_SETREDRAW, FALSE, 0);
		for (const auto& node : nodes)
			::SendMessage(_hTree, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(node.second));
		::SendMessage(_hTree, WM_SETREDRAW, TRUE, 0);
		::InvalidateRect(_hTree, nullptr, TRUE);

		// The state has to survive the next rebuild.
		for (const auto& node : nodes)
			rememberExpandedState(node.second);
	}

	void OutlinePanel::collapseAll()
	{
		if (!_hTree)
			return;

		std::vector<std::pair<int, HTREEITEM>> nodes;
		collectNodes(reinterpret_cast<HTREEITEM>(TVI_ROOT), 0, nodes);

		::SendMessage(_hTree, WM_SETREDRAW, FALSE, 0);
		for (const auto& node : nodes)
			::SendMessage(_hTree, TVM_EXPAND, TVE_COLLAPSE, reinterpret_cast<LPARAM>(node.second));
		::SendMessage(_hTree, WM_SETREDRAW, TRUE, 0);
		::InvalidateRect(_hTree, nullptr, TRUE);

		// The state has to survive the next rebuild.
		for (const auto& node : nodes)
			rememberExpandedState(node.second);
	}

	void OutlinePanel::onTreeDoubleClick()
	{
		HTREEITEM item = reinterpret_cast<HTREEITEM>(::SendMessage(_hTree, TVM_GETNEXTITEM, TVGN_CARET, 0));
		if (!item)
			return;

		TVITEMW get{};
		get.mask = TVIF_PARAM;
		get.hItem = item;
		::SendMessage(_hTree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&get));

		const size_t index = static_cast<size_t>(get.lParam);
		if (index >= _lineNumbers.size())
			return;

		if (_jumpHandler)
			_jumpHandler(_lineNumbers[index]);
	}

	void OutlinePanel::collectNodes(HTREEITEM parent, int depth, std::vector<std::pair<int, HTREEITEM>>& nodes) const
	{
		HTREEITEM child = reinterpret_cast<HTREEITEM>(::SendMessage(_hTree, TVM_GETNEXTITEM, TVGN_CHILD, reinterpret_cast<LPARAM>(parent)));
		while (child)
		{
			nodes.push_back(std::make_pair(depth, child));
			collectNodes(child, depth + 1, nodes);
			child = reinterpret_cast<HTREEITEM>(::SendMessage(_hTree, TVM_GETNEXTITEM, TVGN_NEXT, reinterpret_cast<LPARAM>(child)));
		}
	}

	std::wstring OutlinePanel::makeStateKey(size_t index) const
	{
		if (index >= _stateKeys.size())
			return std::wstring();

		return _stateKeys[index];
	}

	void OutlinePanel::rememberExpandedState(HTREEITEM item)
	{
		if (!_hTree || !item)
			return;

		TVITEMW get{};
		get.mask = TVIF_PARAM | TVIF_STATE;
		get.stateMask = TVIS_EXPANDED | TVIS_EXPANDPARTIAL;
		get.hItem = item;
		::SendMessage(_hTree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&get));

		const size_t index = static_cast<size_t>(get.lParam);
		if (index >= _stateKeys.size())
			return;

		const bool expanded = (get.state & (TVIS_EXPANDED | TVIS_EXPANDPARTIAL)) != 0;
		_expandedState[makeStateKey(index)] = expanded;
	}

	void OutlinePanel::captureExpandedState()
	{
		if (!_hTree)
			return;

		std::vector<std::pair<int, HTREEITEM>> nodes;
		collectNodes(reinterpret_cast<HTREEITEM>(TVI_ROOT), 0, nodes);

		for (const auto& node : nodes)
			rememberExpandedState(node.second);
	}

	void OutlinePanel::applyExpandedState(const std::vector<std::pair<int, HTREEITEM>>& nodes)
	{
		if (!_hTree)
			return;

		for (const auto& node : nodes)
		{
			TVITEMW get{};
			get.mask = TVIF_PARAM;
			get.hItem = node.second;
			::SendMessage(_hTree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&get));

			const size_t index = static_cast<size_t>(get.lParam);
			if (index >= _stateKeys.size())
				continue;

			bool expanded = false;
			const auto it = _expandedState.find(makeStateKey(index));
			if (it != _expandedState.end())
			{
				expanded = it->second;
			}
			else
			{
				// A heading which was not seen before: apply the default policy.
				switch (_policy)
				{
					case ExpansionPolicy::All:
						expanded = true;
						break;
					case ExpansionPolicy::None:
						expanded = false;
						break;
					case ExpansionPolicy::Default:
					default:
						// The default policy shows the "_defaultExpandLevel" first
						// levels, so the headings of the first levels have to be
						// expanded to make the following ones visible.
						expanded = node.first < (_defaultExpandLevel - 1);
						break;
				}
			}

			::SendMessage(_hTree, TVM_EXPAND, expanded ? TVE_EXPAND : TVE_COLLAPSE, reinterpret_cast<LPARAM>(node.second));
		}
	}

	std::wstring OutlinePanel::dumpTree() const
	{
		if (!_hTree)
			return std::wstring();

		std::vector<std::pair<int, HTREEITEM>> nodes;
		collectNodes(reinterpret_cast<HTREEITEM>(TVI_ROOT), 0, nodes);

		std::wstring result;

		for (const auto& node : nodes)
		{
			TVITEMW get{};
			get.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
                        get.stateMask = TVIS_EXPANDED | TVIS_EXPANDPARTIAL;
			get.hItem = node.second;
			wchar_t text[1024] = { '\0' };
			get.pszText = text;
			get.cchTextMax = 1024;
			::SendMessage(_hTree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&get));

			for (int i = 0; i < node.first; ++i)
				result += L"  ";

			result += L"idx=" + std::to_wstring(get.lParam);
			result += L" depth=" + std::to_wstring(node.first);
			result += L" line=" + std::to_wstring(_lineNumbers[static_cast<size_t>(get.lParam)]);
			result += L" expanded=" + std::to_wstring((get.state & TVIS_EXPANDED) ? 1 : 0);
			result += L" | " + std::wstring(text) + L"\n";
		}
		return result;
	}

	bool OutlinePanel::jumpToIndex(size_t index)
	{
		if (!_hTree || index >= _lineNumbers.size())
			return false;

		if (_jumpHandler)
			_jumpHandler(_lineNumbers[index]);
		return true;
	}

	void OutlinePanel::onTreeKeyDown(LPNMTVKEYDOWN keyDown)
	{
		if (!keyDown)
			return;

		if (keyDown->wVKey == VK_RETURN)
		{
			onTreeDoubleClick();
			::SetWindowLongPtr(_hSelf, DWLP_MSGRESULT, 1); // swallow the key
		}
	}

	LRESULT CALLBACK OutlinePanel::windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		OutlinePanel* self = reinterpret_cast<OutlinePanel*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
		if (!self)
			return ::DefWindowProc(hWnd, uMsg, wParam, lParam);

		return self->handleMessage(hWnd, uMsg, wParam, lParam);
	}

	LRESULT OutlinePanel::handleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
			case WM_SIZE:
				layout();
				return 0;

			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				if (HIWORD(wParam) == BN_CLICKED)
				{
					if (id == ID_EXPAND_ALL)
					{
						_policy = ExpansionPolicy::All;
						expandAll();
						return 0;
					}
					if (id == ID_COLLAPSE_ALL)
					{
						_policy = ExpansionPolicy::None;
						collapseAll();
						return 0;
					}
				}
				break;
			}

			case WM_TIMER:
			{
				if (wParam == TIMER_REFRESH)
				{
					::KillTimer(hWnd, TIMER_REFRESH);
					if (_refreshHandler)
						_refreshHandler();
				}
				else if (wParam == TIMER_TIMEOUT)
				{
					::KillTimer(hWnd, TIMER_TIMEOUT);
					if (_timeoutHandler)
						_timeoutHandler();
				}
				return 0;
			}

			case WM_NOTIFY:
			{
				auto* header = reinterpret_cast<NMHDR*>(lParam);
				if (header->hwndFrom == _hTree)
				{
					switch (header->code)
					{
						case NM_DBLCLK:
							onTreeDoubleClick();
							return 0;

						case TVN_KEYDOWN:
							onTreeKeyDown(reinterpret_cast<LPNMTVKEYDOWN>(lParam));
							return 0;

						case TVN_ITEMEXPANDED:
						{
							auto* action = reinterpret_cast<NMTREEVIEW*>(lParam);
							rememberExpandedState(action->itemNew.hItem);
							return 0;
						}
					}
				}
				return 0;
			}

			case WM_SETFOCUS:
				if (_hTree)
					::SetFocus(_hTree);
				return 0;
		}

		return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
}
