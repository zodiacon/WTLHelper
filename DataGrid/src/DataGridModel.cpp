#include "DataGridModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

// ---- Values ------------------------------------------------------------------------------

DgValue DgValue::Text(std::wstring text) {
	DgValue v;
	v.type = DgType::Text;
	v.s = std::move(text);
	return v;
}

DgValue DgValue::Int(int64_t value) {
	DgValue v;
	v.type = DgType::Int;
	v.i = value;
	return v;
}

DgValue DgValue::Float(double value) {
	DgValue v;
	v.type = DgType::Float;
	v.d = value;
	return v;
}

DgValue DgValue::Bool(bool value) {
	DgValue v;
	v.type = DgType::Bool;
	v.i = value ? 1 : 0;
	return v;
}

DgValue DgValue::DateTime(int64_t ticks) {
	DgValue v;
	v.type = DgType::DateTime;
	v.i = ticks;
	return v;
}

// ---- Columns -------------------------------------------------------------------------------

std::vector<std::wstring> DgSplitEnumItems(const wchar_t* items) {
	std::vector<std::wstring> result;
	if (!items)
		return result;
	while (*items) {
		result.emplace_back(items);
		items += result.back().size() + 1;
	}
	return result;
}

int DgColumnSet::Insert(DgColumn column, int displayIndex) {
	column.id = m_NextId++;
	column.slot = m_NextSlot++;
	int id = column.id;
	m_Columns.push_back(std::move(column));
	if (displayIndex < 0 || displayIndex > (int)m_Order.size())
		displayIndex = (int)m_Order.size();
	m_Order.insert(m_Order.begin() + displayIndex, id);
	return id;
}

bool DgColumnSet::Erase(int id) {
	auto it = std::find_if(m_Columns.begin(), m_Columns.end(), [&](auto& c) { return c.id == id; });
	if (it == m_Columns.end())
		return false;
	m_Columns.erase(it);
	std::erase(m_Order, id);
	return true;
}

DgColumn* DgColumnSet::Find(int id) {
	for (auto& c : m_Columns)
		if (c.id == id)
			return &c;
	return nullptr;
}

const DgColumn* DgColumnSet::Find(int id) const {
	return const_cast<DgColumnSet*>(this)->Find(id);
}

int DgColumnSet::DisplayIndexOf(int id) const {
	auto it = std::find(m_Order.begin(), m_Order.end(), id);
	return it == m_Order.end() ? -1 : (int)(it - m_Order.begin());
}

bool DgColumnSet::SetOrder(const std::vector<int>& ids) {
	if (ids.size() != m_Order.size())
		return false;
	auto sorted = ids;
	std::sort(sorted.begin(), sorted.end());
	if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
		return false;
	for (int id : ids)
		if (!Find(id))
			return false;
	m_Order = ids;
	return true;
}

bool DgColumnSet::Move(int id, int beforeId) {
	if (DisplayIndexOf(id) < 0 || (beforeId && DisplayIndexOf(beforeId) < 0))
		return false;
	if (id == beforeId)
		return true;
	std::erase(m_Order, id);
	if (beforeId == 0)
		m_Order.push_back(id);
	else
		m_Order.insert(m_Order.begin() + DisplayIndexOf(beforeId), id);
	return true;
}

std::vector<const DgColumn*> DgColumnSet::Visible() const {
	std::vector<const DgColumn*> result;
	for (int id : m_Order)
		if (auto c = Find(id); c && !c->Hidden())
			result.push_back(c);
	return result;
}

// ---- Layout ------------------------------------------------------------------------------------

int DgScale(int dips, int dpi) {
	return (int)(((int64_t)dips * dpi + 48) / 96);
}

int DgUnscale(int pixels, int dpi) {
	return dpi ? (int)(((int64_t)pixels * 96 + dpi / 2) / dpi) : pixels;
}

int DgLayout::IndexOf(int id) const {
	for (size_t i = 0; i < cols.size(); i++)
		if (cols[i].id == id)
			return (int)i;
	return -1;
}

