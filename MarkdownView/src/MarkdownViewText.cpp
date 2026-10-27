// The markdown view's text helpers: raw HTML's tokens, entities, emoji shortcodes, URLs, images' sources.

#include "MarkdownViewModel.h"

#include <windows.h>
#include <pathcch.h>
#include <shlwapi.h>

#include <algorithm>
#include <iterator>

#pragma comment(lib, "pathcch.lib")
#pragma comment(lib, "shlwapi.lib")

extern "C" {
#include "md4c/entity.h"
}

// ---- HTML tokens -------------------------------------------------------------------

const std::wstring* MdvHtmlToken::Attribute(std::wstring_view key) const {
	for (auto& a : attributes)
		if (a.first == key)
			return &a.second;
	return nullptr;
}

static bool IsTagStart(wchar_t ch) {
	return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

static bool IsSpace(wchar_t ch) {
	return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'\f';
}

static std::wstring Lower(std::wstring_view s) {
	std::wstring out(s);
	for (auto& ch : out)
		if (ch >= L'A' && ch <= L'Z')
			ch += L'a' - L'A';
	return out;
}

static bool IsVoid(const std::wstring& name) {
	static const wchar_t* names[] = { L"br", L"img", L"hr", L"input", L"source", L"meta", L"link", L"wbr", L"area", L"col", L"embed" };
	for (auto n : names)
		if (name == n)
			return true;
	return false;
}

// a tag at pos (html[pos] == '<'); false if it isn't one
static bool ParseTag(std::wstring_view html, size_t pos, MdvHtmlToken& t) {
	size_t p = pos + 1;
	const bool end = p < html.size() && html[p] == L'/';
	if (end)
		p++;
	if (p >= html.size() || !IsTagStart(html[p]))
		return false;
	const size_t nameStart = p;
	while (p < html.size() && !IsSpace(html[p]) && html[p] != L'>' && html[p] != L'/')
		p++;
	t.type = end ? MdvHtmlToken::End : MdvHtmlToken::Start;
	t.name = Lower(html.substr(nameStart, p - nameStart));
	t.attributes.clear();
	t.selfClosing = false;
	for (;;) {
		while (p < html.size() && IsSpace(html[p]))
			p++;
		if (p >= html.size())
			return false;
		if (html[p] == L'>') {
			p++;
			break;
		}
		if (html[p] == L'/') {
			if (p + 1 < html.size() && html[p + 1] == L'>') {
				t.selfClosing = true;
				p += 2;
				break;
			}
			p++;
			continue;
		}
		const size_t attrStart = p;
		while (p < html.size() && !IsSpace(html[p]) && html[p] != L'=' && html[p] != L'>' && html[p] != L'/')
			p++;
		std::wstring name = Lower(html.substr(attrStart, p - attrStart));
		while (p < html.size() && IsSpace(html[p]))
			p++;
		std::wstring value;
		if (p < html.size() && html[p] == L'=') {
			p++;
			while (p < html.size() && IsSpace(html[p]))
				p++;
			if (p < html.size() && (html[p] == L'"' || html[p] == L'\'')) {
				const wchar_t quote = html[p++];
				const size_t v = p;
				while (p < html.size() && html[p] != quote)
					p++;
				if (p >= html.size())
					return false;
				value = MdvDecodeEntities(html.substr(v, p - v));
				p++;
			}
			else {
				const size_t v = p;
				while (p < html.size() && !IsSpace(html[p]) && html[p] != L'>')
					p++;
				value = MdvDecodeEntities(html.substr(v, p - v));
			}
		}
		if (!name.empty())
			t.attributes.emplace_back(std::move(name), std::move(value));
	}
	if (IsVoid(t.name))
		t.selfClosing = true;
	t.pos = pos;
	t.length = p - pos;
	return true;
}

std::vector<MdvHtmlToken> MdvTokenizeHtml(std::wstring_view html) {
	std::vector<MdvHtmlToken> tokens;
	size_t text = 0;
	auto flushText = [&](size_t end) {
		if (end > text) {
			MdvHtmlToken t;
			t.type = MdvHtmlToken::Text;
			t.pos = text;
			t.length = end - text;
			tokens.push_back(std::move(t));
		}
	};
	size_t p = 0;
	while (p < html.size()) {
		if (html[p] != L'<') {
			p++;
			continue;
		}
		MdvHtmlToken t;
		if (html.substr(p, 4) == L"<!--") {
			size_t e = html.find(L"-->", p + 4);
			e = e == std::wstring_view::npos ? html.size() : e + 3;
			flushText(p);
			t.type = MdvHtmlToken::Comment;
			t.pos = p;
			t.length = e - p;
			tokens.push_back(std::move(t));
			p = text = e;
			continue;
		}
		if (p + 1 < html.size() && (html[p + 1] == L'!' || html[p + 1] == L'?')) {
			size_t e = html.find(L'>', p);
			e = e == std::wstring_view::npos ? html.size() : e + 1;
			flushText(p);
			t.type = MdvHtmlToken::Comment;
			t.pos = p;
			t.length = e - p;
			tokens.push_back(std::move(t));
			p = text = e;
			continue;
		}
		if (!ParseTag(html, p, t)) {
			p++;
			continue;
		}
		flushText(p);
		p = text = t.pos + t.length;
		const bool raw = t.type == MdvHtmlToken::Start && !t.selfClosing && (t.name == L"script" || t.name == L"style");
		const std::wstring name = t.name;
		tokens.push_back(std::move(t));
		if (raw) {
			// skip to the end tag: nothing in it is shown
			const std::wstring close = L"</" + name;
			size_t e = p;
			for (; e < html.size(); e++)
				if (html[e] == L'<' && _wcsnicmp(html.data() + e, close.c_str(), close.size()) == 0)
					break;
			p = text = e;
		}
	}
	flushText(html.size());
	return tokens;
}

// ---- Entities -------------------------------------------------------------------------

static void AppendCodePoint(std::wstring& out, unsigned cp) {
	if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		cp = 0xFFFD;
	if (cp >= 0x10000) {
		cp -= 0x10000;
		out += (wchar_t)(0xD800 + (cp >> 10));
		out += (wchar_t)(0xDC00 + (cp & 0x3FF));
	}
	else
		out += (wchar_t)cp;
}

bool MdvDecodeEntity(std::wstring_view e, std::wstring& out) {
	if (e.size() < 3 || e.front() != L'&' || e.back() != L';')
		return false;
	if (e[1] == L'#') {
		unsigned cp = 0;
		size_t i = 2;
		const bool hex = i < e.size() && (e[i] == L'x' || e[i] == L'X');
		if (hex)
			i++;
		if (i >= e.size() - 1)
			return false;
		for (; i < e.size() - 1; i++) {
			const wchar_t ch = e[i];
			unsigned digit;
			if (ch >= L'0' && ch <= L'9')
				digit = ch - L'0';
			else if (hex && ch >= L'a' && ch <= L'f')
				digit = ch - L'a' + 10;
			else if (hex && ch >= L'A' && ch <= L'F')
				digit = ch - L'A' + 10;
			else
				return false;
			cp = cp * (hex ? 16 : 10) + digit;
			if (cp > 0x10FFFF)
				cp = 0x110000;
		}
		AppendCodePoint(out, cp);
		return true;
	}
	if (e.size() > 48)
		return false;
	char name[50];
	for (size_t i = 0; i < e.size(); i++) {
		if (e[i] > 127)
			return false;
		name[i] = (char)e[i];
	}
	const ENTITY* entity = entity_lookup(name, e.size());
	if (!entity)
		return false;
	AppendCodePoint(out, entity->codepoints[0]);
	if (entity->codepoints[1])
		AppendCodePoint(out, entity->codepoints[1]);
	return true;
}

std::wstring MdvDecodeEntities(std::wstring_view text) {
	std::wstring out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == L'&') {
			const size_t semi = text.find(L';', i);
			if (semi != std::wstring_view::npos && semi - i <= 48 && MdvDecodeEntity(text.substr(i, semi - i + 1), out)) {
				i = semi;
				continue;
			}
		}
		out += text[i];
	}
	return out;
}

