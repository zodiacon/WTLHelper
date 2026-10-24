#include "ColorPicker.h"
#include "ColorPickerGradient.h"
#include "ColorPickerModel.h"

#include <commctrl.h>

#include <algorithm>

#pragma comment(lib, "comctl32.lib")

// The panel on the right: the swatch with the new and original colors, and the fields. The fields are edit
// controls without borders; their frames, labels and spin buttons are drawn by the control, so they follow
// its colors.

namespace {
	const wchar_t* const FieldNames[CpFieldCount] = { L"H", L"S", L"V", L"A", L"R", L"G", L"B", L"Hex" };
	const UINT SpinTimer = 1;
	const COLORREF BadColor = RGB(0xE0, 0x40, 0x40);

	bool IsEmpty(const RECT& rc) {
		return rc.right <= rc.left || rc.bottom <= rc.top;
	}

	COLORREF Blend(COLORREF a, COLORREF b, int percentB) {
		auto mix = [=](int x, int y) { return (BYTE)((x * (100 - percentB) + y * percentB) / 100); };
		return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
	}

	void FillSolid(HDC hdc, const RECT& rc, COLORREF color) {
		::SetBkColor(hdc, color);
		::ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
	}

	void Frame(HDC hdc, const RECT& rc, COLORREF color) {
		FillSolid(hdc, { rc.left, rc.top, rc.right, rc.top + 1 }, color);
		FillSolid(hdc, { rc.left, rc.bottom - 1, rc.right, rc.bottom }, color);
		FillSolid(hdc, { rc.left, rc.top, rc.left + 1, rc.bottom }, color);
		FillSolid(hdc, { rc.right - 1, rc.top, rc.right, rc.bottom }, color);
	}

	// pixels drawn by the model, put on the DC at rc
	void DrawPixels(HDC hdc, const RECT& rc, auto&& fill) {
		int w = rc.right - rc.left, h = rc.bottom - rc.top;
		if (w <= 0 || h <= 0)
			return;
		std::vector<uint32_t> bits((size_t)w * h);
		fill(CpPixels{ bits.data(), w, h });
		BITMAPINFO bmi{};
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = w;
		bmi.bmiHeader.biHeight = -h;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;
		::SetDIBitsToDevice(hdc, rc.left, rc.top, w, h, 0, 0, 0, h, bits.data(), &bmi, DIB_RGB_COLORS);
	}

	// a small arrow pointing up or down, centered in rc
	void Arrow(HDC hdc, const RECT& rc, bool up, int width, COLORREF color) {
		int height = std::max(2, width / 2 + 1);   // rows from the tip: one pixel, then two more each row
		int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
		int tip = up ? cy - height / 2 : cy + (height - 1) / 2;
		for (int i = 0; i < height; i++) {
			int y = up ? tip + i : tip - i;
			FillSolid(hdc, { cx - i, y, cx + i + 1, y + 1 }, color);
		}
	}
}

// ---- Creating and placing ------------------------------------------------------------------

void CColorPickerCtrl::CreateFields() {
	for (int f = 0; f < CpFieldCount; f++) {
		// created in the tab order: H S V A, then R G B Hex
		HWND edit = ::CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, m_hWnd,
			(HMENU)(INT_PTR)(1000 + f), nullptr, nullptr);
		if (!edit)
			continue;
		m_Edits[f] = edit;
		::SendMessage(edit, WM_SETFONT, (WPARAM)m_hFont, FALSE);
		::SendMessage(edit, EM_SETLIMITTEXT, f == CpHex ? 12 : 5, 0);
		::SetWindowSubclass(edit, EditSubclassProc, f, (DWORD_PTR)this);
	}
}