DgLayout DgLayoutColumns(const DgColumnSet& columns, const DgLayoutParams& params) {
	DgLayout layout;
	layout.rowHeader = params.rowHeader;
	layout.header = params.header;
	layout.rowHeight = std::max(1, params.rowHeight);

	auto visible = columns.Visible();
	int fixed = 0, weights = 0;
	for (auto c : visible) {
		if (c->fill > 0)
			weights += c->fill;
		else
			fixed += std::max(DgScale(c->width, params.dpi), DgScale(c->minWidth, params.dpi));
	}
	int spare = std::max(0, params.clientWidth - params.rowHeader - fixed);

	int x = 0, weightSoFar = 0;
	for (auto c : visible) {
		DgColLayout col;
		col.id = c->id;
		col.left = x;
		int minWidth = DgScale(c->minWidth, params.dpi);
		if (c->fill > 0) {
			// cumulative shares, so the fill columns add up to the spare width exactly
			int before = (int)((int64_t)spare * weightSoFar / weights);
			weightSoFar += c->fill;
			int after = (int)((int64_t)spare * weightSoFar / weights);
			col.width = std::max(minWidth, after - before);
		}
		else {
			col.width = std::max(DgScale(c->width, params.dpi), minWidth);
		}
		x += col.width;
		layout.cols.push_back(col);
	}
	layout.totalWidth = x;

	int frozen = std::clamp(params.frozen, 0, (int)layout.cols.size());
	auto widthOf = [&](int count) {
		return count ? layout.cols[count - 1].left + layout.cols[count - 1].width : 0;
	};
	while (frozen > 0 && widthOf(frozen) >= params.clientWidth - params.rowHeader)
		frozen--;
	layout.frozenCount = frozen;
	layout.frozenWidth = widthOf(frozen);
	return layout;
}

// ---- Scrolling ---------------------------------------------------------------------------------

int DgMaxScrollX(const DgLayout& layout, int clientWidth) {
	return std::max(0, layout.totalWidth - (clientWidth - layout.rowHeader));
}

int DgScrollXToShow(const DgLayout& layout, int scrollX, int clientWidth, int index) {
	if (index < layout.frozenCount || index >= (int)layout.cols.size())
		return scrollX;
	int view = clientWidth - layout.ScrollLeft();
	int left = layout.cols[index].left - layout.frozenWidth;
	int right = left + layout.cols[index].width;
	if (left < scrollX)
		scrollX = left;
	else if (right > scrollX + view)
		scrollX = std::min(left, right - view);
	return std::clamp(scrollX, 0, DgMaxScrollX(layout, clientWidth));
}

int64_t DgFullRows(const DgLayout& layout, int clientHeight) {
	return std::max<int64_t>(1, (clientHeight - layout.header) / layout.rowHeight);
}

int64_t DgMaxTopRow(int64_t rowCount, int64_t fullRows) {
	return std::max<int64_t>(0, rowCount - std::max<int64_t>(1, fullRows));
}

int64_t DgTopToShow(int64_t top, int64_t fullRows, int64_t row) {
	if (row < top)
		return row;
	if (row >= top + fullRows)
		return row - std::max<int64_t>(1, fullRows) + 1;
	return top;
}

int DgScrollMap::Range(int64_t maxTop) {
	return maxTop <= MaxPos ? (int)std::max<int64_t>(0, maxTop) : MaxPos;
}

int DgScrollMap::ToPos(int64_t top, int64_t maxTop) {
	top = std::clamp<int64_t>(top, 0, std::max<int64_t>(0, maxTop));
	if (maxTop <= MaxPos)
		return (int)top;
	if (top == maxTop)
		return MaxPos;
	return (int)std::min<double>(MaxPos - 1, std::floor((double)top / (double)maxTop * MaxPos));
}

int64_t DgScrollMap::FromPos(int pos, int64_t maxTop) {
	if (maxTop <= 0 || pos <= 0)
		return 0;
	if (maxTop <= MaxPos)
		return std::min<int64_t>(pos, maxTop);
	if (pos >= MaxPos)
		return maxTop;
	return std::min<int64_t>(maxTop - 1, (int64_t)std::ceil((double)pos / MaxPos * (double)maxTop));
}

// ---- Hit testing ------------------------------------------------------------------------------

