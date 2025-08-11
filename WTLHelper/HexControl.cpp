#include "pch.h"
#include "HexControl.h"

void CHexControl::DoPaint(CDCHandle dc, RECT& rect) {
	dc.FillSolidRect(&rect, m_Colors.Background);

	ATLASSERT(m_StartOffset >= 0);

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	GetScrollInfo(SB_HORZ, &si);
	int xstart = -si.nPos * m_CharWidth;

	si.nPos = GetSize() == 0 ? 0 : int(GetSize() * ((float)m_StartOffset / GetSize()) / m_BytesPerLine);
	SetScrollInfo(SB_VERT, &si);

	dc.SelectFont(m_Font);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(m_Colors.Background);

	WCHAR str[17];
	int i = 0;
	uint8_t* data;

	int addrLength = GetSize() < 1LL << 32 ? 8 : 16;
	const std::wstring addrFormat = L"%0" + std::to_wstring(addrLength) + L"X";
	int x = (addrLength + 1) * m_CharWidth;

	// data
	m_Text.clear();
	int lines = 0;
	CString number;
	int factor = m_CharWidth * (m_DataSize * 2 + 1);

	for (int y = 0;; y++) {
		auto offset = m_StartOffset + i;
		if (offset >= GetSize())
			break;

		auto count = (uint32_t)GetData(offset, m_BytesPerLine, data);
		if (count == 0)
			break;

		lines++;
		int jcount = min(m_BytesPerLine, count);
		auto step = m_DataSize;
		int x2 = 0;
		for (int j = 0; j < jcount; j += step) {
			//if (step > 1) {
			//	auto ds = uint32_t(m_Buffer->GetSize() - offset - j);
			//	ATLASSERT(ds >= 1);
			//	if (ds < m_DataSize)
			//		step = 1;
			//}
			DWORD64 value = 0;
			memcpy(&value, data + j, step);
			number = FormatNumber(value, step);
			bool selected = m_Selection.IsSelected(offset + j);
			dc.SetTextColor(selected ? m_Colors.SelectionText : (m_Modified.contains(offset + j) ? m_Colors.Modified : (value ? m_Colors.Text : RGB(128, 128, 128))));
			dc.SetBkColor(selected ? m_Colors.SelectionBackground : m_Colors.Background);

			dc.TextOut(x + xstart + j / m_DataSize * factor + x2, y * m_CharHeight, number + L" ", -1);
			if (step < m_DataSize)
				x2 += m_CharWidth * 3;

			if (offset + j >= GetSize())
				break;
		}
		if (y * m_CharHeight > rect.bottom)
			break;
		i += m_BytesPerLine;
	}

	// offsets
	dc.SetTextColor(m_Colors.Offset);
	dc.SetBkColor(m_Colors.Background);
	x = 0;
	m_Text.clear();
	if (GetSize() % m_BytesPerLine == 0)
		lines++;

	POLYTEXT poly[128]{};
	for (int y = 0; y < lines; y++) {
		std::wstring text;
		::StringCchPrintf(str, _countof(str), addrFormat.c_str(), m_BiasOffset + m_StartOffset + y * m_BytesPerLine);
		text = str;

		auto& p = poly[y];
		p.lpstr = text.c_str();
		p.x = x + xstart;
		p.y = y * m_CharHeight;
		p.n = (UINT)text.size();
		m_Text.push_back(std::move(text));
	}
	::PolyTextOut(dc, poly, (int)m_Text.size());


	//
	// ASCII
	//
	dc.SetTextColor(m_Colors.Ascii);
	x = m_CharWidth * (10 + m_BytesPerLine * (m_DataSize * 2 + 1) / m_DataSize);
	m_Text.clear();
	i = 0;
	WCHAR text[2]{};
	for (int y = 0;; y++) {
		auto count = GetData(m_StartOffset + i, m_BytesPerLine, data);
		if (count == 0)
			break;
		if (m_EditDigits > 0 && m_CaretOffset >= m_StartOffset + i && m_CaretOffset < m_StartOffset + i + m_BytesPerLine) {
			// changed data
			memcpy(data + m_CaretOffset - m_StartOffset - i, &m_CurrentInput, m_DataSize);
		}
		auto offset = m_StartOffset + i;
		for (uint32_t j = 0; j < count; j++) {
			if (offset + j >= GetSize())
				break;
			text[0] = data[j] < 32 || data[j] > 127 ? L'.' : (wchar_t)data[j];
			bool selected = m_Selection.IsSelected(offset + j);
			if (selected) {
				dc.SetTextColor(m_Colors.SelectionText);
				dc.SetBkColor(m_Colors.SelectionBackground);
			}
			else {
				dc.SetTextColor(m_Modified.contains(offset + j) ? m_Colors.Modified : m_Colors.Ascii);
				dc.SetBkColor(m_Colors.Background);
			}
			dc.TextOut(x + xstart + j * m_CharWidth, y * m_CharHeight, text, 1);
		}
		if (y * m_CharHeight > rect.bottom)
			break;
		i += m_BytesPerLine;
	}
	UpdateCaret();
}

