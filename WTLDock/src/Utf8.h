#pragma once

#include <windows.h>
#include <string>

namespace WTLDock {

inline std::string Utf8FromWide(const std::wstring& s) {
	if (s.empty())
		return {};
	int size = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), size, nullptr, nullptr);
	return result;
}

inline std::wstring WideFromUtf8(const std::string& s) {
	if (s.empty())
		return {};
	int size = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring result(size, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), size);
	return result;
}

}