// ---- Emoji ----------------------------------------------------------------------------

const wchar_t* MdvEmoji(std::wstring_view name) {
	// GitHub's most used shortcodes
	static const struct { const wchar_t* name; const wchar_t* emoji; } table[] = {
		{ L"+1", L"\U0001F44D" }, { L"-1", L"\U0001F44E" }, { L"100", L"\U0001F4AF" }, { L"alarm_clock", L"\u23F0" },
		{ L"angry", L"\U0001F620" }, { L"apple", L"\U0001F34E" }, { L"arrow_down", L"\u2B07\uFE0F" },
		{ L"arrow_left", L"\u2B05\uFE0F" }, { L"arrow_right", L"\u27A1\uFE0F" }, { L"arrow_up", L"\u2B06\uFE0F" },
		{ L"art", L"\U0001F3A8" }, { L"balloon", L"\U0001F388" }, { L"bang", L"\u2757" }, { L"beer", L"\U0001F37A" },
		{ L"bell", L"\U0001F514" }, { L"blush", L"\U0001F60A" }, { L"bomb", L"\U0001F4A3" }, { L"book", L"\U0001F4D6" },
		{ L"books", L"\U0001F4DA" }, { L"boom", L"\U0001F4A5" }, { L"bow", L"\U0001F647" }, { L"brain", L"\U0001F9E0" },
		{ L"broken_heart", L"\U0001F494" }, { L"bug", L"\U0001F41B" }, { L"bulb", L"\U0001F4A1" }, { L"calendar", L"\U0001F4C6" },
		{ L"camera", L"\U0001F4F7" }, { L"cat", L"\U0001F431" }, { L"chart_with_upwards_trend", L"\U0001F4C8" },
		{ L"check", L"\u2714\uFE0F" }, { L"clap", L"\U0001F44F" }, { L"clipboard", L"\U0001F4CB" }, { L"clock1", L"\U0001F550" },
		{ L"closed_book", L"\U0001F4D5" }, { L"cloud", L"\u2601\uFE0F" }, { L"coffee", L"\u2615" }, { L"computer", L"\U0001F4BB" },
		{ L"confused", L"\U0001F615" }, { L"construction", L"\U0001F6A7" }, { L"cool", L"\U0001F192" }, { L"cry", L"\U0001F622" },
		{ L"dart", L"\U0001F3AF" }, { L"dash", L"\U0001F4A8" }, { L"disappointed", L"\U0001F61E" }, { L"dizzy", L"\U0001F4AB" },
		{ L"dog", L"\U0001F436" }, { L"door", L"\U0001F6AA" }, { L"email", L"\U0001F4E7" }, { L"envelope", L"\u2709\uFE0F" },
		{ L"exclamation", L"\u2757" }, { L"eyes", L"\U0001F440" }, { L"facepalm", L"\U0001F926" }, { L"file_folder", L"\U0001F4C1" },
		{ L"fire", L"\U0001F525" }, { L"flashlight", L"\U0001F526" }, { L"floppy_disk", L"\U0001F4BE" }, { L"gear", L"\u2699\uFE0F" },
		{ L"gem", L"\U0001F48E" }, { L"ghost", L"\U0001F47B" }, { L"gift", L"\U0001F381" }, { L"globe_with_meridians", L"\U0001F310" },
		{ L"green_heart", L"\U0001F49A" }, { L"grin", L"\U0001F601" }, { L"grinning", L"\U0001F600" }, { L"hammer", L"\U0001F528" },
		{ L"hammer_and_wrench", L"\U0001F6E0\uFE0F" }, { L"hand", L"\u270B" }, { L"heart", L"\u2764\uFE0F" },
		{ L"heart_eyes", L"\U0001F60D" }, { L"heavy_check_mark", L"\u2714\uFE0F" }, { L"heavy_minus_sign", L"\u2796" },
		{ L"heavy_plus_sign", L"\u2795" }, { L"hourglass", L"\u231B" }, { L"house", L"\U0001F3E0" }, { L"hugs", L"\U0001F917" },
		{ L"inbox_tray", L"\U0001F4E5" }, { L"information_source", L"\u2139\uFE0F" }, { L"joy", L"\U0001F602" },
		{ L"key", L"\U0001F511" }, { L"keyboard", L"\u2328\uFE0F" }, { L"label", L"\U0001F3F7\uFE0F" }, { L"laughing", L"\U0001F606" },
		{ L"link", L"\U0001F517" }, { L"lipstick", L"\U0001F484" }, { L"lock", L"\U0001F512" }, { L"loudspeaker", L"\U0001F4E2" },
		{ L"mag", L"\U0001F50D" }, { L"mag_right", L"\U0001F50E" }, { L"mailbox", L"\U0001F4EB" }, { L"memo", L"\U0001F4DD" },
		{ L"microscope", L"\U0001F52C" }, { L"money_with_wings", L"\U0001F4B8" }, { L"moon", L"\U0001F314" },
		{ L"muscle", L"\U0001F4AA" }, { L"mute", L"\U0001F507" }, { L"new", L"\U0001F195" }, { L"no_entry", L"\u26D4" },
		{ L"no_entry_sign", L"\U0001F6AB" }, { L"notebook", L"\U0001F4D3" }, { L"ok", L"\U0001F197" }, { L"ok_hand", L"\U0001F44C" },
		{ L"open_book", L"\U0001F4D6" }, { L"outbox_tray", L"\U0001F4E4" }, { L"package", L"\U0001F4E6" },
		{ L"page_facing_up", L"\U0001F4C4" }, { L"paperclip", L"\U0001F4CE" }, { L"partying_face", L"\U0001F973" },
		{ L"pencil", L"\U0001F4DD" }, { L"pencil2", L"\u270F\uFE0F" }, { L"point_down", L"\U0001F447" },
		{ L"point_left", L"\U0001F448" }, { L"point_right", L"\U0001F449" }, { L"point_up", L"\u261D\uFE0F" },
		{ L"pray", L"\U0001F64F" }, { L"pushpin", L"\U0001F4CC" }, { L"question", L"\u2753" }, { L"rainbow", L"\U0001F308" },
		{ L"raised_hands", L"\U0001F64C" }, { L"recycle", L"\u267B\uFE0F" }, { L"red_circle", L"\U0001F534" },
		{ L"relaxed", L"\u263A\uFE0F" }, { L"rocket", L"\U0001F680" }, { L"rofl", L"\U0001F923" }, { L"rotating_light", L"\U0001F6A8" },
		{ L"scream", L"\U0001F631" }, { L"see_no_evil", L"\U0001F648" }, { L"shield", L"\U0001F6E1\uFE0F" },
		{ L"shipit", L"\U0001F43F\uFE0F" }, { L"slightly_smiling_face", L"\U0001F642" }, { L"smile", L"\U0001F604" },
		{ L"smiley", L"\U0001F603" }, { L"smirk", L"\U0001F60F" }, { L"snail", L"\U0001F40C" }, { L"snowflake", L"\u2744\uFE0F" },
		{ L"sob", L"\U0001F62D" }, { L"sparkles", L"\u2728" }, { L"speech_balloon", L"\U0001F4AC" }, { L"star", L"\u2B50" },
		{ L"star2", L"\U0001F31F" }, { L"stop_sign", L"\U0001F6D1" }, { L"sun_with_face", L"\U0001F31E" },
		{ L"sunglasses", L"\U0001F60E" }, { L"sunny", L"\u2600\uFE0F" }, { L"sweat_smile", L"\U0001F605" }, { L"tada", L"\U0001F389" },
		{ L"thinking", L"\U0001F914" }, { L"thumbsdown", L"\U0001F44E" }, { L"thumbsup", L"\U0001F44D" }, { L"tools", L"\U0001F6E0\uFE0F" },
		{ L"trophy", L"\U0001F3C6" }, { L"truck", L"\U0001F69A" }, { L"unlock", L"\U0001F513" }, { L"v", L"\u270C\uFE0F" },
		{ L"warning", L"\u26A0\uFE0F" }, { L"wave", L"\U0001F44B" }, { L"white_check_mark", L"\u2705" },
		{ L"wink", L"\U0001F609" }, { L"wrench", L"\U0001F527" }, { L"x", L"\u274C" }, { L"yellow_heart", L"\U0001F49B" },
		{ L"zap", L"\u26A1" }, { L"zzz", L"\U0001F4A4" },
	};
	for (auto& e : table)
		if (name == e.name)
			return e.emoji;
	return nullptr;
}