void CColorPickerCtrl::PositionFields() {
	for (auto& rc : m_Labels)
		rc = {};
	for (auto& rc : m_Frames)
		rc = {};
	m_Swatch = {};
	RECT client;
	GetClientRect(&client);

	// measured in the font
	WTL::CClientDC dc(m_hWnd);
	auto old = dc.SelectFont(m_hFont);
	TEXTMETRICW tm{};
	dc.GetTextMetrics(&tm);
	int labelWidth = 0;
	for (auto name : FieldNames) {
		SIZE size{};
		dc.GetTextExtent(name, -1, &size);
		labelWidth = std::max(labelWidth, (int)size.cx);
	}
	SIZE hex{}, number{};
	dc.GetTextExtent(L"#DDDDDD", -1, &hex);
	dc.GetTextExtent(L"0000", -1, &number);
	dc.SelectFont(old);

	m_TextHeight = tm.tmHeight;
	m_SpinWidth = Scale(14);
	int padX = Scale(3);
	CpPanelMetrics pm;
	pm.labelWidth = labelWidth + Scale(8);
	pm.fieldWidth = std::max({ Scale(56), (int)hex.cx + 2 * padX + 4, (int)number.cx + 2 * padX + 4 + m_SpinWidth });
	pm.fieldHeight = m_TextHeight + Scale(8);
	pm.rowGap = Scale(6);
	pm.colGap = Scale(10);
	pm.swatchHeight = Scale(56);
	pm.swatchGap = Scale(12);
	pm.spinWidth = m_SpinWidth;

	bool fields = HasFields(), swatch = HasSwatch();
	if (fields || swatch) {
		int left = std::max(m_Pad, (int)client.right - m_Pad - CpPanelWidth(pm));
		auto p = CpComputePanel(left, m_Pad, pm, swatch, fields, HasAlpha());
		m_Swatch = p.swatch;
		std::copy(std::begin(p.label), std::end(p.label), m_Labels);
		std::copy(std::begin(p.frame), std::end(p.frame), m_Frames);
	}

	HWND focus = ::GetFocus();
	for (int f = 0; f < CpFieldCount; f++) {
		HWND edit = m_Edits[f];
		if (!edit)
			continue;
		if (IsEmpty(m_Frames[f])) {
			if (focus == edit && m_Grad->IsWindow())
				m_Grad->SetFocus();
			::ShowWindow(edit, SW_HIDE);
			continue;
		}
		auto rc = CpEditRect(m_Frames[f], (CpField)f, m_SpinWidth, m_TextHeight, padX);
		::SetWindowPos(edit, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}
}

// ---- The fields' text ------------------------------------------------------------------------

int CColorPickerCtrl::FieldOf(HWND hEdit) const {
	for (int f = 0; f < CpFieldCount; f++) {
		if (hEdit && m_Edits[f] == hEdit)
			return f;
	}
	return -1;
}

ATL::CString CColorPickerCtrl::FieldText(int field) const {
	return CpFieldText((CpField)field, { m_Hsv.h, m_Hsv.s, m_Hsv.v }, m_Alpha).c_str();
}

static ATL::CString WindowText(HWND hWnd) {
	ATL::CString text;
	int len = ::GetWindowTextLengthW(hWnd);
	::GetWindowTextW(hWnd, text.GetBuffer(len + 1), len + 1);
	text.ReleaseBuffer();
	return text;
}

void CColorPickerCtrl::UpdateFields(int skipField) {
	for (int f = 0; f < CpFieldCount; f++) {
		if (f == skipField || !m_Edits[f])
			continue;
		auto text = FieldText(f);
		if (WindowText(m_Edits[f]) != text) {
			m_SettingText = true;
			::SetWindowTextW(m_Edits[f], text);
			m_SettingText = false;
		}
		if (m_Bad[f]) {
			m_Bad[f] = false;
			InvalidateField(f);
		}
	}
}

bool CColorPickerCtrl::IsFieldTextValid(int field) const {
	CpHsv hsv{ m_Hsv.h, m_Hsv.s, m_Hsv.v };
	BYTE alpha = m_Alpha;
	return CpApplyField((CpField)field, (LPCWSTR)WindowText(m_Edits[field]), hsv, alpha);
}

bool CColorPickerCtrl::IsFieldEdited(int field) const {
	return m_Typing == field || WindowText(m_Edits[field]) != FieldText(field);
}

void CColorPickerCtrl::InvalidateField(int field) {
	RECT rc = m_Frames[field];
	if (!IsEmpty(rc))
		InvalidateRect(&rc, FALSE);
}

// ---- Typing and stepping -------------------------------------------------------------------

void CColorPickerCtrl::FieldChanged(int field) {
	CpHsv hsv{ m_Hsv.h, m_Hsv.s, m_Hsv.v };
	BYTE alpha = m_Alpha;
	bool ok = CpApplyField((CpField)field, (LPCWSTR)WindowText(m_Edits[field]), hsv, alpha);
	if (ok == m_Bad[field]) {
		m_Bad[field] = !ok;
		InvalidateField(field);
	}
	Hsv n{ hsv.h, hsv.s, hsv.v };
	if (!ok || SameState(n, alpha))
		return;
	if (m_Typing != field) {
		if (m_Typing >= 0)
			CommitField(m_Typing);
		m_Typing = field;
		m_TypingStartHsv = m_Hsv;
		m_TypingStartAlpha = m_Alpha;
	}
	SetState(n, alpha, field);   // the field being typed in keeps its text
	Notify(CPKN_COLORCHANGING, CPKHT_FIELD);
}

void CColorPickerCtrl::CommitField(int field) {
	if (field < 0 || field >= CpFieldCount || !m_Edits[field])
		return;
	// a bad or unfinished text shows the color's value again ("007" is 7, "300" is 255)
	auto text = FieldText(field);
	if (WindowText(m_Edits[field]) != text) {
		m_SettingText = true;
		::SetWindowTextW(m_Edits[field], text);
		m_SettingText = false;
	}
	if (m_Bad[field]) {
		m_Bad[field] = false;
		InvalidateField(field);
	}
	if (m_Typing == field) {
		m_Typing = -1;
		Notify(CPKN_COLORCHANGED, CPKHT_FIELD);
	}
}

void CColorPickerCtrl::RevertField(int field) {
	if (m_Typing == field) {
		m_Typing = -1;
		if (!SameState(m_TypingStartHsv, m_TypingStartAlpha)) {
			SetState(m_TypingStartHsv, m_TypingStartAlpha);
			Notify(CPKN_COLORCHANGING, CPKHT_FIELD);
		}
	}
	CommitField(field);   // the text, without a notification now
	::SendMessage(m_Edits[field], EM_SETSEL, 0, -1);
}

bool CColorPickerCtrl::StepField(int field, int delta) {
	CpHsv hsv{ m_Hsv.h, m_Hsv.s, m_Hsv.v };
	BYTE alpha = m_Alpha;
	if (!CpStepField((CpField)field, delta, hsv, alpha))
		return false;
	bool typed = m_Typing == field;
	m_Typing = -1;
	Hsv n{ hsv.h, hsv.s, hsv.v };
	bool changed = !SameState(n, alpha);
	if (changed)
		SetState(n, alpha);
	CommitField(field);   // its text too, when the color didn't change ("007" at a limit)
	if (::GetFocus() == m_Edits[field])
		::SendMessage(m_Edits[field], EM_SETSEL, 0, -1);
	if (changed)
		Notify(CPKN_COLORCHANGING, CPKHT_FIELD);
	return changed || typed;
}

LRESULT CColorPickerCtrl::OnCommand(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	int field = FieldOf((HWND)lParam);
	if (field < 0) {
		bHandled = FALSE;
		return 0;
	}
	switch (HIWORD(wParam)) {
		case EN_CHANGE:
			if (!m_SettingText)
				FieldChanged(field);
			break;
		case EN_SETFOCUS:
			m_WheelRemainder = 0;
			::SendMessage(m_Edits[field], EM_SETSEL, 0, -1);
			InvalidateField(field);
			break;
		case EN_KILLFOCUS:
			CommitField(field);
			InvalidateField(field);
			break;
	}
	return 0;
}

LRESULT CALLBACK CColorPickerCtrl::EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	auto self = (CColorPickerCtrl*)data;
	int field = (int)id;
	bool shift = ::GetKeyState(VK_SHIFT) < 0;
	switch (msg) {
		case WM_NCDESTROY:
			::RemoveWindowSubclass(hWnd, EditSubclassProc, id);
			break;

		case WM_GETDLGCODE:
		{
			// Enter and Escape are the field's while it's being edited; otherwise the dialog's (OK, Cancel)
			LRESULT code = ::DefSubclassProc(hWnd, msg, wParam, lParam);
			auto m = (const MSG*)lParam;
			if (m && m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE) &&
				self->IsFieldEdited(field))
				code |= DLGC_WANTMESSAGE;
			return code;
		}

		case WM_KEYDOWN:
			switch (wParam) {
				case VK_UP:
				case VK_DOWN:
				case VK_PRIOR:
				case VK_NEXT:
					if (CpHasSpin((CpField)field)) {
						int delta = wParam == VK_PRIOR || wParam == VK_NEXT || shift ? 10 : 1;
						if (wParam == VK_DOWN || wParam == VK_NEXT)
							delta = -delta;
						if (self->StepField(field, delta))
							self->Notify(CPKN_COLORCHANGED, CPKHT_FIELD);
						return 0;
					}
					break;
				case VK_RETURN:
					self->CommitField(field);
					::SendMessage(hWnd, EM_SETSEL, 0, -1);
					return 0;
				case VK_ESCAPE:
					if (self->IsFieldEdited(field)) {
						self->RevertField(field);
						return 0;
					}
					break;
			}
			break;

		case WM_CHAR:
			if (wParam == L'\r' || wParam == 27)
				return 0;   // no beep
			break;

		case WM_MOUSEWHEEL:
			if (::GetFocus() == hWnd && CpHasSpin((CpField)field)) {
				self->m_WheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
				int steps = self->m_WheelRemainder / WHEEL_DELTA;
				self->m_WheelRemainder -= steps * WHEEL_DELTA;
				if (steps && self->StepField(field, steps * (shift ? 10 : 1)))
					self->Notify(CPKN_COLORCHANGED, CPKHT_FIELD);
				return 0;
			}
			break;
	}
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ---- Spin buttons and the swatch ------------------------------------------------------------