bool CHexControl::HasSelection() const {
	return !m_Selection.IsEmpty();
}

LRESULT CHexControl::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	CreateSolidCaret(m_InsertMode ? 2 : m_CharWidth, m_CharHeight);
	UpdateCaret();
	ShowCaret();

	return 0;
}

LRESULT CHexControl::OnKillFocus(UINT, WPARAM, LPARAM, BOOL&) {
	HideCaret();
	DestroyCaret();

	return 0;
}

LRESULT CHexControl::OnLeftButtonDown(UINT, WPARAM, LPARAM lParam, BOOL&) {
	SetFocus();
	m_Selection.Clear();

	int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
	auto offset = GetOffsetFromPoint(CPoint(x, y));
	if (offset >= 0) {
		m_CaretOffset = offset;
		SetCapture();
		UpdateCaret();
	}
	RedrawWindow();
	return 0;
}

LRESULT CHexControl::OnGetDialogCode(UINT, WPARAM, LPARAM, BOOL&) {
	return DLGC_WANTALLKEYS;
}

LRESULT CHexControl::OnKeyDown(UINT, WPARAM wParam, LPARAM, BOOL&) {
	bool ctrl = ::GetKeyState(VK_CONTROL) & 0x80;
	if (ctrl) {
		switch (wParam) {
			case 'V': case 'v':
				if (CanPaste())
					Paste();
				return 0;
		}
		return 0;
	}

	if (GetSize() == 0)
		return 0;

	auto current = m_CaretOffset;
	bool abortEdit = false;
	bool shift = ::GetKeyState(VK_SHIFT) & 0x80;

	bool redraw = false;

	if (shift) {
		if (m_Selection.IsEmpty()) {
			m_Selection.SetAnchor(m_CaretOffset);
		}
	}
	else if (!shift && !m_Selection.IsEmpty()) {
		ClearSelection();
		redraw = true;
	}

	if (shift && m_EditDigits > 0) {
		CancelEdit();
		return 0;
	}
	switch (wParam) {
		case VK_ESCAPE:
			if (m_EditDigits > 0) {
				CancelEdit();
			}
			break;

		case VK_HOME:
			if (ctrl) {
				m_CaretOffset = 0;
				redraw = true;
			}
			else {
				m_CaretOffset -= m_CaretOffset % m_BytesPerLine;
			}
			break;

		case VK_END:
			if (ctrl) {
				m_CaretOffset = GetSize() - m_DataSize;
				redraw = true;
			}
			else {
				m_CaretOffset += m_BytesPerLine - m_CaretOffset % m_BytesPerLine - 1;
			}
			break;

		case VK_DOWN:
			if (m_CaretOffset + m_BytesPerLine < GetSize()) {
				m_CaretOffset += m_BytesPerLine;
				if (m_CaretOffset >= m_StartOffset + m_Lines * m_BytesPerLine) {
					m_StartOffset += m_BytesPerLine;
					redraw = true;
				}
			}
			break;

		case VK_UP:
			if (m_CaretOffset >= m_BytesPerLine) {
				m_CaretOffset -= m_BytesPerLine;
				if (m_CaretOffset < m_StartOffset) {
					m_StartOffset -= m_BytesPerLine;
					redraw = true;
				}
			}
			break;

		case VK_RIGHT:
			m_CaretOffset += m_DataSize;
			if (m_CaretOffset >= GetSize())
				m_CaretOffset = GetSize() + m_DataSize - 1;
			if (m_CaretOffset >= m_StartOffset + m_Lines * m_BytesPerLine) {
				m_StartOffset += m_BytesPerLine;
				redraw = true;
			}
			break;

		case VK_LEFT:
			m_CaretOffset -= m_DataSize;
			if (m_CaretOffset < 0)
				m_CaretOffset = 0;
			if (m_CaretOffset < m_StartOffset) {
				m_StartOffset -= m_BytesPerLine;
				redraw = true;
			}
			break;

		case VK_NEXT:
			m_StartOffset += m_BytesPerLine * m_Lines;
			if (m_StartOffset >= GetSize() - m_Lines * m_BytesPerLine) {
				m_StartOffset = GetSize() - m_Lines * m_BytesPerLine;
				m_StartOffset -= m_StartOffset % m_BytesPerLine;
				ATLASSERT(m_StartOffset % m_BytesPerLine == 0);
			}
			m_CaretOffset += m_BytesPerLine * m_Lines;
			redraw = true;
			break;

		case VK_PRIOR:
			if (m_CaretOffset >= m_BytesPerLine * m_Lines) {
				m_CaretOffset -= m_BytesPerLine * m_Lines;
				m_StartOffset -= m_BytesPerLine * m_Lines;
				if (m_StartOffset < 0)
					m_StartOffset = 0;
				redraw = true;
			}
			else {
				m_StartOffset = 0;
				m_CaretOffset = m_CaretOffset % m_BytesPerLine;
			}
			redraw = true;
			break;
	}
	if (m_EditDigits && m_CaretOffset != current) {
		//
		// done with edit mode
		//
		CommitValue(current, m_CurrentInput);
		m_EditDigits = 0;
	}

	if (shift && m_CaretOffset != current) {
		if (ctrl) {
			m_Selection.SetBox(min(m_CaretOffset, m_Selection.GetAnchor()), m_BytesPerLine,
				(m_CaretOffset - m_Selection.GetAnchor()) % m_BytesPerLine, (int)(m_CaretOffset - m_Selection.GetAnchor()) / m_BytesPerLine);
		}
		else {
			m_Selection.SetSimple(min(m_CaretOffset, m_Selection.GetAnchor()), abs(m_CaretOffset - m_Selection.GetAnchor()));
		}
		redraw = true;
		SendSelectionChanged();
	}

	NormalizeOffset(m_CaretOffset);
	if (abortEdit)
		CommitValue(current, m_CurrentInput);

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_POS;
	si.nPos = int(m_StartOffset / m_BytesPerLine);
	SetScrollInfo(SB_VERT, &si);

	//if (m_SelStart >= 0 && selLength != m_SelLength)
	//	DrawSelection(current);

	if (m_CaretOffset != current) {
		if (!abortEdit && m_EditDigits > 0)
			CommitValue(current, m_CurrentInput);
	}

	if (redraw) {
		RedrawWindow();
	}
	else {
		auto pt = GetPointFromOffset(m_CaretOffset);
		SetCaretPos(pt.x, pt.y);
	}
	return 0;
}

