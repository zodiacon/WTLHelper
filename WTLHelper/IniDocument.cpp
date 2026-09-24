#include "pch.h"
#include "IniDocument.h"
#include <charconv>
#include <cwctype>

namespace {
	bool SameName(std::wstring_view a, std::wstring_view b) {
		if (a.size() != b.size())
			return false;
		if (a.empty())
			return true;
		return ::CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) == CSTR_EQUAL;
	}

	bool IsBlank(wchar_t ch) {
		return ch == L' ' || ch == L'\t';
	}

	std::wstring_view Trim(std::wstring_view text) {
		while (!text.empty() && IsBlank(text.front()))
			text.remove_prefix(1);
		while (!text.empty() && IsBlank(text.back()))
			text.remove_suffix(1);
		return text;
	}

	bool ToWide(std::string_view text, UINT codePage, DWORD flags, std::wstring& result) {
		result.clear();
		if (text.empty())
			return true;
		int count = ::MultiByteToWideChar(codePage, flags, text.data(), (int)text.size(), nullptr, 0);
		if (count <= 0)
			return false;
		result.resize(count);
		::MultiByteToWideChar(codePage, flags, text.data(), (int)text.size(), result.data(), count);
		return true;
	}

	std::string ToUtf8(std::wstring_view text) {
		if (text.empty())
			return {};
		int count = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		std::string result(count, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), count, nullptr, nullptr);
		return result;
	}

	std::wstring SystemMessage(DWORD error) {
		WCHAR text[512];
		auto length = ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, text, _countof(text), nullptr);
		std::wstring message(text, length);
		while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
			message.pop_back();
		return message.empty() ? L"error " + std::to_wstring(error) : message;
	}

	// numbers are plain ASCII; anything else can't be one
	bool ToAscii(std::wstring_view text, std::string& result) {
		result.clear();
		text = Trim(text);
		if (text.size() > 1 && text.front() == L'+' && iswdigit(text[1]))
			text.remove_prefix(1);
		for (auto ch : text) {
			if (ch > 127)
				return false;
			result.push_back((char)ch);
		}
		return !result.empty();
	}

	bool NeedsQuotes(std::wstring_view value) {
		return !value.empty() && (IsBlank(value.front()) || IsBlank(value.back()) || value.front() == L'"');
	}

	std::wstring SingleLine(std::wstring_view value) {
		std::wstring text(value);
		for (auto& ch : text)
			if (ch == L'\r' || ch == L'\n')
				ch = L' ';
		return text;
	}
}

bool IniDocument::Fail(std::wstring message) const {
	m_Error = std::move(message);
	return false;
}

//
// reading and writing files and text
//

bool IniDocument::Load(const wchar_t* path) {
	Clear();
	HANDLE file = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return Fail(SystemMessage(::GetLastError()));

	LARGE_INTEGER size = {};
	std::string text;
	bool ok = ::GetFileSizeEx(file, &size) && size.QuadPart <= 16 * 1024 * 1024;
	if (ok) {
		text.resize((size_t)size.QuadPart);
		DWORD read = 0;
		ok = size.QuadPart == 0 || (::ReadFile(file, text.data(), (DWORD)text.size(), &read, nullptr) && read == text.size());
	}
	auto error = ::GetLastError();
	::CloseHandle(file);
	if (!ok)
		return Fail(size.QuadPart > 16 * 1024 * 1024 ? L"the file is too large" : SystemMessage(error));

	return Parse(text);
}

