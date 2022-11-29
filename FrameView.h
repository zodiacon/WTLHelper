#pragma once

template<typename T, typename TFrame, bool dynamic = true>
class CFrameView abstract :
	public CFrameWindowImpl<T, CWindow, CControlWinTraits> {
public:
	using BaseFrame = CFrameWindowImpl<T, CWindow, CControlWinTraits>;
	explicit CFrameView(TFrame* frame) : m_pFrame(frame) {}

	TFrame* Frame() const {
		return m_pFrame;
	}

	void OnFinalMessage(HWND) override {
		if(dynamic)
			delete this;
	}

	BEGIN_MSG_MAP(CFrameView)
		CHAIN_MSG_MAP(BaseFrame)
	END_MSG_MAP()

private:
	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	TFrame* m_pFrame;
};
