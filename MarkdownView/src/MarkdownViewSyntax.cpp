// The markdown view's built-in syntax highlighting (see MarkdownViewSyntax.h).

#include "MarkdownViewSyntax.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace {

struct Language {
	const wchar_t* names;          // the info strings that name it, separated by spaces
	const wchar_t* lineComment;
	const wchar_t* lineComment2;
	const wchar_t* blockStart;
	const wchar_t* blockEnd;
	const wchar_t* quotes;
	bool ignoreCase;
	bool hashPreprocessor;         // #include, #define ... at a line's start
	bool variables;                // $name
	bool tripleQuotes;             // """ and '''
	bool markup;
	const wchar_t* keywords;
	const wchar_t* types;
};

const Language g_languages[] = {
	{ L"c cpp c++ cc cxx h hpp hxx", L"//", nullptr, L"/*", L"*/", L"\"'", false, true, false, false, false,
		L"alignas alignof asm auto break case catch class concept const consteval constexpr constinit const_cast continue "
		L"co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern false final "
		L"for friend goto if inline mutable namespace new noexcept nullptr operator override private protected public "
		L"register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this "
		L"thread_local throw true try typedef typeid typename union using virtual volatile while",
		L"bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t "
		L"int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t BOOL BYTE DWORD HANDLE HRESULT HWND LPARAM "
		L"LPCWSTR LPWSTR LRESULT UINT WPARAM WORD" },
	{ L"cs csharp c#", L"//", nullptr, L"/*", L"*/", L"\"'", false, true, false, false, false,
		L"abstract as async await base break case catch checked class const continue default delegate do else enum event "
		L"explicit extern false finally fixed for foreach get goto if implicit in init interface internal is lock "
		L"namespace new null operator out override params private protected public readonly record ref return sealed set "
		L"sizeof stackalloc static struct switch this throw true try typeof unchecked unsafe using value var virtual "
		L"volatile when where while yield",
		L"bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint ulong ushort void" },
	{ L"java kotlin kt", L"//", nullptr, L"/*", L"*/", L"\"'", false, false, false, false, false,
		L"abstract assert break case catch class const continue default do else enum extends final finally for fun goto "
		L"if implements import instanceof interface is native new null object package private protected public return "
		L"static super switch synchronized this throw throws transient true false try val var void volatile when while",
		L"boolean byte char double float int long short String Int Long Boolean Unit Any" },
	{ L"js javascript jsx mjs cjs ts typescript tsx", L"//", nullptr, L"/*", L"*/", L"\"'`", false, false, false, false, false,
		L"abstract as async await break case catch class const continue debugger declare default delete do else enum "
		L"export extends false finally for from function get if implements import in instanceof interface let new null "
		L"of package private protected public readonly return set static super switch this throw true try type typeof "
		L"undefined var void while with yield",
		L"any boolean never number object string symbol unknown Array Map Promise Set" },
	{ L"json jsonc json5", L"//", nullptr, L"/*", L"*/", L"\"", false, false, false, false, false,
		L"true false null", L"" },
	{ L"py python python3", L"#", nullptr, nullptr, nullptr, L"\"'", false, false, false, true, false,
		L"and as assert async await break class continue def del elif else except False finally for from global if "
		L"import in is lambda None nonlocal not or pass raise return self True try while with yield match case",
		L"bool bytes dict float int list object set str tuple" },
	{ L"ps ps1 psm1 powershell pwsh", L"#", nullptr, L"<#", L"#>", L"\"'", true, false, true, false, false,
		L"begin break catch class continue data do dynamicparam else elseif end enum exit filter finally for foreach "
		L"from function if in param process return switch throw trap try until using while -eq -ne -gt -ge -lt -le "
		L"-like -notlike -match -notmatch -and -or -not -contains -replace -split -join",
		L"" },
	{ L"sh bash shell zsh console", L"#", nullptr, nullptr, nullptr, L"\"'", false, false, true, false, false,
		L"case do done elif else esac export fi for function if in local return select then until while echo cd exit "
		L"source sudo",
		L"" },
	{ L"rust rs", L"//", nullptr, L"/*", L"*/", L"\"", false, false, false, false, false,
		L"as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move "
		L"mut pub ref return self Self static struct super trait true type unsafe use where while",
		L"bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize String Vec Option Result Box" },
	{ L"go golang", L"//", nullptr, L"/*", L"*/", L"\"'`", false, false, false, false, false,
		L"break case chan const continue default defer else fallthrough false for func go goto if import interface iota "
		L"map nil package range return select struct switch true type var",
		L"bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 "
		L"uint32 uint64 uintptr" },
	{ L"sql mysql tsql psql", L"--", nullptr, L"/*", L"*/", L"'\"", true, false, false, false, false,
		L"add all alter and as asc begin between by case create delete desc distinct drop else end exists from group "
		L"having in index inner insert into is join key left like limit not null on or order outer primary references "
		L"right select set table then top union unique update values view when where with",
		L"bigint bit char date datetime decimal float int integer money nchar nvarchar real smallint text varchar" },
	{ L"css scss less", nullptr, nullptr, L"/*", L"*/", L"\"'", true, false, false, false, false,
		L"important inherit initial none auto", L"" },
	{ L"xml html htm xhtml svg xaml vue", nullptr, nullptr, L"<!--", L"-->", L"\"'", true, false, false, false, true,
		L"", L"" },
	{ L"yaml yml toml ini", L"#", L";", nullptr, nullptr, L"\"'", false, false, false, false, false,
		L"true false null yes no on off", L"" },
};