// ---- URLs -------------------------------------------------------------------------------

static int HexValue(wchar_t ch) {
	if (ch >= L'0' && ch <= L'9')
		return ch - L'0';
	if (ch >= L'a' && ch <= L'f')
		return ch - L'a' + 10;
	if (ch >= L'A' && ch <= L'F')
		return ch - L'A' + 10;
	return -1;
}

std::wstring MdvPercentDecode(std::wstring_view s) {
	if (s.find(L'%') == std::wstring_view::npos)
		return std::wstring(s);
	// as UTF-8 bytes, so %E2%9C%93 is one character
	std::string bytes;
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == L'%' && i + 2 < s.size() && HexValue(s[i + 1]) >= 0 && HexValue(s[i + 2]) >= 0) {
			bytes += (char)(HexValue(s[i + 1]) * 16 + HexValue(s[i + 2]));
			i += 2;
			continue;
		}
		char buf[8];
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, &s[i], 1, buf, sizeof(buf), nullptr, nullptr);
		if (n > 0)
			bytes.append(buf, n);
		else
			bytes += '?';
	}
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
	std::wstring out(n, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), out.data(), n);
	return out;
}

MdvUrlKind MdvClassifyUrl(std::wstring_view url) {
	// browsers drop control characters and spaces around a scheme ("java\tscript:")
	std::wstring clean;
	for (wchar_t ch : url)
		if (ch > L' ')
			clean += ch;
	if (clean.empty())
		return MdvUrlKind::File;
	if (clean[0] == L'#')
		return MdvUrlKind::Anchor;
	const size_t colon = clean.find(L':');
	size_t i = 0;
	if (colon != std::wstring::npos && colon > 1) {
		for (; i < colon; i++) {
			const wchar_t ch = clean[i];
			const bool alpha = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
			if (!alpha && (i == 0 || !((ch >= L'0' && ch <= L'9') || ch == L'+' || ch == L'-' || ch == L'.')))
				break;
		}
	}
	if (colon == std::wstring::npos || colon <= 1 || i < colon)
		return MdvUrlKind::File;                // relative, or a drive letter
	const std::wstring scheme = Lower(std::wstring_view(clean).substr(0, colon));
	if (scheme == L"javascript" || scheme == L"vbscript" || scheme == L"data")
		return MdvUrlKind::Unsafe;
	if (scheme == L"mailto")
		return MdvUrlKind::Mail;
	if (scheme == L"file")
		return MdvUrlKind::File;
	return MdvUrlKind::Web;
}

