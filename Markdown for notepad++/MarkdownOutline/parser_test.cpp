// Standalone test harness for the Markdown heading parser.
// Reads a file given on the command line and prints the headings it finds.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "MarkdownParser.h"

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cout << "usage: parser_test <file>" << std::endl;
		return 1;
	}

	std::ifstream file;
	file.open(argv[1], std::ios::binary);
	if (!file)
	{
		std::cout << "cannot open " << argv[1] << std::endl;
		return 1;
	}

	std::ostringstream buffer;
	buffer << file.rdbuf();
	const std::string raw = buffer.str();

	// The file is UTF-8, so decode it the same way the plugin decodes the editor content.
	const int charCount = ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
	std::wstring text(static_cast<size_t>(charCount), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), &text[0], charCount);

	std::vector<MdOutline::Heading> headings;
	MdOutline::parseHeadings(text, headings);

	std::cout << headings.size() << " heading(s) found\n";
	for (const MdOutline::Heading& heading : headings)
	{
		std::wcout << L"H" << heading._level << L"  line " << heading._line << L"  " << heading._text << L"\n";
	}
	return 0;
}
