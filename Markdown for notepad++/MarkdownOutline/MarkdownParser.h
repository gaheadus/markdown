// MarkdownOutline plugin for Notepad++
// Shows a hierarchical outline (headings) of the current Markdown document
// in a dockable panel.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#pragma once

#include <windows.h>

#include <commctrl.h>

#include <string>
#include <vector>

// The plugin window class and the tree control it owns.
namespace MdOutline
{
	const wchar_t* const PANEL_TITLE = L"Markdown Outline";
	const wchar_t* const MODULE_NAME = L"MarkdownOutline.dll";

	// One heading found in the document.
	struct Heading
	{
		int _level = 0;      // 1 to 6
		int _line = 0;       // 0-based line number
		std::wstring _text;  // heading text, without the leading '#'
	};

	// Extracts the headings of a Markdown document.
	//
	// Recognized forms:
	//   - ATX headings:      "## Some heading"
	//   - Setext headings:   a paragraph line followed by "===" (level 1) or "---" (level 2)
	//
	// Headings inside fenced code blocks (``` or ~~~) and inside indented
	// code blocks are ignored.
	void parseHeadings(const std::wstring& text, std::vector<Heading>& headings);

	// Returns the extension of the given file path, in lower case, without the dot.
	std::wstring getFileExtension(const std::wstring& filePath);

	// Returns true when the given file path looks like a Markdown document.
	bool isMarkdownFile(const std::wstring& filePath);
}
