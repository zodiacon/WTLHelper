#pragma once

#include "Selection.h"
#include <span>
#include <unordered_set>
#include <atltheme.h>
#include <atltypes.h>

struct HexControlColors {
	COLORREF Text{ ::GetSysColor(COLOR_WINDOWTEXT) };
	COLORREF Background{ ::GetSysColor(COLOR_WINDOW) };
	COLORREF Ascii{ RGB(128, 0, 0) };
	COLORREF Offset{ RGB(0, 0, 128) };
	COLORREF SelectionText{ ::GetSysColor(COLOR_HIGHLIGHTTEXT) };
	COLORREF SelectionBackground{ ::GetSysColor(COLOR_HIGHLIGHT) };
	COLORREF Modified{ RGB(255, 0, 0) };
};

enum class HexControlOptions {
	None = 0,
	DisableUndoRedo = 1,
	GrayOutZeros = 2,
};
DEFINE_ENUM_FLAG_OPERATORS(HexControlOptions);

constexpr auto NMHX_SELECTION_CHANGED = 0x2000;

struct NMHexControlNotify : NMHDR {
};

class CHexControl :
	public CBufferedPaintWindowImpl<CHexControl> {
public:
	DECLARE_WND_CLASS_EX(L"WTLHexControl", CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, NULL)

	int64_t GetSize() const;
	int64_t GetData(int64_t offset, int64_t size, uint8_t*& p);
	int64_t GetData(int64_t offset, int64_t size, uint8_t const*& p) const;
	void SetReadOnly(bool readonly);
	bool IsReadOnly() const;
	bool SetInsertMode(bool insert);
	bool IsInsertMode() const;
	bool IsDataOwner() const;

	void SetSize(int64_t size);
	bool SetDataSize(int32_t size);
	int32_t GetDataSize() const;
	bool SetBytesPerLine(int32_t bytesPerLine);
	int32_t GetBytesPerLine() const;
	bool Copy(int64_t offset = -1, int64_t size = 0) const;
	bool Paste(int64_t offset = -1);
	bool HasSelection() const;
	bool CanCopy() const;
	bool CanPaste() const;
	bool Cut();
	bool Delete();
	void ClearAll();
	void SetDirty(bool dirty);
	bool IsDirty() const;
	bool CanCut() const;
	bool CanDelete() const;
	Selection const& GetSelection() const;
	int64_t SetBiasOffset(int64_t offset);
	int64_t GetBiasOffset() const;
	int64_t Fill(int64_t offset, uint8_t value, int64_t count);
	HexControlColors& GetColors();
	std::wstring GetText(int64_t offset, int64_t size) const;
	void Refresh();
	bool SetData(int64_t offset, std::span<const uint8_t> data, bool update = true);
	bool InitData(uint8_t* p, int64_t size, bool owner = false);
	bool InsertData(int64_t offset, std::span<uint8_t> data, bool update = true);
	bool LoadFile(PCWSTR path);

	BEGIN_MSG_MAP(CHexControl)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLeftButtonUp)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftButtonDown)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDialogCode)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		CHAIN_MSG_MAP(CBufferedPaintWindowImpl<CHexControl>)
	END_MSG_MAP()

	void DoPaint(CDCHandle dc, RECT& rect);

private:
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnHScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseWheel(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLeftButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDialogCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChar(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLeftButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	void CancelEdit();
	bool CopyText(PCWSTR text) const;
	void SendSelectionChanged();
	void RecalcLayout();
	void InitFontMetrics();
	CPoint GetPointFromOffset(int64_t offset) const;
	int64_t GetOffsetFromPoint(const POINT& pt) const;
	void DrawNumber(CDCHandle dc, int64_t offset, uint64_t value, uint32_t editDigits);
	void DrawAscii(CDCHandle dc, int64_t offset, int chars);
	CString FormatNumber(ULONGLONG number, int size = 0) const;

	void UpdateCaret();
	void RedrawWindow(RECT* = nullptr);
	void ClearSelection();
	void CommitValue(int64_t offset, uint64_t value);
	void ResetInput();
	int64_t NormalizeOffset(int64_t& offset) const;
	void RedrawCaretLine();

private:
	HexControlColors m_Colors;
	CFont m_Font;
	std::vector<uint8_t> m_Data;
	uint8_t* m_pBuffer{ nullptr };
	int64_t m_Size{ 0 };
	std::wstring m_FileName;
	int m_FontPointSize{ 110 };
	int m_Lines{ 1 };
	int m_Chars{ 0 };
	int m_CharWidth, m_CharHeight;
	std::vector<std::wstring> m_Text;
	int64_t m_StartOffset{ 0 }, m_BiasOffset{ 0 };
	uint32_t m_DataSize{ 1 }, m_BytesPerLine{ 32 };
	int64_t m_CaretOffset{ 0 };
	int m_AddressDigits{ 8 };
	int m_EditDigits{ 0 }, m_LastDigits{ 0 };
	Selection m_Selection;
	uint64_t m_CurrentInput{ 0 }, m_OldValue;
	std::unordered_set<int64_t> m_Modified;
	NMHexControlNotify m_Notify;
	bool m_InsertMode : 1 { true };
	bool m_ReadOnly : 1 { false };
	bool m_Dirty : 1 { false };
	bool m_AllowGrow : 1 { false };
};