std::wstring Lower(std::wstring_view s) {
	std::wstring out(s);
	for (auto& ch : out)
		ch = (wchar_t)std::towlower(ch);
	return out;
}

bool InList(const wchar_t* list, std::wstring_view word, bool ignoreCase) {
	if (!list || word.empty())
		return false;
	const std::wstring w = ignoreCase ? Lower(word) : std::wstring(word);
	for (const wchar_t* p = list; *p;) {
		while (*p == L' ')
			p++;
		const wchar_t* e = p;
		while (*e && *e != L' ')
			e++;
		if ((size_t)(e - p) == w.size()) {
			bool same = true;
			for (size_t i = 0; i < w.size() && same; i++)
				same = (ignoreCase ? (wchar_t)std::towlower(p[i]) : p[i]) == w[i];
			if (same)
				return true;
		}
		p = e;
	}
	return false;
}

const Language* Find(std::wstring_view lang) {
	if (lang.empty())
		return nullptr;
	for (auto& l : g_languages)
		if (InList(l.names, lang, true))
			return &l;
	return nullptr;
}

bool StartsWith(std::wstring_view code, size_t i, const wchar_t* s) {
	if (!s)
		return false;
	const size_t n = wcslen(s);
	return code.substr(i, n) == std::wstring_view(s, n);
}

bool IsIdentStart(wchar_t ch) { return std::iswalpha(ch) || ch == L'_'; }
bool IsIdent(wchar_t ch) { return std::iswalnum(ch) || ch == L'_'; }

void Markup(std::wstring_view code, std::vector<MdvToken>& out) {
	const size_t n = code.size();
	for (size_t i = 0; i < n;) {
		if (StartsWith(code, i, L"<!--")) {
			const size_t e = code.find(L"-->", i + 4);
			const size_t end = e == std::wstring_view::npos ? n : e + 3;
			out.push_back({ i, end - i, MdvTokenKind::Comment });
			i = end;
			continue;
		}
		if (code[i] != L'<') {
			i++;
			continue;
		}
		// <name, </name, <?xml, <!DOCTYPE
		size_t p = i + 1;
		if (p < n && (code[p] == L'/' || code[p] == L'?' || code[p] == L'!'))
			p++;
		size_t nameEnd = p;
		while (nameEnd < n && (IsIdent(code[nameEnd]) || code[nameEnd] == L'-' || code[nameEnd] == L':' || code[nameEnd] == L'.'))
			nameEnd++;
		if (nameEnd == p) {
			i++;
			continue;
		}
		out.push_back({ p, nameEnd - p, code[i + 1] == L'?' || code[i + 1] == L'!' ? MdvTokenKind::Preprocessor : MdvTokenKind::Tag });
		// attributes and their values, to the tag's end
		size_t k = nameEnd;
		while (k < n && code[k] != L'>') {
			if (code[k] == L'"' || code[k] == L'\'') {
				const wchar_t q = code[k];
				size_t e = code.find(q, k + 1);
				e = e == std::wstring_view::npos ? n : e + 1;
				out.push_back({ k, e - k, MdvTokenKind::String });
				k = e;
				continue;
			}
			if (IsIdentStart(code[k])) {
				size_t e = k;
				while (e < n && (IsIdent(code[e]) || code[e] == L'-' || code[e] == L':'))
					e++;
				out.push_back({ k, e - k, MdvTokenKind::Attribute });
				k = e;
				continue;
			}
			if (code[k] == L'<')
				break;
			k++;
		}
		i = k < n && code[k] == L'>' ? k + 1 : k;
	}
}

}  // namespace

bool MdvKnownLanguage(std::wstring_view lang) {
	return Find(lang) != nullptr;
}

