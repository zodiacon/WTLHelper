#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

// An INI file held in memory: sections of key=value pairs that can be read, changed and written back.
// (IniFile, next to this, is a thin wrapper over the GetPrivateProfile* APIs; this one exists for text that
// has to be right - Unicode, exact numbers, error messages - and for whole-file load and save.)
//
// Format
//   [Section]
//   Key=Value
//   ; a comment (also # at the start of a line - comments are whole lines only, so a ';' inside a value is fine)
//
// - UTF-8 on disk, written without a BOM; a UTF-8 or UTF-16 LE BOM is accepted on reading, and text that isn't
//   valid UTF-8 is read as the system ANSI code page.
// - Section and key names are case-insensitive. Blank lines are ignored. Keys before any section belong to
//   the unnamed section "". A key repeated in a section keeps its last value.
// - Whitespace around names and values is trimmed. A value in double quotes keeps its inside spaces exactly
//   ("  x  "); the writer quotes values that need it. Values are single-line (line breaks become spaces).
// - Numbers are locale independent ('.' is the decimal point).
//
// Saving rewrites the whole file (comments in a file that was loaded are not kept), through a temporary file
// so a failed save can't damage the existing one.
class IniDocument {
public:
	// ---- files and text. On failure they return false and Error() says why (with the line for syntax errors).
	bool Load(const wchar_t* path);
	bool Parse(std::string_view text);
	bool Save(const wchar_t* path) const;
	std::string ToString() const;
	std::wstring const& Error() const noexcept {
		return m_Error;
	}

	// ---- reading
	bool HasSection(std::wstring_view section) const;
	bool Has(std::wstring_view section, std::wstring_view key) const;
	std::vector<std::wstring> Sections() const;
	std::vector<std::wstring> Keys(std::wstring_view section) const;
	// the line the value was read from, 1-based; 0 if it wasn't read from text (or isn't there)
	int LineOf(std::wstring_view section, std::wstring_view key) const;

	// The value, or nothing if the key is missing (or, for the typed ones, isn't of that type).
	std::optional<std::wstring> Get(std::wstring_view section, std::wstring_view key) const;
	std::optional<int> GetInt(std::wstring_view section, std::wstring_view key) const;
	std::optional<double> GetDouble(std::wstring_view section, std::wstring_view key) const;
	std::optional<bool> GetBool(std::wstring_view section, std::wstring_view key) const;		// true/false, yes/no, on/off, 1/0
	// ... or a default
	std::wstring GetString(std::wstring_view section, std::wstring_view key, std::wstring_view defaultValue = {}) const;
	int GetInt(std::wstring_view section, std::wstring_view key, int defaultValue) const;
	double GetDouble(std::wstring_view section, std::wstring_view key, double defaultValue) const;
	bool GetBool(std::wstring_view section, std::wstring_view key, bool defaultValue) const;

	// ---- writing (sections and keys are created as needed, in the order first set)
	void SetString(std::wstring_view section, std::wstring_view key, std::wstring_view value);
	void SetInt(std::wstring_view section, std::wstring_view key, int value);
	// decimals < 0 writes the shortest text that reads back as exactly the same number; otherwise that many
	// decimals, with trailing zeros dropped
	void SetDouble(std::wstring_view section, std::wstring_view key, double value, int decimals = -1);
	void SetBool(std::wstring_view section, std::wstring_view key, bool value);
	void Remove(std::wstring_view section, std::wstring_view key);
	void RemoveSection(std::wstring_view section);
	void Clear();
	// comment lines written at the top of the file
	void SetHeader(std::wstring_view text);

private:
	struct Entry {
		std::wstring Key, Value;
		int Line{ 0 };
	};
	struct Section {
		std::wstring Name;
		std::vector<Entry> Entries;
	};

	Section* FindSection(std::wstring_view name);
	Section const* FindSection(std::wstring_view name) const;
	Section& EnsureSection(std::wstring_view name);
	Entry const* FindEntry(std::wstring_view section, std::wstring_view key) const;
	bool Fail(std::wstring message) const;

	std::vector<Section> m_Sections;
	std::wstring m_Header;
	mutable std::wstring m_Error;
};