bool CColorPickerCtrl::SpinHitTest(POINT pt, int* pField, int* pDir) const {
	for (int f = 0; f < CpFieldCount; f++) {
		if (IsEmpty(m_Frames[f]) || !CpHasSpin((CpField)f))
			continue;
		RECT up, down;
		CpSpinRects(m_Frames[f], m_SpinWidth, &up, &down);
		if (::PtInRect(&up, pt) || ::PtInRect(&down, pt)) {
			*pField = f;
			*pDir = ::PtInRect(&up, pt) ? 1 : -1;
			return true;
		}
	}
	return false;
}

LRESULT CColorPickerCtrl::OnLButtonDown(UINT, WPARAM wParam, LPARAM lParam, BOOL&) {
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	int field, dir;
	if (m_SpinField < 0 && SpinHitTest(pt, &field, &dir)) {
		::SetFocus(m_Edits[field]);   // another field typed in commits first
		m_SpinField = field;
		m_SpinDir = dir;
		m_SpinPressed = true;
		m_SpinChanged = StepField(field, dir * ((wParam & MK_SHIFT) ? 10 : 1));
		SetCapture();
		SetTimer(SpinTimer, 400);   // then repeating faster
		InvalidateField(field);
		return 0;
	}
	if (HitTest(pt) == CPKHT_OLDCOLOR && m_HasOriginal) {
		auto c = CpUpdateHsv({ m_Hsv.h, m_Hsv.s, m_Hsv.v }, m_Original);
		Hsv n{ c.h, c.s, c.v };
		if (!SameState(n, m_OriginalAlpha)) {
			SetState(n, m_OriginalAlpha);
			Notify(CPKN_COLORCHANGING, CPKHT_OLDCOLOR);
			Notify(CPKN_COLORCHANGED, CPKHT_OLDCOLOR);
		}
	}
	return 0;
}