std::vector<MdvToken> MdvHighlight(std::wstring_view lang, std::wstring_view code) {
	std::vector<MdvToken> out;
	const Language* l = Find(lang);
	if (!l)
		return out;
	if (l->markup) {
		Markup(code, out);
		return out;
	}
	const size_t n = code.size();
	bool lineStart = true;
	for (size_t i = 0; i < n;) {
		const wchar_t ch = code[i];
		if (ch == L'\n') {
			lineStart = true;
			i++;
			continue;
		}
		if (ch == L' ' || ch == L'\t' || ch == L'\r') {
			i++;
			continue;
		}
		const bool atLineStart = lineStart;
		lineStart = false;
		if (StartsWith(code, i, l->blockStart)) {
			const size_t e = code.find(l->blockEnd, i + wcslen(l->blockStart));
			const size_t end = e == std::wstring_view::npos ? n : e + wcslen(l->blockEnd);
			out.push_back({ i, end - i, MdvTokenKind::Comment });
			i = end;
			continue;
		}
		if (StartsWith(code, i, l->lineComment) || StartsWith(code, i, l->lineComment2)) {
			size_t e = code.find(L'\n', i);
			e = e == std::wstring_view::npos ? n : e;
			out.push_back({ i, e - i, MdvTokenKind::Comment });
			i = e;
			continue;
		}
		if (l->hashPreprocessor && ch == L'#' && atLineStart) {
			// to the line's end, with its continuations
			size_t e = i;
			while (e < n) {
				const size_t nl = code.find(L'\n', e);
				if (nl == std::wstring_view::npos) {
					e = n;
					break;
				}
				size_t back = nl;
				while (back > e && (code[back - 1] == L'\r' || code[back - 1] == L' '))
					back--;
				if (back > e && code[back - 1] == L'\\') {
					e = nl + 1;
					continue;
				}
				e = nl;
				break;
			}
			// a comment after it is a comment
			const size_t comment = l->lineComment ? code.substr(i, e - i).find(l->lineComment) : std::wstring_view::npos;
			const size_t pe = comment == std::wstring_view::npos ? e : i + comment;
			out.push_back({ i, pe - i, MdvTokenKind::Preprocessor });
			i = pe;
			continue;
		}
		if (l->tripleQuotes && (StartsWith(code, i, L"\"\"\"") || StartsWith(code, i, L"'''"))) {
			const std::wstring_view q = code.substr(i, 3);
			const size_t e = code.find(q, i + 3);
			const size_t end = e == std::wstring_view::npos ? n : e + 3;
			out.push_back({ i, end - i, MdvTokenKind::String });
			i = end;
			continue;
		}
		if (wcschr(l->quotes, ch) && ch) {
			size_t e = i + 1;
			while (e < n && code[e] != ch && (code[e] != L'\n' || ch == L'`')) {
				if (code[e] == L'\\' && e + 1 < n)
					e++;
				e++;
			}
			e = e < n && code[e] == ch ? e + 1 : e;
			out.push_back({ i, e - i, MdvTokenKind::String });
			i = e;
			continue;
		}
		if (l->variables && ch == L'$' && i + 1 < n && (IsIdentStart(code[i + 1]) || code[i + 1] == L'{')) {
			size_t e = i + 1;
			if (code[e] == L'{') {
				const size_t close = code.find(L'}', e);
				e = close == std::wstring_view::npos ? n : close + 1;
			}
			else
				while (e < n && (IsIdent(code[e]) || code[e] == L':'))
					e++;
			out.push_back({ i, e - i, MdvTokenKind::Type });
			i = e;
			continue;
		}
		if (std::iswdigit(ch) || (ch == L'.' && i + 1 < n && std::iswdigit(code[i + 1]))) {
			// not inside an identifier ("x1")
			if (i > 0 && IsIdent(code[i - 1])) {
				i++;
				continue;
			}
			size_t e = i + 1;
			while (e < n && (std::iswalnum(code[e]) || code[e] == L'.' || code[e] == L'_' || (code[e] == L'\'' && l->hashPreprocessor)))
				e++;
			out.push_back({ i, e - i, MdvTokenKind::Number });
			i = e;
			continue;
		}
		if (IsIdentStart(ch) || (ch == L'-' && l->ignoreCase && l->variables && i + 1 < n && std::iswalpha(code[i + 1]))) {
			size_t e = i + 1;
			while (e < n && IsIdent(code[e]))
				e++;
			const std::wstring_view word = code.substr(i, e - i);
			if (InList(l->keywords, word, l->ignoreCase))
				out.push_back({ i, e - i, MdvTokenKind::Keyword });
			else if (InList(l->types, word, l->ignoreCase))
				out.push_back({ i, e - i, MdvTokenKind::Type });
			i = e;
			continue;
		}
		i++;
	}
	return out;
}
