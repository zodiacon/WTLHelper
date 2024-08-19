// View.h : interface of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <HexControl.h>
#include <IBufferManager.h>

class SimpleBuffer : public IBufferManager {
	// Inherited via IBufferManager
	virtual uint32_t GetData(int64_t offset, uint8_t* buffer, uint32_t count) override;
	virtual bool Insert(int64_t offset, const uint8_t* data, uint32_t count) override;
	virtual bool Delete(int64_t offset, size_t count) override;
	virtual bool SetData(int64_t offset, const uint8_t* data, uint32_t count) override;
	virtual int64_t GetSize() const override;
	virtual uint8_t* GetRawData(int64_t offset) override;
	virtual bool IsReadOnly() const override;
};

class CView : public CWindowImpl<CView> {
public:
	DECLARE_WND_CLASS(NULL)

		BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(CView)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
	END_MSG_MAP()

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CHexControl m_Hex;
	SimpleBuffer m_buffer;
};
