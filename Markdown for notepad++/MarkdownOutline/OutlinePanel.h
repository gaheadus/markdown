// MarkdownOutline plugin for Notepad++
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "MarkdownParser.h"

namespace MdOutline
{
	// The policy applied to the headings which are not known yet.
	enum class ExpansionPolicy
	{
		// Use the default number of visible levels.
		Default,
		// Every heading is expanded.
		All,
		// Every heading is collapsed.
		None
	};

	// The dockable panel: a tree view showing the headings of the current Markdown document.
	class OutlinePanel
	{
	public:
		OutlinePanel() = default;
		~OutlinePanel() = default;

		// Creates the panel window (as a child of the given parent) and the tree view it contains.
		bool create(HWND parent, HINSTANCE hInst);
		void destroy();

		HWND getHSelf() const { return _hSelf; }
		HWND getTreeHandle() const { return _hTree; }

		// Replaces the content of the tree with the given headings.
		void setHeadings(const std::vector<Heading>& headings);

		// Empties the tree and shows the given hint text.
		void showMessage(const std::wstring& message);

		// Called when the user asks to jump to a heading: the argument is the 0-based line number.
		void setJumpHandler(std::function<void(int)> handler) { _jumpHandler = std::move(handler); }

		// Called when the panel is shown or when the document changed and a refresh is due.
		void setRefreshHandler(std::function<void()> handler) { _refreshHandler = std::move(handler); }

		// Asks for a refresh, but not more often than once every "delayMs" milliseconds.
		// Used to avoid a full re-scan on every keystroke.
		void scheduleRefresh(UINT delayMs);

		// Requests an immediate refresh.
		void refreshNow();

		// Runs the handler once, after the given delay.
		void scheduleTimeout(UINT delayMs);
		void setTimeoutHandler(std::function<void()> handler) { _timeoutHandler = std::move(handler); }

		// --- expansion ---------------------------------------------------------

		// The number of heading levels which are visible by default. With 2, the
		// top level headings are expanded (so the second level is visible) while
		// the second level itself stays collapsed.
		void setDefaultExpandLevel(int level) { _defaultExpandLevel = level; }
		int getDefaultExpandLevel() const { return _defaultExpandLevel; }

		// Expands every node of the tree.
		void expandAll();

		// Collapses every top level node, hiding all the children.
		void collapseAll();

		// --- helpers used by the self test -------------------------------------

		// Returns the tree content as text, one node per line, indented by depth.
		// The line number stored in the node is appended to each entry.
		std::wstring dumpTree() const;

		// Triggers the jump for the node with the given index, exactly like a
		// double click would do. Returns false when the index does not exist.
		bool jumpToIndex(size_t index);

	private:
		static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
		LRESULT handleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

		void layout();
		void onTreeDoubleClick();
		void onTreeKeyDown(LPNMTVKEYDOWN keyDown);

		// Depth first walk over the tree nodes.
		void collectNodes(HTREEITEM parent, int depth, std::vector<std::pair<int, HTREEITEM>>& nodes) const;

		// Remembers which nodes are expanded, so that the state survives the
		// rebuild which happens after every edit.
		void captureExpandedState();

		// Applies the expansion state to the nodes which have just been inserted.
		void applyExpandedState(const std::vector<std::pair<int, HTREEITEM>>& nodes);

		// Stores the expanded state of a single node.
		void rememberExpandedState(HTREEITEM item);

		// Builds the key which identifies the heading of the node.
		std::wstring makeStateKey(size_t index) const;

		HWND _hSelf = nullptr;
		HWND _hTree = nullptr;
		HWND _hExpandAll = nullptr;
		HWND _hCollapseAll = nullptr;

		std::function<void(int)> _jumpHandler;
		std::function<void()> _refreshHandler;
		std::function<void()> _timeoutHandler;

		// One entry per tree node, holding the line to jump to.
		std::vector<int> _lineNumbers;

		// One entry per tree node, holding a key which identifies the heading.
		// The key is built out of the heading text and the number of times this
		// very text was already seen, so that two identical headings do not
		// share the same expansion state.
		std::vector<std::wstring> _stateKeys;

		// The expanded state of the known headings: heading key -> expanded.
		std::map<std::wstring, bool> _expandedState;

		// The default number of visible heading levels.
		int _defaultExpandLevel = 2;

		// The policy applied to the headings which are not known yet.
		ExpansionPolicy _policy = ExpansionPolicy::Default;
	};
}
