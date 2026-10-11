#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <utility>
#include <atlstr.h>

struct SortHelper final {
	static bool Sort(const ATL::CString& s1, const ATL::CString& s2, bool ascending) noexcept;
	static bool Sort(const std::string& s1, const std::string& s2, bool ascending) noexcept;
	static bool Sort(const std::wstring& s1, const std::wstring& s2, bool ascending) noexcept;
	static bool Sort(PCWSTR s1, PCWSTR s2, bool ascending) noexcept;
	static bool Sort(PWSTR s1, PWSTR s2, bool ascending) noexcept;
	static bool Sort(bool a, bool b, bool asc) noexcept;

	template<typename Number>
	static bool Sort(const Number& n1, const Number& n2, bool ascending) noexcept {
		return ascending ? n2 > n1 : n2 < n1;
	}

	//
	// Sorts items by a key read once per item. For values queried from the system rather than taken from a snapshot:
	// read in the comparison, such a value can change in the middle of the sort, and std::sort's behavior is undefined
	// if the comparison isn't consistent (it can even read out of range); it's also one query per comparison.
	//
	template<typename Item, typename GetKey, typename Less>
	static void SortByKey(std::vector<Item>& items, GetKey getKey, Less less) {
		using Key = std::decay_t<decltype(getKey(items.front()))>;
		std::vector<std::pair<Key, Item>> keyed;
		keyed.reserve(items.size());
		for (auto& item : items) {
			Key key = getKey(item);
			keyed.emplace_back(std::move(key), std::move(item));
		}
		std::sort(keyed.begin(), keyed.end(), [&](const auto& a, const auto& b) { return less(a.first, b.first); });
		for (size_t i = 0; i < items.size(); i++)
			items[i] = std::move(keyed[i].second);
	}
};
