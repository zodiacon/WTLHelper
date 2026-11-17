#pragma once

// What CDataGridCtrl keeps (behind its m_Data), shared by its source files.

#include "DataGrid.h"
#include "DataGridModel.h"

#include <memory>
#include <string>

struct DgData {
	// the columns and the rows
	DgColumnSet columns;
	int frozen = 0;                 // as asked; the layout may freeze fewer
	bool virtualMode = false;
	int64_t virtualCount = 0;
	DgStore store;
	DgCellCache cache;              // virtual mode's
	DgRowMap rows;                  // rows in view to rows in the data

	// laid out and scrolled
	DgLayout layout;
	int clientWidth = 0, clientHeight = 0;
	int scrollX = 0;
	int64_t topRow = 0;
	bool relayingOut = false, sizedAgain = false;

	// the caret: a data row (-1: none) and a column's ID
	int64_t caretRow = -1;
	int caretId = 0;
	bool focused = false;

	// the selection (rows in view, visible columns) and the sort
	DgSelection selection;
	std::vector<DgSortKey> sortKeys;
	std::vector<int> shownIds;      // the visible columns the selection was made over: another set resets it

	// the mouse: the header's (resizing, pressing a title, moving a column) or selecting cells
	enum class Drag { None, Resize, Press, Reorder, Select } drag = Drag::None;
	bool selectRows = false;        // selecting by the row numbers: whole rows
	static constexpr UINT_PTR ScrollTimer = 1;
	static constexpr UINT_PTR ErrorTimer = 2;

	// editing
	struct Edit {
		bool active = false;
		int64_t row = -1;           // a data row
		int id = 0;
		enum class Kind { Text, List, Date } kind = Kind::Text;
		bool typed = false;         // opened by typing: the arrows commit
		bool dirty = false;         // changed in the editor: nothing is written otherwise
		bool closing = false;       // being committed: the view changing under it doesn't end it again
	} edit;
	DgUndoStack undo;

	// filtering and grouping: the view's groups (for their rows) and the job rebuilding it
	std::vector<DgFilter> filters;
	std::vector<int> groupIds;
	DgViewResult view;
	std::unordered_set<std::wstring> collapsed;   // groups' keys
	struct Job {
		std::atomic<bool> cancel{ false };
		uint64_t id = 0;
		DgViewInput input;
		DgViewResult result;
	};
	std::shared_ptr<Job> job;       // running on the worker thread
	uint64_t generation = 0;
	bool ensureCaret = false;       // a user's sort: the caret's row into view once the rows are shown
	int filterEditId = 0;           // the column typed in the filter row (0: none)
	int hotFilterCol = -1;
	static constexpr UINT_PTR FilterTimer = 3;
	static constexpr int64_t BackgroundRows = 100'000;

	// conditional formats, finding
	std::vector<DgFormatRule> rules;
	int nextRuleId = 1;
	std::wstring findText;          // the last find, for F3
	unsigned findFlags = 0;
	std::wstring highlight;         // cells matching it are shaded
	unsigned highlightFlags = 0;
	bool findMiss = false;          // the find box's text matches nothing: shown in red

	// the tooltip with a cut-off text in full, over the text (in place, as a list view's)
	HWND tip = nullptr;
	struct TipKey {
		int64_t row = -3;            // a row in view; -1: a title
		int col = -1;
		bool operator==(const TipKey&) const = default;
	} tipKey;
	std::wstring tipText;
	RECT tipRect{};                 // the text's place (client)
	bool tipBold = false;

	HWND errorTip = nullptr;
	HBRUSH editBrush = nullptr;
	COLORREF editBrushColor = CLR_INVALID;
	int dragCol = -1;               // an index into layout.cols
	int dragStartX = 0, dragStartWidth = 0, dragX = 0, dragOffset = 0;
	int dropIndex = -1;
	int hotCol = -1;                // the title under the mouse
	bool tracking = false;          // TrackMouseEvent for WM_MOUSELEAVE

	// metrics (pixels) and looks
	int rowHeightDips = 0;          // 0: from the font
	int textHeight = 16;
	int digitWidth = 8;
	DGVCOLORS userColors{ CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT };
	struct Colors {
		COLORREF back, text, grid, headerBack, headerText, headerHot, headerPressed, caretRow, caret, caretInactive, link,
			selection, selectionInactive, selectionText;
		bool highContrast;
	} colors{};
	HIMAGELIST images = nullptr;
	DgNumberFormat numberFormat;

	std::wstring cellText;          // GetCell's text, valid until the next call

	int64_t RowCount() const {
		return rows.Count();
	}
	int64_t DataRowCount() const {
		return virtualMode ? virtualCount : store.Count();
	}
	DgView View() const {
		return { scrollX, topRow, RowCount(), clientWidth, clientHeight };
	}
	int ColCount() const {
		return (int)layout.cols.size();
	}
	bool HasCaret() const {         // a data row, or the new row's placeholder
		return caretRow >= 0 || (caretRow == DgRowMap::NewRow && rows.HasNewRow());
	}
	DgPos CaretPos() const {   // in view; (0, 0) if there's none
		int64_t row = rows.ToView(caretRow);
		int col = layout.IndexOf(caretId);
		return { std::max<int64_t>(0, row), std::max(0, col) };
	}
};

// the text a cell shows (dates in the user's format)
std::wstring DgCellText(const DgCell& cell, const DgColumn& column, const DgNumberFormat& nf);

// the text a value shows in a column (a cell's, without its image); safe on the worker thread
std::wstring DgValueText(const DgValue& value, const DgColumn& column, const DgNumberFormat& nf);

// a date in the user's format (VarDateFromStr), for pasting what was copied
bool DgParseUserDate(const std::wstring& text, int64_t& ticks);

// ---- Between the API's values and the model's -------------------------------------------

inline int64_t DgTicksOf(const FILETIME& ft) {
	return (int64_t)(((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
}

inline FILETIME DgFileTimeOf(int64_t ticks) {
	return { (DWORD)(uint64_t)ticks, (DWORD)((uint64_t)ticks >> 32) };
}

inline DgValue DgFromPublic(const DGVVALUE& v) {
	switch (v.type) {
		case DGVVT_TEXT:     return DgValue::Text(v.pszText ? v.pszText : L"");
		case DGVVT_INT:      return DgValue::Int(v.llVal);
		case DGVVT_FLOAT:    return DgValue::Float(v.dblVal);
		case DGVVT_BOOL:     return DgValue::Bool(v.bVal != FALSE);
		case DGVVT_DATETIME: return DgValue::DateTime(DgTicksOf(v.ftVal));
	}
	return {};
}

inline DGVVALUE DgToPublic(const DgValue& v, LPCWSTR text) {   // text: the value's, kept alive by the caller
	switch (v.type) {
		case DgType::Text:     return DgvText(text);
		case DgType::Int:      return DgvInt(v.i);
		case DgType::Float:    return DgvFloat(v.d);
		case DgType::Bool:     return DgvBool(v.i != 0);
		case DgType::DateTime: return DgvDateTime(DgFileTimeOf(v.i));
	}
	return DgvEmpty();
}