namespace {
	int ColumnAt(const DgLayout& layout, int scrollX, int x) {
		if (x < layout.rowHeader)
			return -1;
		int n = (int)layout.cols.size();
		if (x < layout.ScrollLeft()) {
			for (int i = 0; i < layout.frozenCount; i++)
				if (x < layout.Right(i, 0))
					return i;
			return -1;
		}
		for (int i = layout.frozenCount; i < n; i++)
			if (x >= layout.Left(i, scrollX) && x < layout.Right(i, scrollX))
				return i;
		return -1;
	}

	int DividerAt(const DgLayout& layout, int scrollX, int x, int zone) {
		int best = -1, bestDistance = zone + 1;
		for (int i = 0; i < (int)layout.cols.size(); i++) {
			int right = layout.Right(i, scrollX);
			if (i >= layout.frozenCount && right < layout.ScrollLeft())
				continue;   // under the frozen columns
			int distance = std::abs(x - right);
			if (distance <= bestDistance) {   // a tie: the later one, so a narrow column can still be widened
				best = i;
				bestDistance = distance;
			}
		}
		return best;
	}
}

DgHit DgHitTest(const DgLayout& layout, const DgView& view, int x, int y, int dividerZone) {
	DgHit hit;
	if (x < 0 || y < 0 || x >= view.clientWidth || y >= view.clientHeight)
		return hit;
	if (y < layout.header) {
		if (x < layout.rowHeader) {
			hit.flags = DgHitFlag::Corner;
			return hit;
		}
		if (int divider = DividerAt(layout, view.scrollX, x, dividerZone); divider >= 0) {
			hit.flags = DgHitFlag::HeaderDivider;
			hit.col = divider;
			return hit;
		}
		hit.col = ColumnAt(layout, view.scrollX, x);
		hit.flags = hit.col >= 0 ? DgHitFlag::Header : DgHitFlag::Empty;
		return hit;
	}
	int64_t row = view.topRow + (y - layout.header) / layout.rowHeight;
	if (row >= view.rowCount) {
		hit.flags = DgHitFlag::Empty;
		return hit;
	}
	hit.row = row;
	if (x < layout.rowHeader) {
		hit.flags = DgHitFlag::RowHeader;
		return hit;
	}
	hit.col = ColumnAt(layout, view.scrollX, x);
	hit.flags = hit.col >= 0 ? DgHitFlag::Cell : DgHitFlag::Empty;
	return hit;
}

int DgDropIndex(const DgLayout& layout, int scrollX, int x) {
	int n = (int)layout.cols.size();
	for (int i = 0; i < layout.frozenCount; i++)
		if (x < layout.Left(i, 0) + layout.cols[i].width / 2)
			return i;
	if (x < layout.ScrollLeft())
		return layout.frozenCount;
	for (int i = layout.frozenCount; i < n; i++)
		if (x < layout.Left(i, scrollX) + layout.cols[i].width / 2)
			return i;
	return n;
}


DgPos DgPosFromPoint(const DgLayout& layout, const DgView& view, int x, int y) {
	DgPos pos;
	if (view.rowCount > 0) {
		int yy = std::clamp(y, layout.header, std::max(layout.header, view.clientHeight - 1));
		pos.row = std::clamp<int64_t>(view.topRow + (yy - layout.header) / layout.rowHeight, 0, view.rowCount - 1);
	}
	int n = (int)layout.cols.size();
	if (n == 0)
		return pos;
	if (int col = ColumnAt(layout, view.scrollX, x); col >= 0) {
		pos.col = col;
		return pos;
	}
	if (x >= layout.Right(n - 1, view.scrollX) && x >= layout.rowHeader) {
		pos.col = n - 1;
		return pos;
	}
	// left of the columns: the first one in view
	pos.col = 0;
	if (layout.frozenCount == 0)
		for (int i = 0; i < n; i++)
			if (layout.Right(i, view.scrollX) > layout.ScrollLeft()) {
				pos.col = i;
				break;
			}
	return pos;
}

// ---- The caret -----------------------------------------------------------------------------------

