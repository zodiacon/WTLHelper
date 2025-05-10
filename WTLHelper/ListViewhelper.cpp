#include "pch.h"
#include "ListViewHelper.h"
#include "IListView.h"
#include <wil\resource.h>
#include "VirtualListView.h"

bool ListViewHelper::SaveAll(PCWSTR path, CListViewCtrl& lv, PCWSTR separator, bool includeHeaders) {
	wil::unique_handle hFile(::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
	if (!hFile)
		return false;

	auto count = lv.GetItemCount();
	auto header = lv.GetHeader();
	auto columns = header.GetItemCount();
	CString text;
	DWORD written;

	if (includeHeaders) {
		HDITEM hdi;
		WCHAR text[64] = { 0 };
		hdi.cchTextMax = _countof(text);
		hdi.pszText = text;
		hdi.mask = HDI_TEXT;
		for (int i = 0; i < columns; i++) {
			header.GetItem(i, &hdi);
			::wcscat_s(text, i == columns - 1 ? L"\n" : separator);
			::WriteFile(hFile.get(), text, (DWORD)::wcslen(text) * sizeof(WCHAR), &written, nullptr);
		}
	}

	for (int i = 0; i < count; i++) {
		for (int c = 0; c < columns; c++) {
			text.Empty();
			lv.GetItemText(i, c, text);
			text += c == columns - 1 ? L"\n" : separator;
			::WriteFile(hFile.get(), text.GetBuffer(), text.GetLength() * sizeof(WCHAR), &written, nullptr);
		}
	}

	return true;
}

CString ListViewHelper::GetRowAsString(CListViewCtrl const& lv, int row, PCWSTR separator) {
	auto count = lv.GetHeader().GetItemCount();
	if (count == 0)
		return L"";

	CString text, item;
	for (int c = 0; c < count; c++) {
		if (lv.GetItemText(row, c, item))
			item.Trim(L"\n\r");
		text += item;
		if (c < count - 1)
			text += separator;
	}
	return text;
}

CString ListViewHelper::GetRowColumnsAsString(CListViewCtrl const& lv, int row, int start, int count, PCWSTR separator) {
	if(count == 0)
		count = lv.GetHeader().GetItemCount();
	if (count == 0)
		return L"";

	CString text, item;
	for (int c = 0; c < count; c++) {
		if (lv.GetItemText(row, c + start, item))
			item.Trim(L"\n\r");
		text += item;
		if (c < count - 1)
			text += separator;
	}
	return text;
}

CString ListViewHelper::GetSelectedRowsAsString(CListViewCtrl const& lv, PCWSTR separator, PCWSTR cr) {
	CString text;
	for (auto line : SelectedItemsView(lv)) {
		text += GetRowAsString(lv, line, separator) += cr;
	}
	if (!text.IsEmpty())
		text = text.Left(text.GetLength() - 2);
	return text;
}

int ListViewHelper::FindItem(CListViewCtrl const& lv, PCWSTR text, bool partial) {
	auto columns = lv.GetHeader().GetItemCount();
	CString stext(text);
	stext.MakeLower();
	for (int i = 0; i < lv.GetItemCount(); i++) {
		for (int c = 0; c < columns; c++) {
			CString text;
			lv.GetItemText(i, c, text);
			text.MakeLower();
			if (partial && text.Find(stext) >= 0)
				return i;
			if (!partial && text == stext)
				return i;
		}
	}

	return -1;
}

int ListViewHelper::SearchItem(CListViewCtrl const& lv, PCWSTR textToFind, bool searchDown, bool caseSenstive) {
	int start = lv.GetNextItem(-1, LVIS_SELECTED);
	CString find(textToFind);
	auto ignoreCase = !caseSenstive;
	if (ignoreCase)
		find.MakeLower();

	auto columns = lv.GetHeader().GetItemCount();
	auto count = lv.GetItemCount();
	int from = searchDown ? start + 1 : start - 1 + count;
	int to = searchDown ? count + start : start + 1;
	int step = searchDown ? 1 : -1;

	int findIndex = -1;
	CString text;
	for (int i = from; i != to; i += step) {
		int index = i % count;
		for (int c = 0; c < columns; c++) {
			lv.GetItemText(index, c, text);
			if (ignoreCase)
				text.MakeLower();
			if (text.Find(find) >= 0) {
				findIndex = index;
				break;
			}
		}
		if (findIndex >= 0)
			return findIndex;
	}

	return -1;
}

int ListViewHelper::FindRow(CListViewCtrl const& lv, PCWSTR rowText, int start) {
	auto count = lv.GetItemCount();
	for (int i = start + 1; i < count; i++)
		if (GetRowAsString(lv, i) == rowText)
			return i;

	return -1;
}

int ListViewHelper::FindRow(CListViewCtrl const& lv, int colStart, int colCount, PCWSTR rowText, int start) {
	if (colCount == 0)
		colCount = lv.GetHeader().GetItemCount();
	auto count = lv.GetItemCount();
	for (int i = start + 1; i < count; i++)
		if (GetRowColumnsAsString(lv, i, colStart, colCount) == rowText)
			return i;
	return -1;
}

IListView* ListViewHelper::GetIListView(HWND hListView) {
	IListView* p{ nullptr };
	::SendMessage(hListView, LVM_QUERYINTERFACE, reinterpret_cast<WPARAM>(&__uuidof(IListView)), reinterpret_cast<LPARAM>(&p));
	return p;
}

CString ListViewHelper::GetAllRowsAsString(CListViewCtrl const& lv, PCWSTR separator, PCWSTR cr) {
	CString text;
	int count = lv.GetItemCount();
	for (int i = 0; i < count; i++) {
		text += GetRowAsString(lv, i, separator) += cr;
	}
	if (!text.IsEmpty())
		text = text.Left(text.GetLength() - 2);
	return text;
}

bool ListViewHelper::WriteColumnsState(ColumnsState const& state, IStream* stm) {
	auto count = state.Count;
	stm->Write(&count, sizeof(count), nullptr);
	stm->Write(&state.SortColumn, sizeof(state.SortColumn), nullptr);
	stm->Write(&state.SortAscending, sizeof(state.SortAscending), nullptr);
	stm->Write(state.Order.get(), sizeof(int) * count, nullptr);
	stm->Write(state.Columns.get(), sizeof(LVCOLUMN) * count, nullptr);
	stm->Write(state.Tags.get(), sizeof(int) * count, nullptr);
	if (state.Text) {
		for (int i = 0; i < count; i++) {
			auto len = (uint16_t)state.Text[i].length();
			stm->Write(&len, sizeof(len), nullptr);
			if (len) {
				stm->Write(state.Text[i].c_str(), len * sizeof(WCHAR), nullptr);
			}
		}
	}
	uint32_t end = 0xffff;
	stm->Write(&end, sizeof(end), nullptr);
	return true;
}

bool ListViewHelper::ReadColumnsState(ColumnsState& state, IStream* stm) {
	int count = 0;
	stm->Read(&count, sizeof(count), nullptr);
	if (count == 0)
		return false;

	stm->Read(&state.SortColumn, sizeof(state.SortColumn), nullptr);
	stm->Read(&state.SortAscending, sizeof(state.SortAscending), nullptr);
	state.Order = std::make_unique<int[]>(count);
	stm->Read(state.Order.get(), sizeof(int) * count, nullptr);
	state.Columns = std::make_unique<LVCOLUMN[]>(count);
	stm->Read(state.Order.get(), sizeof(LVCOLUMN) * count, nullptr);
	state.Tags = std::make_unique<int[]>(count);
	stm->Read(state.Tags.get(), sizeof(int) * count, nullptr);

	uint16_t len = 0;
	stm->Read(&len, sizeof(len), nullptr);
	if (len != 0xffff) {
		state.Text = std::make_unique<std::wstring[]>(count);
		for (int i = 0; i < count; i++) {
			state.Text[i].resize(len);
			stm->Read(state.Text[i].data(), len * sizeof(WCHAR), nullptr);
			stm->Read(&len, sizeof(len), nullptr);
		}
		ATLASSERT(len == 0xffff);
	}
	return true;
}
