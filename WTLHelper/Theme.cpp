#include "pch.h"
#include "Theme.h"

Theme::Theme(bool def) : _default(def) {
	for (auto& c : SysColors)
		c = CLR_INVALID;
}

bool Theme::IsDefault() const {
	return _default;
}

HBRUSH Theme::GetSysBrush(int index) const {
	if (_SysBrush[index] == nullptr || ::GetObjectType(_SysBrush[index]) != OBJ_BRUSH) {
		auto color = GetSysColor(index);
		if (color != CLR_INVALID) {
			_SysBrush[index].Detach();
			_SysBrush[index].CreateSolidBrush(color);
		}
	}
	return _SysBrush[index].m_hBrush;
}

COLORREF Theme::GetSysColor(int index) const {
	return SysColors[index];
}
