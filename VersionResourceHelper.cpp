#include "pch.h"
#include <strsafe.h>
#include "VersionResourceHelper.h"

#pragma comment(lib, "version")

VersionResourceHelper::VersionResourceHelper(PCWSTR path) : m_path(path) {
	if (path == nullptr) {
		WCHAR exepath[MAX_PATH];
		::GetModuleFileName(nullptr, exepath, _countof(exepath));
		m_path = exepath;
	}
	DWORD zero;
	auto infoSize = ::GetFileVersionInfoSize(m_path, &zero);
	if (infoSize) {
		m_buffer = std::make_unique<BYTE[]>(infoSize);
		if (!::GetFileVersionInfo(m_path, 0, infoSize, m_buffer.get()))
			m_buffer.reset();
	}
}

bool VersionResourceHelper::IsValid() const {
	return m_buffer != nullptr;
}

CString VersionResourceHelper::GetValue(const std::wstring& name) const {
	CString result;
	if (m_buffer) {
		WORD* langAndCodePage;
		UINT len;
		if (::VerQueryValue(m_buffer.get(), L"\\VarFileInfo\\Translation", (void**)&langAndCodePage, &len)) {
			WCHAR text[256];
			::StringCchPrintf(text, _countof(text), L"\\StringFileInfo\\%04x%04x\\%s", langAndCodePage[0], langAndCodePage[1], name.c_str());
			WCHAR* desc;
			if (::VerQueryValue(m_buffer.get(), text, (void**)&desc, &len))
				result = desc;
		}
	}
	return result;
}