bool IniDocument::Parse(std::string_view text) {
	Clear();

	std::wstring wide;
	if (text.size() >= 2 && (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE) {
		// UTF-16 LE, as Notepad's "Unicode" writes
		wide.resize((text.size() - 2) / 2);
		memcpy(wide.data(), text.data() + 2, wide.size() * sizeof(wchar_t));
	}
	else {
		if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
			text.remove_prefix(3);
		if (!ToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS, wide))
			ToWide(text, CP_ACP, 0, wide);		// not UTF-8: an old editor's ANSI text
	}

	Section* current = nullptr;
	int lineNumber = 0;
	size_t pos = 0;
	while (pos < wide.size()) {
		size_t end = wide.find_first_of(L"\r\n", pos);
		if (end == std::wstring::npos)
			end = wide.size();
		auto line = Trim(std::wstring_view(wide).substr(pos, end - pos));
		lineNumber++;
		// step over the line break (\r\n, \n or \r)
		pos = end;
		if (pos < wide.size()) {
			if (wide[pos] == L'\r' && pos + 1 < wide.size() && wide[pos + 1] == L'\n')
				pos++;
			pos++;
		}

		if (line.empty() || line.front() == L';' || line.front() == L'#')
			continue;

		auto where = L"line " + std::to_wstring(lineNumber) + L": ";
		if (line.front() == L'[') {
			auto close = line.find(L']');
			if (close == std::wstring_view::npos) {
				Clear();
				return Fail(where + L"the section name is missing its closing ]");
			}
			auto rest = Trim(line.substr(close + 1));
			if (!rest.empty() && rest.front() != L';' && rest.front() != L'#') {
				Clear();
				return Fail(where + L"unexpected text after ]");
			}
			current = &EnsureSection(Trim(line.substr(1, close - 1)));
			continue;
		}

		auto equals = line.find(L'=');
		if (equals == std::wstring_view::npos) {
			Clear();
			return Fail(where + L"expected Key=Value, [Section] or a comment");
		}
		auto key = Trim(line.substr(0, equals));
		if (key.empty()) {
			Clear();
			return Fail(where + L"the key name is missing before the =");
		}
		auto value = Trim(line.substr(equals + 1));
		if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
			value = value.substr(1, value.size() - 2);		// quotes keep the spaces inside

		if (current == nullptr)
			current = &EnsureSection(L"");
		bool found = false;
		for (auto& entry : current->Entries) {
			if (SameName(entry.Key, key)) {
				entry.Value = value;		// a repeated key keeps its last value
				entry.Line = lineNumber;
				found = true;
				break;
			}
		}
		if (!found)
			current->Entries.push_back({ std::wstring(key), std::wstring(value), lineNumber });
	}
	return true;
}

std::string IniDocument::ToString() const {
	std::wstring out;
	if (!m_Header.empty()) {
		size_t pos = 0;
		while (pos <= m_Header.size()) {
			size_t end = m_Header.find(L'\n', pos);
			if (end == std::wstring::npos)
				end = m_Header.size();
			auto line = std::wstring_view(m_Header).substr(pos, end - pos);
			while (!line.empty() && line.back() == L'\r')
				line.remove_suffix(1);
			out += line.empty() ? L";" : L"; " + std::wstring(line);
			out += L"\r\n";
			pos = end + 1;
		}
		out += L"\r\n";
	}

	bool first = true;
	for (auto const& section : m_Sections) {
		if (section.Entries.empty() && section.Name.empty())
			continue;
		if (!first)
			out += L"\r\n";
		first = false;
		if (!section.Name.empty())
			out += L"[" + section.Name + L"]\r\n";
		for (auto const& entry : section.Entries) {
			out += entry.Key + L"=";
			auto value = SingleLine(entry.Value);
			out += NeedsQuotes(value) ? L"\"" + value + L"\"" : value;
			out += L"\r\n";
		}
	}
	return ToUtf8(out);
}