void CHexControl::ResetInput() {
	m_EditDigits = 0;
	m_CurrentInput = 0;
}

int64_t CHexControl::NormalizeOffset(int64_t& offset) const {
	if (offset > GetSize())
		offset = GetSize();
	else if (offset < 0)
		offset = 0;
	offset -= offset % m_DataSize;
	return offset;
}

void CHexControl::RedrawCaretLine() {
	auto pos = GetPointFromOffset(m_CaretOffset);
	RECT rcClient;
	GetClientRect(&rcClient);
	RECT rc = { pos.x - m_CharWidth, pos.y, rcClient.right, pos.y + m_CharHeight };
	RedrawWindow(&rc);
}

LRESULT CHexControl::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	InitFontMetrics();
	m_Notify.hwndFrom = m_hWnd;
	return 0;
}

LRESULT CHexControl::OnHScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(SB_HORZ, &si);
	auto pos = si.nPos;

	switch (LOWORD(wParam)) {
		case SB_LINELEFT:
			si.nPos--;
			break;

		case SB_LINERIGHT:
			si.nPos++;
			break;

		case SB_PAGELEFT:
			si.nPos -= si.nPage;
			break;

		case SB_PAGERIGHT:
			si.nPos += si.nPage;
			break;

		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			break;

	}
	si.fMask = SIF_POS;
	SetScrollInfo(SB_HORZ, &si);
	GetScrollInfo(SB_HORZ, &si);
	if (si.nPos != pos) {
		RedrawWindow();
	}
	return 0;
}

