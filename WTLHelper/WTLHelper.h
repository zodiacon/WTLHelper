#pragma once

namespace DarkMode {
	enum class DarkModeType : unsigned char;
}

namespace WTLHelper {
	bool InitDarkMode(DarkMode::DarkModeType type);
	DarkMode::DarkModeType DarkModeType() noexcept;
	bool IsDarkMode() noexcept;

}