DgPos DgNavigate(DgPos pos, DgKey key, bool ctrl, int64_t rowCount, int colCount, int64_t page) {
	if (rowCount <= 0 || colCount <= 0)
		return {};
	int64_t lastRow = rowCount - 1;
	int lastCol = colCount - 1;
	page = std::max<int64_t>(1, page);
	switch (key) {
		case DgKey::Left:     pos.col = ctrl ? 0 : pos.col - 1; break;
		case DgKey::Right:    pos.col = ctrl ? lastCol : pos.col + 1; break;
		case DgKey::Up:       pos.row = ctrl ? 0 : pos.row - 1; break;
		case DgKey::Down:     pos.row = ctrl ? lastRow : pos.row + 1; break;
		case DgKey::PageUp:   pos.row -= page; break;
		case DgKey::PageDown: pos.row += page; break;
		case DgKey::Home:
			pos.col = 0;
			if (ctrl)
				pos.row = 0;
			break;
		case DgKey::End:
			pos.col = lastCol;
			if (ctrl)
				pos.row = lastRow;
			break;
	}
	pos.row = std::clamp<int64_t>(pos.row, 0, lastRow);
	pos.col = std::clamp(pos.col, 0, lastCol);
	return pos;
}

// ---- Formatting --------------------------------------------------------------------------------

namespace {
	std::wstring Group(const std::wstring& digits, const std::wstring& separator) {
		std::wstring result;
		int n = (int)digits.size();
		for (int i = 0; i < n; i++) {
			if (i > 0 && (n - i) % 3 == 0)
				result += separator;
			result += digits[i];
		}
		return result;
	}

	// days since 1970-01-01 to a date (Howard Hinnant's civil_from_days)
	void CivilFromDays(int64_t z, int& year, int& month, int& day) {
		z += 719468;
		int64_t era = (z >= 0 ? z : z - 146096) / 146097;
		int64_t doe = z - era * 146097;
		int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
		int64_t mp = (5 * doy + 2) / 153;
		day = (int)(doy - (153 * mp + 2) / 5 + 1);
		month = (int)(mp < 10 ? mp + 3 : mp - 9);
		year = (int)(yoe + era * 400 + (month <= 2 ? 1 : 0));
	}
}

std::wstring DgFormatInt(int64_t value, bool group, const DgNumberFormat& nf) {
	uint64_t magnitude = value < 0 ? 0 - (uint64_t)value : (uint64_t)value;
	std::wstring digits = std::to_wstring(magnitude);
	if (group)
		digits = Group(digits, nf.thousands);
	return value < 0 ? L"-" + digits : digits;
}

std::wstring DgFormatFloat(double value, int decimals, bool group, const DgNumberFormat& nf) {
	if (!std::isfinite(value))
		return std::isnan(value) ? L"NaN" : value < 0 ? L"-\x221E" : L"\x221E";
	decimals = std::clamp(decimals, 0, 15);
	wchar_t buffer[400];
	swprintf_s(buffer, L"%.*f", decimals, std::fabs(value));
	std::wstring text = buffer;
	auto point = text.find(L'.');
	std::wstring whole = text.substr(0, point);
	std::wstring fraction = point == std::wstring::npos ? L"" : text.substr(point + 1);
	if (group)
		whole = Group(whole, nf.thousands);
	std::wstring result = whole;
	if (!fraction.empty())
		result += nf.decimal + fraction;
	bool zero = text.find_first_not_of(L"0.") == std::wstring::npos;
	return value < 0 && !zero ? L"-" + result : result;
}

std::wstring DgFormatDateTime(int64_t ticks) {
	constexpr int64_t TicksPerSecond = 10'000'000;
	constexpr int64_t SecondsPerDay = 86400;
	constexpr int64_t DaysFrom1601To1970 = 134774;
	int64_t seconds = ticks / TicksPerSecond;
	int64_t days = seconds / SecondsPerDay;
	int64_t secondOfDay = seconds % SecondsPerDay;
	if (secondOfDay < 0) {
		secondOfDay += SecondsPerDay;
		days--;
	}
	int year, month, day;
	CivilFromDays(days - DaysFrom1601To1970, year, month, day);
	wchar_t buffer[64];
	swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", year, month, day, (int)(secondOfDay / 3600),
		(int)(secondOfDay / 60 % 60), (int)(secondOfDay % 60));
	return buffer;
}