LRESULT CHexControl::OnVScroll(UINT, WPARAM wParam, LPARAM, BOOL&) {
	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(SB_VERT, &si);
	auto pos = si.nPos;

	switch (LOWORD(wParam)) {
		case SB_TOP:
			si.nPos = si.nMin;
			break;

		case SB_BOTTOM:
			si.nPos = si.nMax;
			break;

		case SB_LINEUP:
			si.nPos--;
			break;

		case SB_LINEDOWN:
			si.nPos++;
			break;

		case SB_PAGEUP:
			si.nPos -= si.nPage;
			if (si.nPos < 0)
				si.nPos = 0;
			break;

		case SB_PAGEDOWN:
			si.nPos += si.nPage;
			break;

		case SB_THUMBTRACK:
			si.nPos = si.nTrackPos;
			break;
	}
	if (si.nPos != pos) {
		si.fMask = SIF_POS;
		SetScrollInfo(SB_VERT, &si);
		GetScrollInfo(SB_VERT, &si);
		auto start = si.nPos * m_BytesPerLine;
		m_StartOffset = start;
		RedrawWindow();
	}
	return 0;
}

void CHexControl::SendSelectionChanged() {
	m_Notify.idFrom = GetWindowLongPtr(GWLP_ID);
	m_Notify.code = NMHX_SELECTION_CHANGED;
	GetParent().SendMessage(WM_NOTIFY, m_Notify.idFrom, reinterpret_cast<LPARAM>(&m_Notify));
}

void CHexControl::RecalcLayout() {
	if (GetSize() == 0) {
		return;
	}

	CRect rc;
	GetClientRect(&rc);

	auto lines = int(GetSize() / m_BytesPerLine) + 1;
	m_Lines = min(rc.Height() / m_CharHeight, int(GetSize() / m_BytesPerLine));

	if (GetSize() % m_BytesPerLine == 0) {
		lines++;
		m_Lines++;
	}

	SCROLLINFO si;
	si.cbSize = sizeof(si);
	si.fMask = SIF_PAGE | SIF_RANGE;
	si.nMin = 0;
	si.nMax = lines - 1;
	si.nPage = rc.bottom / m_CharHeight;
	SetScrollInfo(SB_VERT, &si);

	m_AddressDigits = GetSize() >= 1LL << 32 ? 16 : 8;
	m_Chars = m_AddressDigits + 1 + m_BytesPerLine / m_DataSize * (1 + 2 * m_DataSize) + 1 + m_BytesPerLine;

	si.nMax = static_cast<int>(m_Chars) - 1;
	si.nPage = rc.right / m_CharWidth;
	SetScrollInfo(SB_HORZ, &si);

	si.fMask = SIF_POS;
	GetScrollInfo(SB_VERT, &si);

	m_StartOffset = si.nPos * m_BytesPerLine;
	if (m_StartOffset + m_Lines * m_BytesPerLine >= GetSize()) {
		m_StartOffset = GetSize() - m_Lines * m_BytesPerLine;
		m_StartOffset += m_StartOffset % m_BytesPerLine;
		ATLASSERT(m_StartOffset % m_BytesPerLine == 0);
		if (m_StartOffset < 0)
			m_StartOffset = 0;
	}
}

void CHexControl::InitFontMetrics() {
	if (m_Font)
		m_Font.DeleteObject();
	m_Font.CreatePointFont(m_FontPointSize, L"Consolas");
	CClientDC dc(*this);
	dc.SelectFont(m_Font);
	TEXTMETRIC tm;
	dc.GetTextMetrics(&tm);
	m_CharHeight = tm.tmHeight;
	m_CharWidth = tm.tmAveCharWidth;
}

