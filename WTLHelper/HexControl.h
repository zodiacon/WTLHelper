#pragma once

struct HexControlColors {
	COLORREF Text{ ::GetSysColor(COLOR_WINDOWTEXT) };
	COLORREF Background{ ::GetSysColor(COLOR_WINDOW) };
	COLORREF Ascii{ RGB(128, 0, 0) };
	COLORREF Offset{ RGB(0, 0, 128) };
	COLORREF SelectionText{ ::GetSysColor(COLOR_HIGHLIGHTTEXT) };
	COLORREF SelectionBackground{ ::GetSysColor(COLOR_HIGHLIGHT) };
	COLORREF Modified{ RGB(255, 0, 0) };
};

struct IBufferManager;

struct IHexControlCallback {
	virtual void OnContextMenu(POINT const& pt) = 0;
	virtual void OnSizeChanged(int64_t newSize) = 0;
};

struct IHexControl abstract {
	virtual HWND GetHwnd() const = 0;
	virtual void SetBufferManager(IBufferManager* mgr) = 0;
	virtual IBufferManager* GetBufferManager() const = 0;
	virtual void SetReadOnly(bool readonly) = 0;
	virtual bool IsReadOnly() const = 0;
	virtual void SetAllowExtension(bool allow) = 0;
	virtual bool IsAllowExtension() const = 0;
	virtual bool CanUndo() const = 0;
	virtual bool CanRedo() const = 0;
	virtual bool Undo() = 0;
	virtual bool Redo() = 0;
	virtual void SetSize(int64_t size) = 0;
	virtual bool SetDataSize(int32_t size) = 0;
	virtual int32_t GetDataSize() const = 0;
	virtual bool SetBytesPerLine(int32_t bytesPerLine) = 0;
	virtual int32_t GetBytesPerLine() const = 0;
	virtual bool Copy(int64_t offset = -1, int64_t size = 0) = 0;
	virtual bool Paste(int64_t offset = -1) = 0;
	virtual bool CanCopy() const = 0;
	virtual bool CanPaste() const = 0;
	virtual bool Cut(int64_t offset = -1, int64_t size = 0) = 0;
	virtual bool Delete(int64_t offset = -1, int64_t size = 0) = 0;
	virtual bool CanCut() const = 0;
	virtual bool CanDelete() const = 0;
	virtual int64_t SetBiasOffset(int64_t offset) = 0;
	virtual int64_t GetBiasOffset() const = 0;
	virtual HexControlColors& GetColors() = 0;
	virtual std::wstring GetText(int64_t offset, int64_t size) = 0;
	virtual void Refresh() = 0;
	virtual bool DeleteState(int64_t offset) = 0;
	virtual bool SetModified(int64_t offset, uint32_t size, bool modified) = 0;
	virtual bool ToggleModified(int64_t offset, uint32_t size) = 0;
	virtual uint32_t Fill(int64_t offset, uint8_t value, uint32_t count) = 0;
	virtual bool SetHexControlClient(IHexControlCallback* client) = 0;
};

enum class HexControlOptions {
	None = 0,
	DisableUndoRedo = 1,
	GrayOutZeros = 2,
};
DEFINE_ENUM_FLAG_OPERATORS(HexControlOptions);


class CHexControl :
	public IHexControl,
	public CZoomScrollWindowImpl<CHexControl> {
public:
	DECLARE_WND_CLASS_EX(L"HexControl", CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, NULL)

	BEGIN_MSG_MAP(CHexControl)
		CHAIN_MSG_MAP(CZoomScrollWindowImpl<CHexControl>)
	END_MSG_MAP()

	void DoPaint(CDCHandle dc);

protected:
	void UpdateLayout();

	// Inherited via IHexControl
	virtual HWND GetHwnd() const override;
	virtual void SetBufferManager(IBufferManager* mgr) override;
	virtual IBufferManager* GetBufferManager() const override;
	virtual void SetReadOnly(bool readonly) override;
	virtual bool IsReadOnly() const override;
	virtual void SetAllowExtension(bool allow) override;
	virtual bool IsAllowExtension() const override;
	virtual bool CanUndo() const override;
	virtual bool CanRedo() const override;
	virtual bool Undo() override;
	virtual bool Redo() override;
	virtual void SetSize(int64_t size) override;
	virtual bool SetDataSize(int32_t size) override;
	virtual int32_t GetDataSize() const override;
	virtual bool SetBytesPerLine(int32_t bytesPerLine) override;
	virtual int32_t GetBytesPerLine() const override;
	virtual bool Copy(int64_t offset, int64_t size) override;
	virtual bool Paste(int64_t offset) override;
	virtual bool CanCopy() const override;
	virtual bool CanPaste() const override;
	virtual bool Cut(int64_t offset, int64_t size) override;
	virtual bool Delete(int64_t offset, int64_t size) override;
	virtual bool CanCut() const override;
	virtual bool CanDelete() const override;
	virtual int64_t SetBiasOffset(int64_t offset) override;
	virtual int64_t GetBiasOffset() const override;
	virtual HexControlColors& GetColors() override;
	virtual std::wstring GetText(int64_t offset, int64_t size) override;
	virtual void Refresh() override;
	virtual bool DeleteState(int64_t offset) override;
	virtual bool SetModified(int64_t offset, uint32_t size, bool modified) override;
	virtual bool ToggleModified(int64_t offset, uint32_t size) override;
	virtual uint32_t Fill(int64_t offset, uint8_t value, uint32_t count) override;
	virtual bool SetHexControlClient(IHexControlCallback* client) override;

private:
	IBufferManager* m_bm{ nullptr };
	uint32_t m_BytesPerLine{ 16 };
	uint32_t m_Lines;
	HexControlColors m_colors;
	int m_CharHeight{ 16 };
};

