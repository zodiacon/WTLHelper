#pragma once

// The markdown view's built-in syntax highlighting for code blocks: a small lexer per language (comments, strings,
// numbers, keywords, types, preprocessor lines; tags and attributes for markup). Window-free: covered by
// MarkdownViewTests.

#include <cstdint>
#include <string_view>
#include <vector>

enum class MdvTokenKind : uint8_t {     // the order of the control's MDVSYN_*
	Keyword,
	Type,              // a type name, or a variable in a shell language
	String,
	Comment,
	Number,
	Preprocessor,
	Tag,
	Attribute,
};

struct MdvToken {
	size_t start = 0;
	size_t length = 0;
	MdvTokenKind kind = MdvTokenKind::Keyword;
};

bool MdvKnownLanguage(std::wstring_view lang);   // "cpp", "c#", "js", "py", "ps1", "html" ...
std::vector<MdvToken> MdvHighlight(std::wstring_view lang, std::wstring_view code);