double DgProgress(const DgValue& value) {
	switch (value.type) {
		case DgType::Int:   return std::clamp(value.i / 100.0, 0.0, 1.0);
		case DgType::Float: return std::isnan(value.d) ? -1 : std::clamp(value.d, 0.0, 1.0);
	}
	return -1;
}

bool DgChecked(const DgValue& value) {
	return (value.type == DgType::Bool || value.type == DgType::Int) && value.i != 0;
}

std::wstring DgFormat(const DgValue& value, const DgColumn& column, const DgNumberFormat& nf) {
	if (value.type == DgType::Empty)
		return {};
	bool group = (column.flags & DgColFlag::GroupDigits) != 0;
	switch (column.type) {
		case DgColType::Enum:
			if (value.type == DgType::Int && value.i >= 0 && value.i < (int64_t)column.enumItems.size())
				return column.enumItems[(size_t)value.i];
			break;
		case DgColType::Progress:
			if (double p = DgProgress(value); p >= 0)
				return std::to_wstring((int)std::lround(p * 100)) + L"%";
			break;
		case DgColType::Bool:
			if (value.type == DgType::Int)
				return DgChecked(value) ? L"true" : L"false";
			break;
		case DgColType::Float:
			if (value.type == DgType::Int)
				return DgFormatFloat((double)value.i, column.decimals, group, nf);
			break;
		case DgColType::Integer:
			if (value.type == DgType::Float && std::isfinite(value.d) && std::fabs(value.d) < 9.2e18)
				return DgFormatInt(std::llround(value.d), group, nf);
			break;
	}
	switch (value.type) {
		case DgType::Text:     return value.s;
		case DgType::Int:      return DgFormatInt(value.i, group, nf);
		case DgType::Float:    return DgFormatFloat(value.d, column.decimals, group, nf);
		case DgType::Bool:     return value.i ? L"true" : L"false";
		case DgType::DateTime: return DgFormatDateTime(value.i);
	}
	return {};
}

int DgEffectiveAlign(const DgColumn& column) {
	if (column.align == DgAlign::Left || column.align == DgAlign::Center || column.align == DgAlign::Right)
		return column.align;
	switch (column.type) {
		case DgColType::Integer:
		case DgColType::Float:
			return DgAlign::Right;
		case DgColType::Bool:
		case DgColType::Progress:
			return DgAlign::Center;
	}
	return DgAlign::Left;
}

// ---- The store -----------------------------------------------------------------------------------

void DgStore::Resize(int64_t count) {
	m_Rows.resize((size_t)std::max<int64_t>(0, count));
}

int64_t DgStore::Insert(int64_t at, int64_t count) {
	if (at < 0 || at > Count())
		at = Count();
	if (count > 0)
		m_Rows.insert(m_Rows.begin() + (ptrdiff_t)at, (size_t)count, std::vector<DgCell>());
	return at;
}

bool DgStore::Erase(int64_t at, int64_t count) {
	if (at < 0 || count < 0 || at + count > Count())
		return false;
	m_Rows.erase(m_Rows.begin() + (ptrdiff_t)at, m_Rows.begin() + (ptrdiff_t)(at + count));
	return true;
}

bool DgStore::Set(int64_t row, int slot, DgCell cell) {
	if (row < 0 || row >= Count() || slot < 0)
		return false;
	auto& cells = m_Rows[(size_t)row];
	if ((int)cells.size() <= slot)
		cells.resize((size_t)slot + 1);
	cells[(size_t)slot] = std::move(cell);
	return true;
}

const DgCell* DgStore::Get(int64_t row, int slot) const {
	if (row < 0 || row >= Count() || slot < 0)
		return nullptr;
	auto& cells = m_Rows[(size_t)row];
	return slot < (int)cells.size() ? &cells[(size_t)slot] : nullptr;
}

void DgStore::ClearSlot(int slot) {
	for (auto& cells : m_Rows)
		if (slot >= 0 && slot < (int)cells.size())
			cells[(size_t)slot] = {};
}

// ---- A virtual grid's cache ------------------------------------------------------------------------

