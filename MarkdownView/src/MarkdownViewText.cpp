// The markdown view's text helpers: raw HTML's tokens, entities, emoji shortcodes, URLs.

#include "MarkdownViewModel.h"

#include <windows.h>

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