CString CHexControl::FormatNumber(ULONGLONG number, int size) const {
	if (size == 0)
		size = m_DataSize;
	static PCWSTR formats[] = {
		L"%02X",
		L"%04X",
		L"%08X",
		nullptr,
		L"%016llX"
	};

	CString result;
	result.Format(formats[size >> 1], number);
	return result.Right(size * 2);
}

LRESULT CHexControl::OnContextMenu(UINT, WPARAM, LPARAM, BOOL&) {
	NMHDR hdr;
	hdr.hwndFrom = m_hWnd;
	hdr.code = NM_RCLICK;
	hdr.idFrom = GetWindowLongPtr(GWLP_ID);
	GetParent().SendMessage(WM_NOTIFY, hdr.idFrom, reinterpret_cast<LPARAM>(&hdr));
	return 0;
}

LRESULT CHexControl::OnSize(UINT, WPARAM wp, LPARAM, BOOL&) {
	RecalcLayout();
	if (wp != SIZE_MINIMIZED)
		RedrawWindow();

	return 0;
}

int64_t CHexControl::GetSize() const {
	return m_pBuffer ? m_Size : m_Data.size();
}

int64_t CHexControl::GetData(int64_t offset, int64_t size, uint8_t*& p) {
	p = (m_pBuffer ? m_pBuffer : m_Data.data()) + offset;

	return min(size, m_Size - offset);
}

int64_t CHexControl::GetData(int64_t offset, int64_t size, uint8_t const*& p) const {
	p = (m_pBuffer ? m_pBuffer : m_Data.data()) + offset;

	return min(size, (int64_t)m_Size - offset);
}


void CHexControl::SetReadOnly(bool readonly) {
	m_ReadOnly = readonly;
	Refresh();
}

bool CHexControl::IsReadOnly() const {
	return m_ReadOnly;
}

bool CHexControl::SetInsertMode(bool insert) {
	if (insert && m_pBuffer)
		return false;

	m_InsertMode = insert;
	Refresh();
	return true;
}

bool CHexControl::IsInsertMode() const {
	return m_InsertMode;
}

bool CHexControl::IsDataOwner() const {
	return m_pBuffer != nullptr;
}

void CHexControl::SetSize(int64_t size) {
	if (m_pBuffer)
		m_Size = size;
	else
		m_Data.resize(size);
}

bool CHexControl::SetDataSize(int32_t size) {
	if (size == 0 || (m_DataSize & (m_DataSize - 1)) != 0 || size > 8)
		return false;

	m_DataSize = size;
	RecalcLayout();
	RedrawWindow();
	return true;
}

int32_t CHexControl::GetDataSize() const {
	return m_DataSize;
}

bool CHexControl::SetBytesPerLine(int32_t bytesPerLine) {
	if (bytesPerLine % 8 != 0 || bytesPerLine == 0)
		return false;

	m_BytesPerLine = bytesPerLine;
	RecalcLayout();
	RedrawWindow();
	return true;
}

int32_t CHexControl::GetBytesPerLine() const {
	return m_BytesPerLine;
}

bool CHexControl::Copy(int64_t offset, int64_t size) const {
	if (offset < 0)
		offset = m_Selection.GetOffset();
	if (size == 0)
		size = m_Selection.GetLength();
	if (size == 0 || offset < 0)
		return false;

	auto text = std::format(L"Offset: {:X} Data: ", offset + m_BiasOffset);
	CString fmt;
	fmt.Format(L"%%0%dX ", m_DataSize * 2);
	uint64_t const* value = 0;
	CString item;
	for (int64_t i = 0; i < size; i += m_DataSize) {
		GetData(offset + i, m_DataSize, (uint8_t const*&)value);
		item.Format(fmt, *value);
		text += (PCWSTR)item;
	}
	CopyText(text.c_str());
	return true;
}

bool CHexControl::Paste(int64_t offset) {
	if (::IsClipboardFormatAvailable(CF_TEXT)) {
		if (OpenClipboard()) {
			auto hGlobal = ::GetClipboardData(CF_TEXT);
			if (hGlobal) {
				auto text = (char*)::GlobalLock(hGlobal);
				std::vector<uint8_t> data;
				while (*text) {
					char* next;
					int value = strtol(text, &next, 16);
					if (text == next) {
						text++;
						continue;
					}
					text = next;
					data.push_back(value);
				}
				offset = offset < 0 ? 0 : offset;
				if (m_InsertMode)
					InsertData(offset, data);
				else
					SetData(offset, data);
			}
			::CloseClipboard();
		}
	}
	return false;
}