bool IniDocument::Save(const wchar_t* path) const {
	auto text = ToString();
	// through a temporary file, so a failure part way can't leave the existing file damaged
	std::wstring temp = std::wstring(path) + L".tmp";
	HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return Fail(L"could not create " + temp + L": " + SystemMessage(::GetLastError()));

	DWORD written = 0;
	bool ok = ::WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size() && ::FlushFileBuffers(file);
	auto error = ::GetLastError();
	::CloseHandle(file);
	if (!ok) {
		::DeleteFileW(temp.c_str());
		return Fail(L"could not write " + temp + L": " + SystemMessage(error));
	}
	if (!::MoveFileExW(temp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		error = ::GetLastError();
		::DeleteFileW(temp.c_str());
		return Fail(L"could not replace " + std::wstring(path) + L": " + SystemMessage(error));
	}
	return true;
}

//
// reading values
//

IniDocument::Section* IniDocument::FindSection(std::wstring_view name) {
	for (auto& section : m_Sections)
		if (SameName(section.Name, name))
			return &section;
	return nullptr;
}

IniDocument::Section const* IniDocument::FindSection(std::wstring_view name) const {
	for (auto const& section : m_Sections)
		if (SameName(section.Name, name))
			return &section;
	return nullptr;
}

IniDocument::Section& IniDocument::EnsureSection(std::wstring_view name) {
	if (auto section = FindSection(name))
		return *section;
	m_Sections.push_back({ std::wstring(name), {} });
	return m_Sections.back();
}

IniDocument::Entry const* IniDocument::FindEntry(std::wstring_view section, std::wstring_view key) const {
	if (auto found = FindSection(section))
		for (auto const& entry : found->Entries)
			if (SameName(entry.Key, key))
				return &entry;
	return nullptr;
}

bool IniDocument::HasSection(std::wstring_view section) const {
	return FindSection(section) != nullptr;
}

bool IniDocument::Has(std::wstring_view section, std::wstring_view key) const {
	return FindEntry(section, key) != nullptr;
}

std::vector<std::wstring> IniDocument::Sections() const {
	std::vector<std::wstring> names;
	for (auto const& section : m_Sections)
		names.push_back(section.Name);
	return names;
}

std::vector<std::wstring> IniDocument::Keys(std::wstring_view section) const {
	std::vector<std::wstring> keys;
	if (auto found = FindSection(section))
		for (auto const& entry : found->Entries)
			keys.push_back(entry.Key);
	return keys;
}

int IniDocument::LineOf(std::wstring_view section, std::wstring_view key) const {
	auto entry = FindEntry(section, key);
	return entry ? entry->Line : 0;
}

std::optional<std::wstring> IniDocument::Get(std::wstring_view section, std::wstring_view key) const {
	if (auto entry = FindEntry(section, key))
		return entry->Value;
	return std::nullopt;
}

std::optional<int> IniDocument::GetInt(std::wstring_view section, std::wstring_view key) const {
	auto value = Get(section, key);
	std::string text;
	if (!value || !ToAscii(*value, text))
		return std::nullopt;
	int result;
	auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
	if (error != std::errc() || end != text.data() + text.size())
		return std::nullopt;
	return result;
}

std::optional<double> IniDocument::GetDouble(std::wstring_view section, std::wstring_view key) const {
	auto value = Get(section, key);
	std::string text;
	if (!value || !ToAscii(*value, text))
		return std::nullopt;
	double result;
	auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
	if (error != std::errc() || end != text.data() + text.size())
		return std::nullopt;
	return result;
}

std::optional<bool> IniDocument::GetBool(std::wstring_view section, std::wstring_view key) const {
	auto value = Get(section, key);
	if (!value)
		return std::nullopt;
	for (auto name : { L"true", L"yes", L"on", L"1" })
		if (SameName(*value, name))
			return true;
	for (auto name : { L"false", L"no", L"off", L"0" })
		if (SameName(*value, name))
			return false;
	return std::nullopt;
}

std::wstring IniDocument::GetString(std::wstring_view section, std::wstring_view key, std::wstring_view defaultValue) const {
	auto value = Get(section, key);
	return value ? *value : std::wstring(defaultValue);
}

int IniDocument::GetInt(std::wstring_view section, std::wstring_view key, int defaultValue) const {
	return GetInt(section, key).value_or(defaultValue);
}

double IniDocument::GetDouble(std::wstring_view section, std::wstring_view key, double defaultValue) const {
	return GetDouble(section, key).value_or(defaultValue);
}

bool IniDocument::GetBool(std::wstring_view section, std::wstring_view key, bool defaultValue) const {
	return GetBool(section, key).value_or(defaultValue);
}

//
// writing values
//

void IniDocument::SetString(std::wstring_view section, std::wstring_view key, std::wstring_view value) {
	auto& target = EnsureSection(section);
	for (auto& entry : target.Entries) {
		if (SameName(entry.Key, key)) {
			entry.Value = value;
			entry.Line = 0;
			return;
		}
	}
	target.Entries.push_back({ std::wstring(key), std::wstring(value), 0 });
}

void IniDocument::SetInt(std::wstring_view section, std::wstring_view key, int value) {
	SetString(section, key, std::to_wstring(value));
}

void IniDocument::SetDouble(std::wstring_view section, std::wstring_view key, double value, int decimals) {
	char buffer[128];
	auto result = decimals < 0 ? std::to_chars(buffer, buffer + sizeof(buffer), value)
		: std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, decimals);
	std::string text(buffer, result.ptr);
	if (decimals >= 0 && text.find('.') != std::string::npos) {
		while (text.back() == '0')
			text.pop_back();
		if (text.back() == '.')
			text.pop_back();
	}
	if (text == "-0")
		text = "0";
	SetString(section, key, std::wstring(text.begin(), text.end()));
}

void IniDocument::SetBool(std::wstring_view section, std::wstring_view key, bool value) {
	SetString(section, key, value ? L"true" : L"false");
}

void IniDocument::Remove(std::wstring_view section, std::wstring_view key) {
	if (auto found = FindSection(section)) {
		std::erase_if(found->Entries, [&](auto const& entry) { return SameName(entry.Key, key); });
	}
}

void IniDocument::RemoveSection(std::wstring_view section) {
	std::erase_if(m_Sections, [&](auto const& s) { return SameName(s.Name, section); });
}

void IniDocument::Clear() {
	m_Sections.clear();
	m_Header.clear();
	m_Error.clear();
}

void IniDocument::SetHeader(std::wstring_view text) {
	m_Header = text;
}
