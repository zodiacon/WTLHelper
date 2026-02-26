#pragma once

template<typename T, typename TFrame, typename TBase = CWindow>
class CFrameView :
	public CFrameWindowImpl<T, TBase, CControlWinTraits> {
public:
	using BaseFrame = CFrameWindowImpl<T, TBase, CControlWinTraits>;
	explicit CFrameView(TFrame* frame) : m_pFrame(frame) {}

	void SetStatic(bool s = true) {
		m_Static = s;
	}

	TFrame* Frame() const {
		return m_pFrame;
	}

	void OnFinalMessage(HWND) override {
		if(!m_Static)
			delete this;
	}

private:
	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	TFrame* m_pFrame;
	bool m_Static{ false };
};