bool CHexControl::CanCopy() const {
	return HasSelection();
}

bool CHexControl::CanPaste() const {
	return !IsReadOnly() && ::IsClipboardFormatAvailable(CF_TEXT);
}

bool CHexControl::Cut() {
	return false;
}

bool CHexControl::Delete() {
	return false;
}

void CHexControl::ClearAll() {
	m_Data.clear();
	m_pBuffer = nullptr;
	m_Size = 0;
	Refresh();
}

void CHexControl::SetDirty(bool dirty) {
	m_Dirty = dirty;
	if (!dirty)
		m_Modified.clear();
	Refresh();
}

bool CHexControl::IsDirty() const {
	return m_Dirty;
}

bool CHexControl::CanCut() const {
	return CanDelete();
}

bool CHexControl::CanDelete() const {
	return !IsReadOnly() && !m_Selection.IsEmpty();
}

Selection const& CHexControl::GetSelection() const {
	return m_Selection;
}

int64_t CHexControl::SetBiasOffset(int64_t offset) {
	auto current = m_BiasOffset;
	m_BiasOffset = offset;
	RedrawWindow();
	return current;
}

int64_t CHexControl::GetBiasOffset() const {
	return m_BiasOffset;
}

HexControlColors& CHexControl::GetColors() {
	return m_Colors;
}

std::wstring CHexControl::GetText(int64_t offset, int64_t size) const {
	return std::wstring();
}

void CHexControl::Refresh() {
	RecalcLayout();
	RedrawWindow();
}

bool CHexControl::SetData(int64_t offset, std::span<const uint8_t> data, bool update) {
	auto size = data.size();
	if (offset + size > m_Data.size()) {
		if (m_AllowGrow)
			m_Data.resize(offset + size);
		else
			size = m_Data.size() - offset;
	}
	memcpy(m_Data.data() + offset, data.data(), size);
	if (update)
		Refresh();
	return true;
}

bool CHexControl::InitData(uint8_t* p, int64_t size, bool owner) {
	if (owner) {
		m_pBuffer = nullptr;
		m_Data.clear();
		SetData(0, std::span{ p, (size_t)size });
	}
	else {
		m_pBuffer = p;
		m_Size = size;
		m_Data.clear();
	}
	Refresh();
	return true;
}

bool CHexControl::InsertData(int64_t offset, std::span<uint8_t> data, bool update) {
	if (m_pBuffer)	// not data owner
		return false;

	m_Data.resize(m_Data.size() + data.size());
	memmove(m_Data.data() + offset + data.size(), m_Data.data() + offset, m_Data.size() - offset + data.size());
	memcpy(m_Data.data() + offset, data.data(), data.size());
	if (update)
		Refresh();
	return true;
}

void CHexControl::CancelEdit() {
	SetData(m_CaretOffset, std::span { (uint8_t*)&m_OldValue, m_DataSize }, false);
	if (m_InsertMode) {
		memmove(m_Data.data() + m_CaretOffset, m_Data.data() + m_CaretOffset + m_DataSize, m_Data.size() - m_CaretOffset);
		m_Data.resize(m_Data.size() - m_DataSize);
		Refresh();
	}
	else {
		CClientDC dc(m_hWnd);
		DrawNumber(dc.m_hDC, m_CaretOffset, m_OldValue, m_DataSize * 2);
	}
	m_EditDigits = 0;
}

bool CHexControl::CopyText(PCWSTR text) const {
	if (::OpenClipboard(m_hWnd)) {
		::EmptyClipboard();
		auto size = (::wcslen(text) + 1) * sizeof(WCHAR);
		auto hData = ::GlobalAlloc(GMEM_MOVEABLE, size);
		if (hData) {
			auto p = ::GlobalLock(hData);
			if (p) {
				::memcpy(p, text, size);
				::GlobalUnlock(p);
				::SetClipboardData(CF_UNICODETEXT, hData);
			}
		}
		::CloseClipboard();
		if (hData)
			return true;
	}
	return false;
}
