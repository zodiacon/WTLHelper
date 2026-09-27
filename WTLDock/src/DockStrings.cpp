#include "DockStrings.h"
#include "Json.h"
#include "Utf8.h"
#include <vector>

namespace WTLDock {

namespace {

struct Entry {
	const wchar_t* Name;
	const wchar_t* Default;
};

const Entry Entries[] = {
#define WTLDOCK_TEXT_ENTRY(name, text) { L#name, text },
	WTLDOCK_TEXTS(WTLDOCK_TEXT_ENTRY)
#undef WTLDOCK_TEXT_ENTRY
};

static_assert(_countof(Entries) == (size_t)Str::Count);

std::vector<std::wstring>& Table() {
	static std::vector<std::wstring> table = [] {
		std::vector<std::wstring> t;
		for (auto& e : Entries)
			t.push_back(e.Default);
		return t;
	}();
	return table;
}

bool Valid(Str id) {
	return (int)id >= 0 && id < Str::Count;
}

}

const std::wstring& DockText(Str id) {
	static const std::wstring none;
	return Valid(id) ? Table()[(int)id] : none;
}

std::wstring DockText(Str id, std::initializer_list<std::wstring> args) {
	std::wstring text = DockText(id);
	int index = 0;
	for (auto& arg : args) {
		const std::wstring token = L"{" + std::to_wstring(index++) + L"}";
		for (size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + arg.size()))
			text.replace(at, token.size(), arg);
	}
	return text;
}

const std::wstring& DockSideText(DockSide side) {
	return DockText((Str)((int)Str::SideLeft + (int)side));
}

const wchar_t* DockTextName(Str id) {
	return Valid(id) ? Entries[(int)id].Name : L"";
}

const wchar_t* DockTextDefault(Str id) {
	return Valid(id) ? Entries[(int)id].Default : L"";
}

void SetDockText(Str id, std::wstring text) {
	if (Valid(id))
		Table()[(int)id] = text.empty() ? std::wstring(Entries[(int)id].Default) : std::move(text);
}

void ResetDockTexts() {
	for (int i = 0; i < (int)Str::Count; i++)
		Table()[i] = Entries[i].Default;
}

bool LoadDockTexts(std::string_view json, std::wstring* error) {
	Json::Value root;
	std::string parseError;
	if (!Json::Parse(json, root, &parseError) || !root.IsObject()) {
		if (error)
			*error = L"the texts are not a JSON object";
		return false;
	}
	for (size_t i = 0; i < root.Items.size(); i++) {
		if (!root.Items[i].IsString())
			continue;
		const std::wstring name = WideFromUtf8(root.Keys[i]);
		for (int k = 0; k < (int)Str::Count; k++)
			if (name == Entries[k].Name)
				SetDockText((Str)k, WideFromUtf8(root.Items[i].String));
	}
	return true;
}

std::string DumpDockTexts(bool defaults) {
	Json::Value root = Json::Value::MakeObject();
	for (int i = 0; i < (int)Str::Count; i++)
		root.Add(Utf8FromWide(Entries[i].Name), Json::Value::MakeString(Utf8FromWide(defaults ? Entries[i].Default : Table()[i])));
	return Json::Write(root) + "\n";
}

}
