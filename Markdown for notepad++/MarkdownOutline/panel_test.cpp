// Standalone test of the outline panel: it creates the panel off screen,
// feeds it headings and checks the expansion behaviour.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include <windows.h>

#include <commctrl.h>

#include <iostream>
#include <string>
#include <vector>

#include "MarkdownParser.h"
#include "OutlinePanel.h"

namespace
{
	int g_failures = 0;

	void check(bool condition, const char* what)
	{
		std::cout << (condition ? "[ OK ] " : "[FAIL] ") << what << "\n";
		if (!condition)
			++g_failures;
	}

	std::string toUtf8(const std::wstring& text)
	{
		if (text.empty())
			return std::string();
		const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr);
		return result;
	}
}

int main()
{
	INITCOMMONCONTROLSEX icc{};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES;
	::InitCommonControlsEx(&icc);

	HWND host = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 400, 400, ::GetDesktopWindow(), nullptr, nullptr, nullptr);

	MdOutline::OutlinePanel panel;
	if (!panel.create(host, ::GetModuleHandle(nullptr)))
	{
		std::cout << "cannot create the panel\n";
		return 1;
	}

	// --- the headings of a small document ------------------------------------
	std::vector<MdOutline::Heading> headings;
	{
		MdOutline::Heading h1; h1._level = 1; h1._line = 0;   h1._text = L"Title";
		MdOutline::Heading h2; h2._level = 2; h2._line = 10;  h2._text = L"Section";
		MdOutline::Heading h3; h3._level = 3; h3._line = 20;  h3._text = L"Subsection";
		MdOutline::Heading h4; h4._level = 4; h4._line = 30;  h4._text = L"Detail";
		MdOutline::Heading h2b; h2b._level = 2; h2b._line = 40; h2b._text = L"Other";
		headings.push_back(h1);
		headings.push_back(h2);
		headings.push_back(h3);
		headings.push_back(h4);
		headings.push_back(h2b);
	}

	panel.setHeadings(headings);
	std::wstring tree = panel.dumpTree();
	std::cout << "--- default (level 2) ---\n" << toUtf8(tree) << "\n";

	// With a default level of 2, the depth 0 node is expanded and the depth 1
	// node is collapsed.
	check(tree.find(L"depth=0 line=0 expanded=1") != std::wstring::npos, "the top level heading is expanded");
	check(tree.find(L"depth=1 line=10 expanded=0") != std::wstring::npos, "the second level heading is collapsed");
	check(tree.find(L"depth=2 line=20 expanded=0") != std::wstring::npos, "the third level heading is collapsed");

	// --- the expansion survives a rebuild ------------------------------------
	panel.expandAll();
	tree = panel.dumpTree();
	check(tree.find(L"depth=2 line=20 expanded=1") != std::wstring::npos, "expand all expands every node");

	// The user edits the document: the tree is rebuilt with the same headings.
	panel.setHeadings(headings);
	tree = panel.dumpTree();
	check(tree.find(L"depth=2 line=20 expanded=1") != std::wstring::npos, "the expansion state survives the rebuild");
	check(tree.find(L"depth=0 line=0 expanded=1") != std::wstring::npos, "the top level is still expanded");

	// --- collapse all --------------------------------------------------------
	panel.collapseAll();
	tree = panel.dumpTree();
	check(tree.find(L"depth=0 line=0 expanded=0") != std::wstring::npos, "collapse all collapses the top level");

	// The collapsed state has to survive the rebuild too.
	panel.setHeadings(headings);
	tree = panel.dumpTree();
	check(tree.find(L"depth=0 line=0 expanded=0") != std::wstring::npos, "the collapsed state survives the rebuild");
	check(tree.find(L"depth=2 line=20 expanded=0") != std::wstring::npos, "the children stay collapsed after the rebuild");

	// --- a new heading gets the default policy ------------------------------
	{
		MdOutline::Heading h2c; h2c._level = 2; h2c._line = 50; h2c._text = L"Fresh";
		std::vector<MdOutline::Heading> more = headings;
		more.push_back(h2c);
		panel.setHeadings(more);
		tree = panel.dumpTree();
		check(tree.find(L"| Fresh") != std::wstring::npos, "the new heading is in the tree");
		check(tree.find(L"depth=1 line=50 expanded=0") != std::wstring::npos, "the new heading uses the default policy");
	}

	// --- the default level can be changed ------------------------------------
	panel.setDefaultExpandLevel(3);
	panel.expandAll();
	panel.collapseAll();
	{
		// A tree which was never seen before uses the new default level.
		MdOutline::OutlinePanel fresh;
		fresh.create(host, ::GetModuleHandle(nullptr));
		fresh.setDefaultExpandLevel(3);
		fresh.setHeadings(headings);
		std::wstring freshTree = fresh.dumpTree();
		check(freshTree.find(L"depth=1 line=10 expanded=1") != std::wstring::npos, "a default level of 3 expands the second level");
		check(freshTree.find(L"depth=2 line=20 expanded=0") != std::wstring::npos, "a default level of 3 keeps the third level collapsed");
		fresh.destroy();
	}

	// --- the buttons of the panel --------------------------------------------
	{
		MdOutline::OutlinePanel buttonPanel;
		buttonPanel.create(host, ::GetModuleHandle(nullptr));
		buttonPanel.setHeadings(headings);

		const HWND expandButton = ::GetDlgItem(buttonPanel.getHSelf(), 1002);
		const HWND collapseButton = ::GetDlgItem(buttonPanel.getHSelf(), 1003);
		check(expandButton != nullptr, "the expand all button exists");
		check(collapseButton != nullptr, "the collapse all button exists");

		// The tree must not cover the buttons.
		RECT buttonRect{};
		RECT treeRect{};
		::GetWindowRect(expandButton, &buttonRect);
		::GetWindowRect(buttonPanel.getTreeHandle(), &treeRect);
		check(treeRect.top >= buttonRect.bottom, "the tree is placed below the buttons");

		// Clicking the buttons must change the expansion.
		::SendMessage(expandButton, BM_CLICK, 0, 0);
		std::wstring tree2 = buttonPanel.dumpTree();
		check(tree2.find(L"depth=2 line=20 expanded=1") != std::wstring::npos, "the expand all button expands the tree");

		::SendMessage(collapseButton, BM_CLICK, 0, 0);
		tree2 = buttonPanel.dumpTree();
		check(tree2.find(L"depth=0 line=0 expanded=0") != std::wstring::npos, "the collapse all button collapses the tree");

		// The number of visible nodes tells whether the tree is really folded.
		auto countVisible = [&buttonPanel]() -> int
		{
			int count = 0;
			HTREEITEM item = reinterpret_cast<HTREEITEM>(::SendMessage(buttonPanel.getTreeHandle(), TVM_GETNEXTITEM, TVGN_ROOT, 0));
			while (item)
			{
				++count;
				item = reinterpret_cast<HTREEITEM>(::SendMessage(buttonPanel.getTreeHandle(), TVM_GETNEXTITEM, TVGN_NEXTVISIBLE, reinterpret_cast<LPARAM>(item)));
			}
			return count;
		};

		::SendMessage(collapseButton, BM_CLICK, 0, 0);
		const int visibleNone = countVisible();
		::SendMessage(expandButton, BM_CLICK, 0, 0);
		const int visibleExpanded = countVisible();
		::SendMessage(collapseButton, BM_CLICK, 0, 0);
		const int visibleCollapsed = countVisible();
		std::cout << "visible: " << visibleNone << " -> " << visibleExpanded << " -> " << visibleCollapsed << "\n";
		check(visibleExpanded == 5, "every node is visible when the tree is expanded");
		check(visibleCollapsed == 1, "only the top level heading is visible when the tree is collapsed");

		buttonPanel.destroy();
	}

	panel.destroy();
	::DestroyWindow(host);

	std::cout << (g_failures == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
	return g_failures == 0 ? 0 : 1;
}