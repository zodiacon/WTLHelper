#include "pch.h"
#include "ListViewHelper.h"
#include "IListView.h"
#include <wil\resource.h>
#include "VirtualListView.h"
#include "WTLHelper.h"
#include <atldlgs.h>

namespace {
	// quotes a field when it has a separator, a quote or a line break (quotes inside are doubled)
	void AppendCsvField(CString& text, CString value) {
		if (value.FindOneOf(L",\"\r\n") < 0) {
			text += value;
			return;
		}
		value.Replace(L"\"", L"\"\"");
		text += L"\"" + value + L"\"";
	}
}

bool ListViewHelper::SaveAsCsv(CListViewCtrl const& lv, PCWSTR path) {
	CWaitCursor wait;	// cell texts may need lookups
	int columns = lv.GetHeader().GetItemCount();
	if (columns <= 0)
		return false;

	std::vector<int> order(columns);
	lv.GetColumnOrderArray(columns, order.data());

	CString text;
	// the header
	for (int i = 0; i < columns; i++) {
		WCHAR name[128]{};
		LVCOLUMN lvc{ LVCF_TEXT };
		lvc.pszText = name;
		lvc.cchTextMax = _countof(name);
		lv.GetColumn(order[i], &lvc);
		if (i)
			text += L",";
		AppendCsvField(text, name);
	}
	text += L"\r\n";

	// the rows, as shown
	int rows = lv.GetItemCount();
	CString value;
	for (int row = 0; row < rows; row++) {
		for (int i = 0; i < columns; i++) {
			value.Empty();
			lv.GetItemText(row, order[i], value);
			if (i)
				text += L",";
			AppendCsvField(text, value);
		}
		text += L"\r\n";
	}

	//
	// UTF-8 with a BOM (so Excel detects the encoding)
	//
	CW2A utf8(text, CP_UTF8);
	wil::unique_hfile hFile(::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
	if (!hFile)
		return false;
	static const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
	DWORD written;
	return ::WriteFile(hFile.get(), bom, sizeof(bom), &written, nullptr) &&
		::WriteFile(hFile.get(), (PCSTR)utf8, (DWORD)strlen(utf8), &written, nullptr);
}

CString ListViewHelper::PromptForCsvFile(HWND hOwner, PCWSTR defaultName) {
	CString name(defaultName);
	// characters that can't be in a file name
	for (auto ch : L"\\/:*?\"<>|") {
		if (ch)
			name.Replace(ch, L'_');
	}
	name.Trim();

	CSimpleFileDialog dlg(FALSE, L"csv", name,
		OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
		L"CSV Files (*.csv)\0*.csv\0All Files\0*.*\0", hOwner);
	WTLHelper::SuspendHook();
	auto ok = dlg.DoModal() == IDOK;
	WTLHelper::ResumeHook();
	return ok ? CString(dlg.m_szFileName) : CString();
}

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
	// remove the last line break, whatever its length
	if (!text.IsEmpty())
		text = text.Left(text.GetLength() - (int)wcslen(cr));
	return text;
}

bool ListViewHelper::WriteColumnsState(ColumnsState const& state, IStream* stm) {
	auto count = state.Count;
	if (stm == nullptr || count <= 0 || !state.Order || !state.Columns || !state.Tags)
		return false;

	bool ok = true;
	auto write = [&](const void* buffer, size_t size) {
		ULONG bytes = 0;
		if (ok && (FAILED(stm->Write(buffer, static_cast<ULONG>(size), &bytes)) || bytes != size))
			ok = false;
		};

	write(&count, sizeof(count));
	write(&state.SortColumn, sizeof(state.SortColumn));
	write(&state.SortAscending, sizeof(state.SortAscending));
	write(state.Order.get(), sizeof(int) * count);
	write(state.Columns.get(), sizeof(LVCOLUMN) * count);
	write(state.Tags.get(), sizeof(int) * count);
	if (state.Text) {
		for (int i = 0; i < count; i++) {
			auto len = (uint16_t)state.Text[i].length();
			write(&len, sizeof(len));
			if (len) {
				write(state.Text[i].c_str(), len * sizeof(WCHAR));
			}
		}
	}
	uint32_t end = 0xffff;
	write(&end, sizeof(end));
	return ok;
}

//
// Reads what WriteColumnsState wrote: the state is filled completely, or (false) not touched at all, whatever the stream
// holds - a short or damaged one is rejected, not guessed at. The column records are LVCOLUMNs as they were in memory, so
// a state is only good for the same build (bitness) that saved it; their text pointers mean nothing later and are cleared
// (the texts, if there are any, are in state.Text).
//
bool ListViewHelper::ReadColumnsState(ColumnsState& state, IStream* stm) {
	if (stm == nullptr)
		return false;

	auto read = [stm](void* buffer, size_t size) {
		ULONG bytes = 0;
		return SUCCEEDED(stm->Read(buffer, static_cast<ULONG>(size), &bytes)) && bytes == size;
		};

	constexpr int MaxColumns = 1024;
	ColumnsState result;
	int count = 0;
	if (!read(&count, sizeof(count)) || count <= 0 || count > MaxColumns)
		return false;
	result.Count = count;

	if (!read(&result.SortColumn, sizeof(result.SortColumn)) || !read(&result.SortAscending, sizeof(result.SortAscending)))
		return false;
	result.Order = std::make_unique<int[]>(count);
	if (!read(result.Order.get(), sizeof(int) * count))
		return false;
	result.Columns = std::make_unique<LVCOLUMN[]>(count);
	if (!read(result.Columns.get(), sizeof(LVCOLUMN) * count))
		return false;
	for (int i = 0; i < count; i++)
		result.Columns[i].pszText = nullptr;
	result.Tags = std::make_unique<int[]>(count);
	if (!read(result.Tags.get(), sizeof(int) * count))
		return false;

	// the texts, each with its length, or nothing but the end marker (a 32 bit 0xffff: its low half is what is seen first)
	uint16_t len = 0;
	if (!read(&len, sizeof(len)))
		return false;
	if (len != 0xffff) {
		result.Text = std::make_unique<std::wstring[]>(count);
		for (int i = 0; i < count; i++) {
			result.Text[i].resize(len);
			if (len && !read(result.Text[i].data(), len * sizeof(WCHAR)))
				return false;
			if (!read(&len, sizeof(len)))
				return false;
		}
		if (len != 0xffff)
			return false;
	}
	// the other half of the end marker
	uint16_t high = 0;
	if (!read(&high, sizeof(high)) || high != 0)
		return false;

	state = std::move(result);
	return true;
}
