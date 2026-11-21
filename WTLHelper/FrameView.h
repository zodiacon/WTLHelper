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
		// not 'delete this': 'this' here is CFrameView<T, TFrame, TBase>*, and nothing in the ATL chain
		// gives it (or CWindowImplBaseT) a virtual destructor, so deleting through it would only run
		// CFrameView's own (trivial) destructor and leak whatever T itself owns
		if (!m_Static)
			delete static_cast<T*>(this);
	}

private:
	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	TFrame* m_pFrame;
	bool m_Static{ false };
};