const DgCell* DgCellCache::Find(int64_t row, int id) const {
	auto it = m_Rows.find(row);
	if (it == m_Rows.end())
		return nullptr;
	for (auto& [cid, cell] : it->second)
		if (cid == id)
			return &cell;
	return nullptr;
}

void DgCellCache::Put(int64_t row, int id, DgCell cell) {
	auto& cells = m_Rows[row];
	for (auto& [cid, c] : cells)
		if (cid == id) {
			c = std::move(cell);
			return;
		}
	cells.emplace_back(id, std::move(cell));
}

void DgCellCache::Drop(int64_t first, int64_t last) {
	std::erase_if(m_Rows, [&](auto& entry) { return entry.first >= first && entry.first <= last; });
}

void DgCellCache::Trim(int64_t first, int64_t last) {
	std::erase_if(m_Rows, [&](auto& entry) { return entry.first < first || entry.first > last; });
}

// ---- Rows in view and in the data ------------------------------------------------------------------

void DgRowMap::Reset(int64_t count) {
	m_Identity = true;
	m_Count = std::max<int64_t>(0, count);
	m_Order.clear();
	m_Inverse.clear();
}

void DgRowMap::SetOrder(std::vector<int64_t> order, int64_t dataCount) {
	m_Identity = false;
	m_Count = dataCount;
	m_Order = std::move(order);
	m_Inverse.assign((size_t)std::max<int64_t>(0, dataCount), -1);
	for (size_t i = 0; i < m_Order.size(); i++)
		if (m_Order[i] >= 0 && m_Order[i] < dataCount)
			m_Inverse[(size_t)m_Order[i]] = (int64_t)i;
}

int64_t DgRowMap::ToData(int64_t view) const {
	if (view < 0 || view >= Count())
		return -1;
	return m_Identity ? view : m_Order[(size_t)view];
}

int64_t DgRowMap::ToView(int64_t data) const {
	if (data < 0)
		return -1;
	if (m_Identity)
		return data < m_Count ? data : -1;
	return data < (int64_t)m_Inverse.size() ? m_Inverse[(size_t)data] : -1;
}

// ---- Selection ----------------------------------------------------------------------------------

DgRange DgRange::Span(DgPos a, DgPos b) {
	return { std::min(a.row, b.row), std::max(a.row, b.row), std::min(a.col, b.col), std::max(a.col, b.col) };
}

DgRange DgSelection::Single(DgPos pos, int colCount, bool wholeRows) const {
	if ((m_Mode == DgSelMode::Rows || wholeRows) && m_Mode != DgSelMode::Single)
		return { pos.row, pos.row, 0, std::max(0, colCount - 1) };
	return { pos.row, pos.row, pos.col, pos.col };
}

void DgSelection::SetMode(DgSelMode mode, DgPos at, int colCount) {
	m_Mode = mode;
	Reset(at, colCount);
}

void DgSelection::Reset(DgPos pos, int colCount, bool wholeRows) {
	m_Ranges = { Single(pos, colCount, wholeRows) };
	m_Anchor = pos;
}

void DgSelection::Extend(DgPos pos, int colCount, bool wholeRows) {
	if (m_Mode == DgSelMode::Single || m_Ranges.empty()) {
		Reset(pos, colCount, wholeRows);
		return;
	}
	auto range = DgRange::Span(m_Anchor, pos);
	if (m_Mode == DgSelMode::Rows || wholeRows) {
		range.left = 0;
		range.right = std::max(0, colCount - 1);
	}
	m_Ranges.back() = range;
}

void DgSelection::Add(DgPos pos, int colCount, bool wholeRows) {
	if (m_Mode == DgSelMode::Single) {
		Reset(pos, colCount);
		return;
	}
	m_Ranges.push_back(Single(pos, colCount, wholeRows));
	m_Anchor = pos;
}