LRESULT CColorPickerCtrl::OnMouseMove(UINT, WPARAM, LPARAM lParam, BOOL&) {
	if (m_SpinField < 0)
		return 0;
	POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
	int field, dir;
	bool pressed = SpinHitTest(pt, &field, &dir) && field == m_SpinField && dir == m_SpinDir;
	if (pressed != m_SpinPressed) {
		m_SpinPressed = pressed;
		InvalidateField(m_SpinField);
	}
	return 0;
}

LRESULT CColorPickerCtrl::OnTimer(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
	if (wParam != SpinTimer) {
		bHandled = FALSE;
		return 0;
	}
	if (m_SpinField < 0) {
		KillTimer(SpinTimer);
		return 0;
	}
	SetTimer(SpinTimer, 50);
	if (m_SpinPressed && StepField(m_SpinField, m_SpinDir * (::GetKeyState(VK_SHIFT) < 0 ? 10 : 1)))
		m_SpinChanged = true;
	return 0;
}

LRESULT CColorPickerCtrl::OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&) {
	EndSpin();
	return 0;
}

LRESULT CColorPickerCtrl::OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&) {
	EndSpin();
	return 0;
}

void CColorPickerCtrl::EndSpin() {
	int field = m_SpinField;
	if (field < 0)
		return;
	m_SpinField = -1;   // first: releasing the capture comes back through OnCaptureChanged
	m_SpinPressed = false;
	KillTimer(SpinTimer);
	if (::GetCapture() == m_hWnd)
		::ReleaseCapture();
	InvalidateField(field);
	if (m_SpinChanged)
		Notify(CPKN_COLORCHANGED, CPKHT_FIELD);
	m_SpinChanged = false;
}

