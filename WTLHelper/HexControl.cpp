#include "pch.h"
#include "HexControl.h"
#include "IBufferManager.h"

void CHexControl::DoPaint(CDCHandle dc) {
	if (!m_bm)
		return;

	CFont font;
	font.CreatePointFont(110, L"Consolas");
	dc.SelectFont(font);
	//dc.SetBkMode(TRANSPARENT);

	int64_t offset = m_ptOffset.y / m_CharHeight * m_BytesPerLine;
	CString text;
	RECT rc;
	GetClientRect(&rc);
	int lines = rc.bottom / m_CharHeight + 1;
	for (int i = 0; i < lines; i++) {
		text.Format(L"%08X", offset + i * m_BytesPerLine);
		dc.TextOutW(0, m_ptOffset.y + i * m_CharHeight, text);
	}
}

void CHexControl::UpdateLayout() {
	auto size = m_bm->GetSize();
	SetScrollSize(100, m_Lines = (1 + size / m_BytesPerLine) * m_CharHeight, TRUE, true);
}

HWND CHexControl::GetHwnd() const {
	return m_hWnd;
}

void CHexControl::SetBufferManager(IBufferManager* mgr) {
	m_bm = mgr;
	if (m_bm) {
		UpdateLayout();
	}
	else {
		Invalidate();
	}
}

IBufferManager* CHexControl::GetBufferManager() const {
	return nullptr;
}

void CHexControl::SetReadOnly(bool readonly) {
}

bool CHexControl::IsReadOnly() const {
	return false;
}

void CHexControl::SetAllowExtension(bool allow) {
}

bool CHexControl::IsAllowExtension() const {
	return false;
}

bool CHexControl::CanUndo() const {
	return false;
}

bool CHexControl::CanRedo() const {
	return false;
}

bool CHexControl::Undo() {
	return false;
}

bool CHexControl::Redo() {
	return false;
}

void CHexControl::SetSize(int64_t size) {
}

bool CHexControl::SetDataSize(int32_t size) {
	return false;
}

int32_t CHexControl::GetDataSize() const {
	return int32_t();
}

bool CHexControl::SetBytesPerLine(int32_t bytesPerLine) {
	return false;
}

int32_t CHexControl::GetBytesPerLine() const {
	return int32_t();
}

bool CHexControl::Copy(int64_t offset, int64_t size) {
	return false;
}

bool CHexControl::Paste(int64_t offset) {
	return false;
}

bool CHexControl::CanCopy() const {
	return false;
}

bool CHexControl::CanPaste() const {
	return false;
}

bool CHexControl::Cut(int64_t offset, int64_t size) {
	return false;
}

bool CHexControl::Delete(int64_t offset, int64_t size) {
	return false;
}

bool CHexControl::CanCut() const {
	return false;
}

bool CHexControl::CanDelete() const {
	return false;
}

int64_t CHexControl::SetBiasOffset(int64_t offset) {
	return int64_t();
}

int64_t CHexControl::GetBiasOffset() const {
	return int64_t();
}

HexControlColors& CHexControl::GetColors() {
	return m_colors;
}

std::wstring CHexControl::GetText(int64_t offset, int64_t size) {
	return std::wstring();
}

void CHexControl::Refresh() {
}

bool CHexControl::DeleteState(int64_t offset) {
	return false;
}

bool CHexControl::SetModified(int64_t offset, uint32_t size, bool modified) {
	return false;
}

bool CHexControl::ToggleModified(int64_t offset, uint32_t size) {
	return false;
}

uint32_t CHexControl::Fill(int64_t offset, uint8_t value, uint32_t count) {
	return uint32_t();
}

bool CHexControl::SetHexControlClient(IHexControlCallback* client) {
	return false;
}
