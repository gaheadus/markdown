// MarkdownOutline plugin for Notepad++
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "MarkdownParser.h"

#include <algorithm>
#include <cwctype>

namespace MdOutline
{
	namespace
	{
		bool isSpace(wchar_t c)
		{
			return c == L' ' || c == L'\t';
		}

		// Returns the number of leading spaces of the line, counting a tab as 4 columns.
		size_t countIndent(const std::wstring& line)
		{
			size_t indent = 0;
			for (wchar_t c : line)
			{
				if (c == L' ')
					++indent;
				else if (c == L'\t')
					indent += 4;
				else
					break;
			}
			return indent;
		}

		void trimRight(std::wstring& s)
		{
			while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r'))
				s.pop_back();
		}

		void trimLeft(std::wstring& s)
		{
			size_t start = 0;
			while (start < s.size() && isSpace(s[start]))
				++start;
			s.erase(0, start);
		}

		// Removes the optional closing sequence of an ATX heading ("## Title ##").
		std::wstring stripClosingSequence(std::wstring text)
		{
			trimRight(text);
			if (text.empty())
				return text;

			wchar_t last = text.back();
			if (last != L'#')
				return text;

			size_t end = text.size();
			while (end > 0 && text[end - 1] == L'#')
				--end;

			// The closing sequence must be preceded by a space (or be the whole text).
			if (end == 0)
				return text;

			if (isSpace(text[end - 1]))
				text.erase(end - 1);

			return text;
		}

		bool isFenceMarker(const std::wstring& trimmed, std::wstring& fence)
		{
			if (trimmed.size() < 3)
				return false;

			wchar_t c = trimmed[0];
			if ((c != L'`' && c != L'~') || c != trimmed[1] || c != trimmed[2])
				return false;

			size_t count = 0;
			while (count < trimmed.size() && trimmed[count] == c)
				++count;

			fence.assign(count, c);
			return true;
		}

		// Returns the Setext level of the given line, or 0 when the line is not a Setext underline.
		int getSetextLevel(const std::wstring& trimmed)
		{
			if (trimmed.size() < 1)
				return 0;

			wchar_t c = trimmed[0];
			if (c != L'=' && c != L'-')
				return 0;

			for (wchar_t ch : trimmed)
			{
				if (ch != c)
					return 0;
			}
			return (c == L'=') ? 1 : 2;
		}
	}

	void parseHeadings(const std::wstring& text, std::vector<Heading>& headings)
	{
		headings.clear();

		// Split the document into lines. Both CRLF and LF are accepted.
		std::vector<std::wstring> lines;
		{
			std::wstring current;
			for (wchar_t c : text)
			{
				if (c == L'\r')
					continue;
				if (c == L'\n')
				{
					lines.push_back(current);
					current.clear();
				}
				else
				{
					current.push_back(c);
				}
			}
			lines.push_back(current);
		}

		std::wstring openFence;
		bool previousLineIsParagraph = false;
		std::wstring previousLineText;
		size_t previousLineIndex = 0;

		for (size_t i = 0; i < lines.size(); ++i)
		{
			const std::wstring& rawLine = lines[i];
			std::wstring trimmed = rawLine;
			trimLeft(trimmed);
			trimRight(trimmed);

			// Fenced code block: everything until the closing fence is code.
			if (!openFence.empty())
			{
				std::wstring fence;
				if (isFenceMarker(trimmed, fence) && fence.size() >= openFence.size() && trimmed.size() == fence.size())
					openFence.clear();
				continue;
			}

			std::wstring fence;
			if (isFenceMarker(trimmed, fence))
			{
				openFence = fence;
				previousLineIsParagraph = false;
				continue;
			}

			// An indented code block (4 spaces or a tab) is code as well.
			const size_t indent = countIndent(rawLine);
			const bool isBlank = trimmed.empty();
			if (!isBlank && indent >= 4)
			{
				previousLineIsParagraph = false;
				continue;
			}

			if (isBlank)
			{
				previousLineIsParagraph = false;
				continue;
			}

			// Setext heading: the previous line was a paragraph and this line is "===" or "---".
			const int setextLevel = getSetextLevel(trimmed);
			if (setextLevel != 0)
			{
				if (previousLineIsParagraph)
				{
					Heading heading;
					heading._level = setextLevel;
					heading._line = static_cast<int>(previousLineIndex);
					heading._text = previousLineText;
					headings.push_back(heading);
				}
				previousLineIsParagraph = false;
				continue;
			}

			// ATX heading: up to 3 leading spaces, then 1 to 6 '#', then a space or the end of the line.
			if (indent <= 3 && trimmed[0] == L'#')
			{
				size_t hashes = 0;
				while (hashes < trimmed.size() && trimmed[hashes] == L'#')
					++hashes;

				if (hashes >= 1 && hashes <= 6)
				{
					const bool hasSpace = (hashes == trimmed.size()) || isSpace(trimmed[hashes]);
					if (hasSpace)
					{
						Heading heading;
						heading._level = static_cast<int>(hashes);
						heading._line = static_cast<int>(i);
						heading._text = stripClosingSequence(trimmed.substr(hashes));
						trimLeft(heading._text);
						headings.push_back(heading);
						previousLineIsParagraph = false;
						continue;
					}
				}
			}

			// Any other non-blank line is a possible paragraph, so it may become a Setext heading.
			previousLineIsParagraph = true;
			previousLineText = trimmed;
			previousLineIndex = i;
		}
	}

	std::wstring getFileExtension(const std::wstring& filePath)
	{
		size_t dot = filePath.find_last_of(L'.');
		if (dot == std::wstring::npos)
			return std::wstring();

		size_t slash = filePath.find_last_of(L"\\/");
		if (slash != std::wstring::npos && dot < slash)
			return std::wstring();

		std::wstring ext = filePath.substr(dot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
		return ext;
	}

	bool isMarkdownFile(const std::wstring& filePath)
	{
		const std::wstring ext = getFileExtension(filePath);
		return ext == L"md" || ext == L"markdown" || ext == L"mdown" || ext == L"mkd";
	}
}