LRESULT CColorPickerCtrl::OnCtlColorEdit(UINT, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (FieldOf((HWND)lParam) < 0) {
		bHandled = FALSE;
		return 0;
	}
	auto c = GetColors();
	::SetTextColor((HDC)wParam, c.crText);
	::SetBkColor((HDC)wParam, c.crFieldBack);
	if (m_FieldBrushColor != c.crFieldBack) {
		if (!m_FieldBrush.IsNull())
			m_FieldBrush.DeleteObject();
		m_FieldBrush.CreateSolidBrush(c.crFieldBack);
		m_FieldBrushColor = c.crFieldBack;
	}
	return (LRESULT)m_FieldBrush.m_hBrush;
}

// ---- Painting the panel ------------------------------------------------------------------------

LRESULT CColorPickerCtrl::OnPaint(UINT uMsg, WPARAM wParam, LPARAM, BOOL&) {
	if (uMsg == WM_PRINTCLIENT) {
		RECT rc;
		GetClientRect(&rc);
		PaintPanel((HDC)wParam, rc);
		return 0;
	}
	WTL::CPaintDC dc(m_hWnd);
	WTL::CMemoryDC mdc(dc, dc.m_ps.rcPaint);
	PaintPanel(mdc, dc.m_ps.rcPaint);
	return 0;
}

void CColorPickerCtrl::PaintPanel(HDC hdc, const RECT& rcPaint) {
	auto c = GetColors();
	FillSolid(hdc, rcPaint, c.crBack);

	// the swatch: the new color over the original one
	if (!IsEmpty(m_Swatch)) {
		Frame(hdc, m_Swatch, c.crBorder);
		int cell = Scale(6);
		DrawPixels(hdc, PartRect(CPKHT_NEWCOLOR), [&](const CpPixels& px) {
			CpRenderSwatch(px, GetColor(), m_Alpha, cell, c.crCheck1, c.crCheck2);
		});
		if (m_HasOriginal)
			DrawPixels(hdc, PartRect(CPKHT_OLDCOLOR), [&](const CpPixels& px) {
				CpRenderSwatch(px, m_Original, m_OriginalAlpha, cell, c.crCheck1, c.crCheck2);
			});
	}

	// the fields: labels, frames, spin buttons (the edits paint the rest)
	auto oldFont = ::SelectObject(hdc, m_hFont);
	::SetBkMode(hdc, TRANSPARENT);
	HWND focus = ::GetFocus();
	COLORREF arrow = Blend(c.crFieldBack, c.crText, 75);
	for (int f = 0; f < CpFieldCount; f++) {
		RECT frame = m_Frames[f];
		if (IsEmpty(frame))
			continue;
		RECT label = m_Labels[f];
		::SetTextColor(hdc, c.crText);
		::DrawTextW(hdc, FieldNames[f], -1, &label, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

		RECT inner = frame;
		::InflateRect(&inner, -1, -1);
		FillSolid(hdc, inner, c.crFieldBack);
		COLORREF border = m_Bad[f] ? BadColor : focus == m_Edits[f] ? ::GetSysColor(COLOR_HIGHLIGHT) : c.crBorder;
		Frame(hdc, frame, border);

		if (CpHasSpin((CpField)f)) {
			RECT up, down;
			CpSpinRects(frame, m_SpinWidth, &up, &down);
			if (m_SpinField == f && m_SpinPressed)
				FillSolid(hdc, m_SpinDir > 0 ? up : down, Blend(c.crFieldBack, c.crText, 22));
			int width = std::max(5, Scale(7)) | 1;
			Arrow(hdc, up, true, width, arrow);
			Arrow(hdc, down, false, width, arrow);
		}
	}
	::SelectObject(hdc, oldFont);
}
