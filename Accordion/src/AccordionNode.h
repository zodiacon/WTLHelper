#pragma once

#include <windows.h>

#include <string>
#include <vector>

// a section's side the model doesn't need: what the header shows, and the body window
struct AcdNode {
	struct Button {
		UINT         Id;
		int          Image;
		std::wstring Text;
		std::wstring Tip;   // the tooltip and the accessible name

		const std::wstring& Name() const { return Tip.empty() ? Text : Tip; }
	};

	std::wstring Text;
	std::wstring Info;
	int          Image = -1;
	HWND         Body = nullptr;
	LPARAM       Data = 0;
	std::vector<Button> Buttons;
	bool         Clipped = false;   // a window region shows part of the body (while it opens or closes)
};