void DgSelection::Toggle(DgPos pos, int colCount) {
	if (m_Mode != DgSelMode::Rows || !RowTouched(pos.row)) {
		Add(pos, colCount);
		return;
	}
	std::vector<DgRange> kept;
	for (auto& r : m_Ranges) {
		if (pos.row < r.top || pos.row > r.bottom) {
			kept.push_back(r);
			continue;
		}
		if (r.top < pos.row)
			kept.push_back({ r.top, pos.row - 1, r.left, r.right });
		if (pos.row < r.bottom)
			kept.push_back({ pos.row + 1, r.bottom, r.left, r.right });
	}
	m_Ranges = std::move(kept);
	m_Anchor = pos;
}

void DgSelection::SelectAll(int64_t rowCount, int colCount) {
	m_Ranges.clear();
	if (rowCount > 0 && colCount > 0)
		m_Ranges.push_back({ 0, rowCount - 1, 0, colCount - 1 });
}

void DgSelection::Clamp(int64_t rowCount, int colCount) {
	bool had = !m_Ranges.empty();
	m_Anchor.row = std::clamp<int64_t>(m_Anchor.row, 0, std::max<int64_t>(0, rowCount - 1));
	m_Anchor.col = std::clamp(m_Anchor.col, 0, std::max(0, colCount - 1));
	if (rowCount <= 0 || colCount <= 0) {
		m_Ranges.clear();
		return;
	}
	std::vector<DgRange> kept;
	for (auto r : m_Ranges) {
		if (r.top >= rowCount || r.left >= colCount)
			continue;
		r.bottom = std::min(r.bottom, rowCount - 1);
		r.right = m_Mode == DgSelMode::Rows ? colCount - 1 : std::min(r.right, colCount - 1);
		if (m_Mode == DgSelMode::Rows)
			r.left = 0;
		kept.push_back(r);
	}
	m_Ranges = std::move(kept);
	if (had && m_Ranges.empty())
		Reset(m_Anchor, colCount);
}

bool DgSelection::Contains(int64_t row, int col) const {
	for (auto& r : m_Ranges)
		if (r.Contains(row, col))
			return true;
	return false;
}

bool DgSelection::RowTouched(int64_t row) const {
	for (auto& r : m_Ranges)
		if (row >= r.top && row <= r.bottom)
			return true;
	return false;
}

bool DgSelection::ColTouched(int col) const {
	for (auto& r : m_Ranges)
		if (col >= r.left && col <= r.right)
			return true;
	return false;
}

std::vector<std::pair<int64_t, int64_t>> DgSelection::Rows() const {
	std::vector<std::pair<int64_t, int64_t>> spans;
	for (auto& r : m_Ranges)
		spans.emplace_back(r.top, r.bottom);
	std::sort(spans.begin(), spans.end());
	std::vector<std::pair<int64_t, int64_t>> merged;
	for (auto& s : spans) {
		if (!merged.empty() && s.first <= merged.back().second + 1)
			merged.back().second = std::max(merged.back().second, s.second);
		else
			merged.push_back(s);
	}
	return merged;
}

std::vector<int> DgSelection::Cols() const {
	std::vector<int> cols;
	for (auto& r : m_Ranges)
		for (int c = r.left; c <= r.right; c++)
			cols.push_back(c);
	std::sort(cols.begin(), cols.end());
	cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
	return cols;
}

int64_t DgSelection::RowCount() const {
	int64_t n = 0;
	for (auto [first, last] : Rows())
		n += last - first + 1;
	return n;
}

// ---- Sorting ----------------------------------------------------------------------------------------

std::vector<DgSortKey> DgClickSort(const std::vector<DgSortKey>& keys, int id, bool add) {
	if (add) {
		auto result = keys;
		for (auto& k : result)
			if (k.id == id) {
				k.descending = !k.descending;
				return result;
			}
		result.push_back({ id, false });
		return result;
	}
	if (keys.size() == 1 && keys[0].id == id)
		return keys[0].descending ? std::vector<DgSortKey>() : std::vector<DgSortKey>{ { id, true } };
	return { { id, false } };
}

int DgOrdinalCompare(const std::wstring& a, const std::wstring& b) {
	int c = a.compare(b);
	return c < 0 ? -1 : c > 0 ? 1 : 0;
}

