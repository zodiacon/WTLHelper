#pragma once

#include <memory>
#include <string>

class VersionResourceHelper {
public:
	explicit VersionResourceHelper(PCWSTR path = nullptr);
	bool IsValid() const;
	operator bool() const {
		return IsValid();
	}
	CString GetValue(const std::wstring& name) const;
	const CString& GetPath() const {
		return m_path;
	}

private:
	std::unique_ptr<BYTE[]> m_buffer;
	CString m_path;
};

