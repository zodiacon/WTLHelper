// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "View.h"

BOOL CView::PreTranslateMessage(MSG* pMsg) {
	pMsg;
	return FALSE;
}

LRESULT CView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Hex.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);

	return 0;
}

LRESULT CView::OnSize(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Hex)
		m_Hex.MoveWindow(0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
	return LRESULT();
}

LRESULT CView::OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
	return 1;
}

uint32_t SimpleBuffer::GetData(int64_t offset, uint8_t* buffer, uint32_t count) {
	return uint32_t();
}

bool SimpleBuffer::Insert(int64_t offset, const uint8_t* data, uint32_t count) {
	return false;
}

bool SimpleBuffer::Delete(int64_t offset, size_t count) {
	return false;
}

bool SimpleBuffer::SetData(int64_t offset, const uint8_t* data, uint32_t count) {
	return false;
}

int64_t SimpleBuffer::GetSize() const {
	return 1 << 20;
}

uint8_t* SimpleBuffer::GetRawData(int64_t offset) {
	return nullptr;
}

bool SimpleBuffer::IsReadOnly() const {
	return false;
}
