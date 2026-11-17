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

	// the header's mouse
	enum class Drag { None, Resize, Press, Reorder } drag = Drag::None;
	int dragCol = -1;               // an index into layout.cols
	int dragStartX = 0, dragStartWidth = 0, dragX = 0, dragOffset = 0;
	int dropIndex = -1;
	int hotCol = -1;                // the title under the mouse
	bool tracking = false;          // TrackMouseEvent for WM_MOUSELEAVE

	// metrics (pixels) and looks
	int rowHeightDips = 0;          // 0: from the font
	int textHeight = 16;
	int digitWidth = 8;
	DGVCOLORS userColors{ CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT, CLR_DEFAULT };
	struct Colors {
		COLORREF back, text, grid, headerBack, headerText, headerHot, headerPressed, caretRow, caret, caretInactive, link;
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
};

// the text a cell shows (dates in the user's format)
std::wstring DgCellText(const DgCell& cell, const DgColumn& column, const DgNumberFormat& nf);