// ---- Images' sources ---------------------------------------------------------------------

bool MdvDecodeDataUrl(std::wstring_view url, std::string& bytes, std::wstring& mime) {
	bytes.clear();
	mime.clear();
	if (url.size() < 5 || _wcsnicmp(url.data(), L"data:", 5) != 0)
		return false;
	const size_t comma = url.find(L',');
	if (comma == std::wstring_view::npos)
		return false;
	std::wstring_view header = url.substr(5, comma - 5);
	bool base64 = false;
	if (header.size() >= 7 && _wcsnicmp(header.data() + header.size() - 7, L";base64", 7) == 0) {
		base64 = true;
		header.remove_suffix(7);
	}
	const size_t semi = header.find(L';');
	mime = Lower(semi == std::wstring_view::npos ? header : header.substr(0, semi));
	const std::wstring_view payload = url.substr(comma + 1);
	if (!base64) {
		const std::wstring text = MdvPercentDecode(payload);
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		bytes.resize(n);
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), bytes.data(), n, nullptr, nullptr);
		return true;
	}
	unsigned acc = 0;
	int bits = 0;
	for (wchar_t ch : payload) {
		int v;
		if (ch >= L'A' && ch <= L'Z')
			v = ch - L'A';
		else if (ch >= L'a' && ch <= L'z')
			v = ch - L'a' + 26;
		else if (ch >= L'0' && ch <= L'9')
			v = ch - L'0' + 52;
		else if (ch == L'+' || ch == L'-')
			v = 62;
		else if (ch == L'/' || ch == L'_')
			v = 63;
		else if (ch == L'=')
			break;
		else if (IsSpace(ch) || ch == L'%')
			continue;               // line breaks, %0A
		else
			return false;
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			bytes += (char)((acc >> bits) & 0xFF);
		}
	}
	return true;
}

