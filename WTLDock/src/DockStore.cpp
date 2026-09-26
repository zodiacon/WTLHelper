#include "DockStore.h"
#include "Json.h"
#include "Utf8.h"
#include <algorithm>

namespace WTLDock {

bool ReadTextFile(const std::wstring& path, std::string& text) {
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size{};
	bool ok = ::GetFileSizeEx(file, &size) && size.QuadPart < (64 << 20);		// a layout is a few kilobytes
	std::string data;
	if (ok) {
		data.resize((size_t)size.QuadPart);
		DWORD read = 0;
		ok = size.QuadPart == 0 || (::ReadFile(file, data.data(), (DWORD)data.size(), &read, nullptr) && read == data.size());
	}
	::CloseHandle(file);
	if (ok)
		text = std::move(data);
	return ok;
}

bool WriteTextFile(const std::wstring& path, std::string_view text) {
	const std::wstring temporary = path + L".tmp";
	HANDLE file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	bool ok = (text.empty() || ::WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr)) && written == text.size();
	ok = ok && ::FlushFileBuffers(file);
	::CloseHandle(file);
	if (ok)
		ok = ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	if (!ok)
		::DeleteFileW(temporary.c_str());
	return ok;
}

std::vector<std::wstring> DockLayoutStore::Names() const {
	std::vector<std::wstring> names;
	for (auto& item : m_Items)
		names.push_back(item.first);
	return names;
}

const std::string* DockLayoutStore::Find(const std::wstring& name) const {
	for (auto& item : m_Items)
		if (item.first == name)
			return &item.second;
	return nullptr;
}

bool DockLayoutStore::Set(const std::wstring& name, std::string layout) {
	if (name.empty())
		return false;
	for (auto& item : m_Items) {
		if (item.first == name) {
			item.second = std::move(layout);
			return true;
		}
	}
	m_Items.emplace_back(name, std::move(layout));
	return true;
}

bool DockLayoutStore::Remove(const std::wstring& name) {
	auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& item) { return item.first == name; });
	if (it == m_Items.end())
		return false;
	m_Items.erase(it);
	return true;
}

bool DockLayoutStore::Rename(const std::wstring& from, const std::wstring& to) {
	if (to.empty() || Contains(to))
		return false;
	auto it = std::find_if(m_Items.begin(), m_Items.end(), [&](auto& item) { return item.first == from; });
	if (it == m_Items.end())
		return false;
	it->first = to;
	return true;
}

std::string DockLayoutStore::Serialize() const {
	Json::Value root = Json::Value::MakeObject();
	root.Add("format", Json::Value::MakeString("WTLDockLayouts"));
	root.Add("version", Json::Value::MakeNumber(1));
	Json::Value order = Json::Value::MakeArray();
	Json::Value layouts = Json::Value::MakeObject();
	for (auto& item : m_Items) {
		order.Push(Json::Value::MakeString(Utf8FromWide(item.first)));
		layouts.Add(Utf8FromWide(item.first), Json::Value::MakeString(item.second));
	}
	root.Add("order", std::move(order));
	root.Add("layouts", std::move(layouts));
	return Json::Write(root) + "\n";
}

bool DockLayoutStore::Parse(std::string_view text, std::wstring* error) {
	auto fail = [&](const wchar_t* message) {
		if (error)
			*error = message;
		return false;
	};

	Json::Value root;
	std::string parseError;
	if (!Json::Parse(text, root, &parseError))
		return fail(L"the layout file is not valid JSON");
	auto format = root.Find("format");
	auto version = root.Find("version");
	auto layouts = root.Find("layouts");
	if (!root.IsObject() || !format || !format->IsString() || format->String != "WTLDockLayouts")
		return fail(L"this is not a layout file");
	if (!version || !version->IsNumber() || version->Number != 1)
		return fail(L"unsupported layout file version");
	if (!layouts || !layouts->IsObject())
		return fail(L"the layout file has no layouts");

	std::vector<std::pair<std::wstring, std::string>> items;
	for (size_t i = 0; i < layouts->Items.size(); i++) {
		if (!layouts->Items[i].IsString() || layouts->Keys[i].empty())
			return fail(L"a layout in the file is not text");
		items.emplace_back(WideFromUtf8(layouts->Keys[i]), layouts->Items[i].String);
	}

	// the stored order, for the names it lists
	if (auto order = root.Find("order"); order && order->IsArray()) {
		std::vector<std::pair<std::wstring, std::string>> ordered;
		for (auto& name : order->Items) {
			if (!name.IsString())
				continue;
			auto wide = WideFromUtf8(name.String);
			auto it = std::find_if(items.begin(), items.end(), [&](auto& x) { return x.first == wide; });
			if (it != items.end() && std::none_of(ordered.begin(), ordered.end(), [&](auto& x) { return x.first == wide; }))
				ordered.push_back(*it);
		}
		for (auto& item : items)
			if (std::none_of(ordered.begin(), ordered.end(), [&](auto& x) { return x.first == item.first; }))
				ordered.push_back(item);
		items = std::move(ordered);
	}
	m_Items = std::move(items);
	return true;
}

bool DockLayoutStore::SaveToFile(const std::wstring& path) const {
	return WriteTextFile(path, Serialize());
}

bool DockLayoutStore::LoadFromFile(const std::wstring& path, std::wstring* error) {
	std::string text;
	if (!ReadTextFile(path, text)) {
		if (error)
			*error = L"the layout file cannot be read";
		return false;
	}
	return Parse(text, error);
}

}
