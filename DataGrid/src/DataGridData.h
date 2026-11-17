#pragma once

// What CDataGridCtrl keeps (behind its m_Data), shared by its source files.

#include "DataGrid.h"
#include "DataGridModel.h"

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
	DgPos CaretPos() const {   // in view; (0, 0) if there's none
		int64_t row = rows.ToView(caretRow);
		int col = layout.IndexOf(caretId);
		return { std::max<int64_t>(0, row), std::max(0, col) };
	}
};

// the text a cell shows (dates in the user's format)
std::wstring DgCellText(const DgCell& cell, const DgColumn& column, const DgNumberFormat& nf);