std::wstring MdvResolvePath(std::wstring_view base, std::wstring_view url) {
	const MdvUrlKind kind = MdvClassifyUrl(url);
	if (kind != MdvUrlKind::File)
		return {};
	std::wstring path;
	if (url.size() >= 5 && _wcsnicmp(url.data(), L"file:", 5) == 0) {
		wchar_t buf[MAX_PATH * 2];
		DWORD n = (DWORD)std::size(buf);
		const std::wstring u(url);
		if (FAILED(::PathCreateFromUrlW(u.c_str(), buf, &n, 0)))
			return {};
		path = buf;
	}
	else {
		// no query or fragment; %20 and the like
		const size_t cut = url.find_first_of(L"?#");
		path = MdvPercentDecode(url.substr(0, cut));
		for (auto& ch : path)
			if (ch == L'/')
				ch = L'\\';
		const bool absolute = (path.size() >= 2 && path[1] == L':') || (!path.empty() && path[0] == L'\\');
		if (!absolute) {
			if (base.empty())
				return {};
			std::wstring joined(base);
			if (!joined.empty() && joined.back() != L'\\' && joined.back() != L'/')
				joined += L'\\';
			path = joined + path;
		}
	}
	if (path.empty())
		return {};
	wchar_t* canonical = nullptr;
	if (FAILED(::PathAllocCanonicalize(path.c_str(), PATHCCH_ALLOW_LONG_PATHS, &canonical)))
		return path;
	std::wstring result = canonical;
	::LocalFree(canonical);
	return result;
}

