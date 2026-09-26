#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace WTLDock {

// Small file helpers for saved layouts (UTF-8 text).
// Reads a whole file; false if it is missing or unreadable.
bool ReadTextFile(const std::wstring& path, std::string& text);
// Writes a file so that a crash halfway leaves the old one: the text goes to a temporary file next to it that then
// replaces it. Creates no directories.
bool WriteTextFile(const std::wstring& path, std::string_view text);

//
// Named layouts ("Design", "Debug", ...): a set of saved DockLayout texts that can be kept in one file.
// The store does not interpret the layouts; DockLayout::Load does that.
//
class DockLayoutStore {
public:
	bool Empty() const {
		return m_Items.empty();
	}
	size_t Count() const {
		return m_Items.size();
	}
	// in the order in which they were first stored
	std::vector<std::wstring> Names() const;
	bool Contains(const std::wstring& name) const {
		return Find(name) != nullptr;
	}
	const std::string* Find(const std::wstring& name) const;

	// stores (or replaces) a layout; false for an empty name
	bool Set(const std::wstring& name, std::string layout);
	bool Remove(const std::wstring& name);
	// false if 'from' does not exist or 'to' is taken
	bool Rename(const std::wstring& from, const std::wstring& to);
	void Clear() {
		m_Items.clear();
	}

	std::string Serialize() const;
	// Replaces the contents; on failure they are left alone.
	bool Parse(std::string_view text, std::wstring* error = nullptr);
	bool SaveToFile(const std::wstring& path) const;
	bool LoadFromFile(const std::wstring& path, std::wstring* error = nullptr);

private:
	std::vector<std::pair<std::wstring, std::string>> m_Items;
};

}