int DgCompareValues(const DgValue& a, const DgValue& b, DgTextCompare compare) {
	auto rank = [](int type) {
		switch (type) {
			case DgType::Int:
			case DgType::Float:
			case DgType::Bool:     return 0;
			case DgType::DateTime: return 1;
			case DgType::Text:     return 2;
		}
		return 3;
	};
	int ra = rank(a.type), rb = rank(b.type);
	if (ra != rb)
		return ra < rb ? -1 : 1;
	switch (ra) {
		case 0: {
			if (a.type != DgType::Float && b.type != DgType::Float)
				return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
			double x = a.type == DgType::Float ? a.d : (double)a.i, y = b.type == DgType::Float ? b.d : (double)b.i;
			if (std::isnan(x) || std::isnan(y))   // NaN after the numbers
				return std::isnan(x) == std::isnan(y) ? 0 : std::isnan(x) ? 1 : -1;
			return x < y ? -1 : x > y ? 1 : 0;
		}
		case 1:
			return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
		case 2:
			return (compare ? compare : DgOrdinalCompare)(a.s, b.s);
	}
	return 0;
}

// ---- Copying ----------------------------------------------------------------------------------------

std::wstring DgTsvField(const std::wstring& text) {
	if (text.find_first_of(L"\t\r\n\"") == std::wstring::npos)
		return text;
	std::wstring quoted = L"\"";
	for (wchar_t ch : text) {
		if (ch == L'"')
			quoted += L'"';
		quoted += ch;
	}
	return quoted + L"\"";
}

std::wstring DgBuildTsv(const std::vector<std::vector<std::wstring>>& rows) {
	std::wstring text;
	for (auto& row : rows) {
		for (size_t i = 0; i < row.size(); i++) {
			if (i)
				text += L'\t';
			text += DgTsvField(row[i]);
		}
		text += L"\r\n";
	}
	return text;
}

std::wstring DgHtmlEscape(const std::wstring& text) {
	std::wstring result;
	for (size_t i = 0; i < text.size(); i++) {
		switch (text[i]) {
			case L'&': result += L"&amp;"; break;
			case L'<': result += L"&lt;"; break;
			case L'>': result += L"&gt;"; break;
			case L'"': result += L"&quot;"; break;
			case L'\r':
				if (i + 1 < text.size() && text[i + 1] == L'\n')
					i++;
				result += L"<br>";
				break;
			case L'\n': result += L"<br>"; break;
			default: result += text[i]; break;
		}
	}
	return result;
}

std::wstring DgBuildHtmlTable(const std::vector<std::vector<std::wstring>>& rows, bool headerRow) {
	std::wstring html = L"<table>";
	for (size_t r = 0; r < rows.size(); r++) {
		const wchar_t* tag = headerRow && r == 0 ? L"th" : L"td";
		html += L"<tr>";
		for (auto& cell : rows[r])
			html += std::wstring(L"<") + tag + L">" + DgHtmlEscape(cell) + L"</" + tag + L">";
		html += L"</tr>";
	}
	return html + L"</table>";
}

std::string DgUtf8(const std::wstring& text) {
	std::string out;
	for (size_t i = 0; i < text.size(); i++) {
		uint32_t cp = text[i];
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
			cp = 0x10000 + ((cp - 0xD800) << 10) + (uint32_t)(text[++i] - 0xDC00);
		else if (cp >= 0xD800 && cp <= 0xDFFF)
			cp = 0xFFFD;   // a lone surrogate
		if (cp < 0x80) {
			out += (char)cp;
		}
		else if (cp < 0x800) {
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000) {
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else {
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}
	return out;
}

std::string DgCfHtml(const std::string& fragmentUtf8) {
	const char* header = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\nEndFragment:%010d\r\n";
	const std::string before = "<html><body>\r\n<!--StartFragment-->";
	const std::string after = "<!--EndFragment-->\r\n</body></html>";
	char probe[256];
	int headerLength = snprintf(probe, sizeof(probe), header, 0, 0, 0, 0);
	int startFragment = headerLength + (int)before.size();
	int endFragment = startFragment + (int)fragmentUtf8.size();
	int endHtml = endFragment + (int)after.size();
	char buffer[256];
	snprintf(buffer, sizeof(buffer), header, headerLength, endHtml, startFragment, endFragment);
	return buffer + before + fragmentUtf8 + after;
}