bool MdvIsSvg(std::string_view bytes) {
	// after a BOM, whitespace, <?xml ...?> and comments: <svg
	size_t i = bytes.size() >= 3 && (BYTE)bytes[0] == 0xEF && (BYTE)bytes[1] == 0xBB && (BYTE)bytes[2] == 0xBF ? 3 : 0;
	const size_t limit = std::min<size_t>(bytes.size(), 4096);
	while (i < limit) {
		const char ch = bytes[i];
		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
			i++;
			continue;
		}
		if (ch != '<')
			return false;
		if (bytes.substr(i, 4) == "<svg")
			return true;
		if (bytes.substr(i, 2) == "<?" || bytes.substr(i, 2) == "<!") {
			const size_t e = bytes.find('>', i);
			if (e == std::string_view::npos)
				return false;
			i = e + 1;
			continue;
		}
		return false;
	}
	return false;
}

static float CssLength(const std::wstring* value, bool& percent) {
	percent = false;
	if (!value || value->empty())
		return 0;
	if (value->find(L'%') != std::wstring::npos) {
		percent = true;
		return 0;
	}
	float v = (float)_wtof(value->c_str());
	if (value->find(L"pt") != std::wstring::npos)
		v *= 96.0f / 72.0f;
	else if (value->find(L"em") != std::wstring::npos)
		v *= 16;
	return v > 0 ? v : 0;
}

bool MdvSvgSize(std::string_view svg, float& width, float& height) {
	width = height = 0;
	// the root element's start is enough: as text
	const size_t start = svg.find("<svg");
	if (start == std::string_view::npos)
		return false;
	const size_t end = svg.find('>', start);
	const std::string_view tag = svg.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start + 1);
	std::wstring wide(tag.size(), L'\0');
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, tag.data(), (int)tag.size(), wide.data(), (int)wide.size());
	wide.resize(n > 0 ? n : 0);
	for (auto& t : MdvTokenizeHtml(wide)) {
		if (t.type != MdvHtmlToken::Start || t.name != L"svg")
			continue;
		bool wp, hp;
		width = CssLength(t.Attribute(L"width"), wp);
		height = CssLength(t.Attribute(L"height"), hp);
		float vw = 0, vh = 0;
		if (auto box = t.Attribute(L"viewbox")) {
			float x, y;
			std::wstring b = *box;
			for (auto& ch : b)
				if (ch == L',')
					ch = L' ';
			if (swscanf_s(b.c_str(), L"%f %f %f %f", &x, &y, &vw, &vh) != 4)
				vw = vh = 0;
		}
		if (width <= 0 && height <= 0) {
			width = vw;
			height = vh;
		}
		else if (width <= 0)
			width = vh > 0 ? height * vw / vh : height;
		else if (height <= 0)
			height = vw > 0 ? width * vh / vw : width;
		return width > 0 && height > 0;
	}
	return false;
}

namespace {

struct SvgStyle {
	std::wstring family;
	float size = 16;
	bool bold = false;
	int anchor = 0;
	bool hasColor = true;
	uint32_t color = 0;
	float opacity = 1;
	float sx = 1, sy = 1, tx = 0, ty = 0;   // to the root's units
	bool visible = true;
};

bool SvgColor(const std::wstring& v, uint32_t& color) {
	static const struct { const wchar_t* name; uint32_t color; } names[] = {
		{ L"black", RGB(0, 0, 0) }, { L"white", RGB(255, 255, 255) }, { L"red", RGB(255, 0, 0) }, { L"green", RGB(0, 128, 0) },
		{ L"blue", RGB(0, 0, 255) }, { L"gray", RGB(128, 128, 128) }, { L"grey", RGB(128, 128, 128) }, { L"yellow", RGB(255, 255, 0) },
		{ L"orange", RGB(255, 165, 0) }, { L"purple", RGB(128, 0, 128) }, { L"silver", RGB(192, 192, 192) },
	};
	if (v.size() == 4 && v[0] == L'#') {
		auto hex = [&](wchar_t c) { const int h = HexValue(c); return h < 0 ? 0 : h * 17; };
		color = RGB(hex(v[1]), hex(v[2]), hex(v[3]));
		return true;
	}
	if (v.size() == 7 && v[0] == L'#') {
		const unsigned n = wcstoul(v.c_str() + 1, nullptr, 16);
		color = RGB((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF);
		return true;
	}
	for (auto& n : names)
		if (_wcsicmp(v.c_str(), n.name) == 0) {
			color = n.color;
			return true;
		}
	return false;
}

float FirstNumber(const std::wstring* v) {
	return v ? (float)_wtof(v->c_str()) : 0;
}

void Apply(const MdvHtmlToken& t, SvgStyle& st) {
	if (auto v = t.Attribute(L"font-family")) {
		std::wstring f = *v;
		const size_t comma = f.find(L',');
		if (comma != std::wstring::npos)
			f.resize(comma);
		while (!f.empty() && (f.back() == L' ' || f.back() == L'\'' || f.back() == L'"'))
			f.pop_back();
		while (!f.empty() && (f[0] == L' ' || f[0] == L'\'' || f[0] == L'"'))
			f.erase(0, 1);
		st.family = f;
	}
	if (auto v = t.Attribute(L"font-size"))
		st.size = (float)_wtof(v->c_str());
	if (auto v = t.Attribute(L"font-weight"))
		st.bold = *v == L"bold" || _wtoi(v->c_str()) >= 600;
	if (auto v = t.Attribute(L"text-anchor"))
		st.anchor = *v == L"middle" ? 1 : *v == L"end" ? 2 : 0;
	if (auto v = t.Attribute(L"fill")) {
		if (*v == L"none")
			st.hasColor = false;
		else if (SvgColor(*v, st.color))
			st.hasColor = true;
	}
	if (auto v = t.Attribute(L"fill-opacity"))
		st.opacity *= (float)_wtof(v->c_str());
	if (auto v = t.Attribute(L"opacity"))
		st.opacity *= (float)_wtof(v->c_str());
	if (auto v = t.Attribute(L"display"); v && *v == L"none")
		st.visible = false;
	if (auto v = t.Attribute(L"transform")) {
		// scale(a[ b]) and translate(x[ y]), in order
		std::wstring tr = *v;
		for (auto& ch : tr)
			if (ch == L',')
				ch = L' ';
		for (size_t i = 0; i < tr.size();) {
			const size_t open = tr.find(L'(', i);
			const size_t close = tr.find(L')', open);
			if (open == std::wstring::npos || close == std::wstring::npos)
				break;
			std::wstring name = tr.substr(i, open - i);
			name.erase(0, name.find_first_not_of(L' '));
			float a = 0, b = 0;
			const int n = swscanf_s(tr.substr(open + 1, close - open - 1).c_str(), L"%f %f", &a, &b);
			if (name == L"scale" && n >= 1) {
				st.sx *= a;
				st.sy *= n >= 2 ? b : a;
			}
			else if (name == L"translate" && n >= 1) {
				st.tx += a * st.sx;
				st.ty += (n >= 2 ? b : 0) * st.sy;
			}
			i = close + 1;
		}
	}
}

}  // namespace

std::vector<MdvSvgText> MdvSvgTexts(std::string_view svg) {
	std::vector<MdvSvgText> out;
	if (svg.find("<text") == std::string_view::npos)
		return out;
	std::wstring wide(svg.size(), L'\0');
	const int n = ::MultiByteToWideChar(CP_UTF8, 0, svg.data(), (int)svg.size(), wide.data(), (int)wide.size());
	wide.resize(n > 0 ? n : 0);
	std::vector<SvgStyle> stack(1);
	int inText = 0;          // depth inside a <text>
	bool skip = false;       // <defs>, <title>, <desc>: not shown
	int skipDepth = 0;
	MdvSvgText current;
	for (auto& t : MdvTokenizeHtml(wide)) {
		if (t.type == MdvHtmlToken::Start) {
			if (skip) {
				if (!t.selfClosing)
					skipDepth++;
				continue;
			}
			if (t.name == L"defs" || t.name == L"title" || t.name == L"desc" || t.name == L"clippath" || t.name == L"mask" ||
				t.name == L"symbol" || t.name == L"lineargradient" || t.name == L"radialgradient" || t.name == L"style") {
				if (!t.selfClosing) {
					skip = true;
					skipDepth = 1;
				}
				continue;
			}
			if (t.selfClosing)
				continue;
			SvgStyle st = stack.back();
			Apply(t, st);
			if (t.name == L"svg" && stack.size() == 1) {
				// the root's viewBox, mapped to its size
				float w = 0, h = 0, vx = 0, vy = 0, vw = 0, vh = 0;
				if (auto v = t.Attribute(L"width"); v && v->find(L'%') == std::wstring::npos)
					w = (float)_wtof(v->c_str());
				if (auto v = t.Attribute(L"height"); v && v->find(L'%') == std::wstring::npos)
					h = (float)_wtof(v->c_str());
				if (auto v = t.Attribute(L"viewbox")) {
					std::wstring b = *v;
					for (auto& ch : b)
						if (ch == L',')
							ch = L' ';
					if (swscanf_s(b.c_str(), L"%f %f %f %f", &vx, &vy, &vw, &vh) != 4)
						vw = vh = 0;
				}
				if (vw > 0 && vh > 0) {
					if (w <= 0 && h <= 0) {
						w = vw;
						h = vh;
					}
					else if (w <= 0)
						w = h * vw / vh;
					else if (h <= 0)
						h = w * vh / vw;
					st.sx = w / vw;
					st.sy = h / vh;
					st.tx = -vx * st.sx;
					st.ty = -vy * st.sy;
				}
			}
			stack.push_back(st);
			if (t.name == L"text") {
				inText = 1;
				current = {};
				current.family = st.family;
				current.size = st.size * st.sy;
				current.bold = st.bold;
				current.anchor = st.anchor;
				current.color = st.color;
				current.opacity = st.opacity;
				current.x = FirstNumber(t.Attribute(L"x")) * st.sx + st.tx;
				current.y = FirstNumber(t.Attribute(L"y")) * st.sy + st.ty;
				if (!st.visible || !st.hasColor)
					current.opacity = 0;
			}
			else if (inText)
				inText++;
			continue;
		}
		if (t.type == MdvHtmlToken::End) {
			if (skip) {
				if (--skipDepth == 0)
					skip = false;
				continue;
			}
			if (stack.size() > 1)
				stack.pop_back();
			if (inText && --inText == 0) {
				while (!current.text.empty() && current.text.back() == L' ')
					current.text.pop_back();
				if (!current.text.empty() && current.opacity > 0)
					out.push_back(std::move(current));
				current = {};
			}
			continue;
		}
		if (t.type == MdvHtmlToken::Text && inText && !skip) {
			for (wchar_t ch : MdvDecodeEntities(std::wstring_view(wide).substr(t.pos, t.length))) {
				const bool space = ch == L' ' || ch == L'\n' || ch == L'\t' || ch == L'\r';
				if (space && (current.text.empty() || current.text.back() == L' '))
					continue;
				current.text += space ? L' ' : ch;
			}
		}
	}
	return out;
}
