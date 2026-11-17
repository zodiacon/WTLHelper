#include "DataGridModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>

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

// ---- Editing --------------------------------------------------------------------------------------------

namespace {
	std::wstring Trim(const std::wstring& s) {
		auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == 0xA0 || c == 0x202F; };
		size_t a = 0, b = s.size();
		while (a < b && space(s[a]))
			a++;
		while (b > a && space(s[b - 1]))
			b--;
		return s.substr(a, b - a);
	}

	std::wstring Lower(std::wstring s) {
		for (auto& c : s)
			c = (wchar_t)towlower(c);
		return s;
	}

	// without the thousands separators and spaces (some locales group with a no-break space)
	std::wstring Ungrouped(const std::wstring& s, const DgNumberFormat& nf) {
		std::wstring result;
		for (size_t i = 0; i < s.size(); i++) {
			if (!nf.thousands.empty() && nf.thousands != nf.decimal && s.compare(i, nf.thousands.size(), nf.thousands) == 0) {
				i += nf.thousands.size() - 1;
				continue;
			}
			if (s[i] == L' ' || s[i] == 0xA0 || s[i] == 0x202F)
				continue;
			result += s[i];
		}
		return result;
	}

	bool ParseInt(const std::wstring& text, const DgNumberFormat& nf, int64_t& value) {
		auto s = Ungrouped(text, nf);
		size_t i = 0;
		bool negative = false;
		if (i < s.size() && (s[i] == L'-' || s[i] == L'+'))
			negative = s[i++] == L'-';
		if (i == s.size())
			return false;
		uint64_t magnitude = 0;
		const uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
		for (; i < s.size(); i++) {
			if (s[i] < L'0' || s[i] > L'9')
				return false;
			unsigned digit = s[i] - L'0';
			if (magnitude > (limit - digit) / 10)
				return false;   // too big
			magnitude = magnitude * 10 + digit;
		}
		value = negative ? (int64_t)(0 - magnitude) : (int64_t)magnitude;
		return true;
	}

	bool ParseNumber(const std::wstring& text, const DgNumberFormat& nf, double& value) {
		auto s = Ungrouped(text, nf);
		if (!nf.decimal.empty() && nf.decimal != L".") {
			for (size_t at; (at = s.find(nf.decimal)) != std::wstring::npos;)
				s.replace(at, nf.decimal.size(), L".");
		}
		if (s.empty() || s.find_first_not_of(L"0123456789+-.eE") != std::wstring::npos)
			return false;   // no "inf", "nan" or hex
		wchar_t* end = nullptr;
		value = wcstod(s.c_str(), &end);
		return end == s.c_str() + s.size() && std::isfinite(value);
	}

	// days since 1970-01-01 (Howard Hinnant's days_from_civil)
	int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
		y -= m <= 2;
		int64_t era = (y >= 0 ? y : y - 399) / 400;
		unsigned yoe = (unsigned)(y - era * 400);
		unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
		unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		return era * 146097 + (int64_t)doe - 719468;
	}

	std::wstring Shortest(double value, const DgNumberFormat& nf) {
		// the fewest digits that read back as the same number
		wchar_t buffer[64];
		for (int digits = 15; digits <= 17; digits++) {
			swprintf_s(buffer, L"%.*g", digits, value);
			if (wcstod(buffer, nullptr) == value)
				break;
		}
		std::wstring s = buffer;
		if (auto at = s.find(L'.'); at != std::wstring::npos)
			s.replace(at, 1, nf.decimal);
		return s;
	}
}

bool DgParseIsoDateTime(const std::wstring& text, int64_t& ticks) {
	auto s = Trim(text);
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
	auto digits = [&](size_t at, size_t count, int& out) {
		if (at + count > s.size())
			return false;
		out = 0;
		for (size_t i = at; i < at + count; i++) {
			if (s[i] < L'0' || s[i] > L'9')
				return false;
			out = out * 10 + (s[i] - L'0');
		}
		return true;
	};
	if (!digits(0, 4, y) || s.size() < 10 || s[4] != L'-' || !digits(5, 2, mo) || s[7] != L'-' || !digits(8, 2, d))
		return false;
	if (s.size() > 10) {
		if ((s[10] != L' ' && s[10] != L'T') || !digits(11, 2, h) || s.size() < 16 || s[13] != L':' || !digits(14, 2, mi))
			return false;
		if (s.size() > 16 && (s[16] != L':' || !digits(17, 2, sec) || s.size() != 19))
			return false;
	}
	static const int monthDays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
	if (y < 1601 || mo < 1 || mo > 12 || d < 1 || d > monthDays[mo - 1] || (mo == 2 && d == 29 && !leap) || h > 23 || mi > 59 || sec > 59)
		return false;
	constexpr int64_t DaysFrom1601To1970 = 134774;
	int64_t days = DaysFromCivil(y, (unsigned)mo, (unsigned)d) + DaysFrom1601To1970;
	ticks = ((days * 86400) + h * 3600 + mi * 60 + sec) * 10'000'000LL;
	return true;
}

DgParsed DgParseValue(const std::wstring& text, const DgColumn& column, const DgNumberFormat& nf, DgDateParser parseDate) {
	DgParsed r;
	auto s = Trim(text);
	if (s.empty()) {
		r.ok = true;
		return r;
	}
	int64_t i = 0;
	double f = 0;
	switch (column.type) {
		case DgColType::Integer:
			if ((r.ok = ParseInt(s, nf, i)))
				r.value = DgValue::Int(i);
			else
				r.error = L"Enter a whole number.";
			break;
		case DgColType::Float:
			if ((r.ok = ParseNumber(s, nf, f)))
				r.value = DgValue::Float(f);
			else
				r.error = L"Enter a number.";
			break;
		case DgColType::Bool: {
			auto l = Lower(s);
			if (l == L"true" || l == L"yes" || l == L"1" || l == L"x" || l == L"on")
				r.value = DgValue::Bool(true);
			else if (l == L"false" || l == L"no" || l == L"0" || l == L"off")
				r.value = DgValue::Bool(false);
			else
				r.error = L"Enter true or false.";
			r.ok = r.error.empty();
			break;
		}
		case DgColType::DateTime:
			if (DgParseIsoDateTime(s, i) || (parseDate && parseDate(s, i))) {
				r.ok = true;
				r.value = DgValue::DateTime(i);
			}
			else {
				r.error = L"Enter a date, as 2026-11-17 13:05.";
			}
			break;
		case DgColType::Enum: {
			auto l = Lower(s);
			for (size_t k = 0; k < column.enumItems.size(); k++)
				if (Lower(column.enumItems[k]) == l) {
					r.ok = true;
					r.value = DgValue::Int((int64_t)k);
					return r;
				}
			if (ParseInt(s, nf, i) && i >= 0 && i < (int64_t)column.enumItems.size()) {
				r.ok = true;
				r.value = DgValue::Int(i);
				return r;
			}
			r.error = L"Enter one of: ";
			for (size_t k = 0; k < column.enumItems.size(); k++)
				r.error += (k ? L", " : L"") + column.enumItems[k];
			r.error += L".";
			break;
		}
		case DgColType::Progress: {
			bool percent = s.back() == L'%';
			auto number = percent ? Trim(s.substr(0, s.size() - 1)) : s;
			if (!percent && ParseInt(number, nf, i) && i >= 0 && i <= 100) {
				r.ok = true;
				r.value = DgValue::Int(i);
			}
			else if (ParseNumber(number, nf, f) && (percent ? f >= 0 && f <= 100 : f >= 0 && f <= 1)) {
				r.ok = true;
				r.value = percent ? DgValue::Int(std::llround(f)) : DgValue::Float(f);
			}
			else {
				r.error = L"Enter a percentage from 0 to 100, or a fraction from 0 to 1.";
			}
			break;
		}
		default:   // text, links, image and text: as typed
			r.ok = true;
			r.value = DgValue::Text(text);
			break;
	}
	return r;
}

std::wstring DgEditText(const DgValue& value, const DgColumn& column, const DgNumberFormat& nf) {
	switch (value.type) {
		case DgType::Int:
			if (column.type == DgColType::Enum)
				return DgFormat(value, column, nf);
			if (column.type == DgColType::Progress)
				return DgFormatInt(value.i, false, nf) + L"%";
			if (column.type == DgColType::Bool)
				return value.i ? L"true" : L"false";
			return DgFormatInt(value.i, false, nf);
		case DgType::Float:
			if (column.type == DgColType::Integer && std::isfinite(value.d) && std::fabs(value.d) < 9.2e18)
				return DgFormatInt(std::llround(value.d), false, nf);
			return std::isfinite(value.d) ? Shortest(value.d, nf) : DgFormat(value, column, nf);
		case DgType::DateTime: {
			auto s = DgFormatDateTime(value.i);
			return s.size() == 19 && s.compare(16, 3, L":00") == 0 ? s.substr(0, 16) : s;
		}
	}
	return DgFormat(value, column, nf);
}

std::vector<std::vector<std::wstring>> DgParseTsv(const std::wstring& text) {
	std::vector<std::vector<std::wstring>> rows;
	std::vector<std::wstring> row;
	std::wstring field;
	bool quoted = false, atStart = true;
	for (size_t i = 0; i < text.size(); i++) {
		wchar_t c = text[i];
		if (quoted) {
			if (c == L'"') {
				if (i + 1 < text.size() && text[i + 1] == L'"') {
					field += L'"';
					i++;
				}
				else {
					quoted = false;
				}
			}
			else {
				field += c;
			}
			continue;
		}
		if (c == L'"' && atStart) {
			quoted = true;
			atStart = false;
			continue;
		}
		if (c == L'\t') {
			row.push_back(std::move(field));
			field.clear();
			atStart = true;
			continue;
		}
		if (c == L'\r' || c == L'\n') {
			if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
				i++;
			row.push_back(std::move(field));
			field.clear();
			rows.push_back(std::move(row));
			row.clear();
			atStart = true;
			continue;
		}
		field += c;
		atStart = false;
	}
	if (!field.empty() || !row.empty() || (!atStart && !text.empty())) {
		row.push_back(std::move(field));
		rows.push_back(std::move(row));
	}
	return rows;
}

std::vector<DgPasteCell> DgPasteTargets(const std::vector<std::vector<std::wstring>>& fields, const DgSelection& selection,
	int64_t rowCount, int colCount, size_t max) {
	std::vector<DgPasteCell> cells;
	if (fields.empty() || selection.Empty() || rowCount <= 0 || colCount <= 0)
		return cells;
	bool one = fields.size() == 1 && fields[0].size() == 1;
	auto& ranges = selection.Ranges();
	bool many = ranges.size() > 1 || ranges[0].top != ranges[0].bottom || ranges[0].left != ranges[0].right;
	if (one && many) {   // fill the selection
		auto cols = selection.Cols();
		for (auto [first, last] : selection.Rows())
			for (int64_t r = first; r <= last && r < rowCount; r++)
				for (int c : cols) {
					if (c >= colCount || !selection.Contains(r, c))
						continue;
					if (cells.size() >= max)
						return cells;
					cells.push_back({ r, c, &fields[0][0] });
				}
		return cells;
	}
	auto& origin = ranges.back();
	for (size_t r = 0; r < fields.size(); r++)
		for (size_t c = 0; c < fields[r].size(); c++) {
			int64_t row = origin.top + (int64_t)r;
			int col = origin.left + (int)c;
			if (row >= rowCount || col >= colCount)
				continue;
			if (cells.size() >= max)
				return cells;
			cells.push_back({ row, col, &fields[r][c] });
		}
	return cells;
}

DgEditAction DgEditKey(unsigned key, bool shift, bool typed) {
	enum : unsigned { Tab = 0x09, Return = 0x0D, Escape = 0x1B, Left = 0x25, Up = 0x26, Right = 0x27, Down = 0x28 };
	switch (key) {
		case Escape: return DgEditAction::Cancel;
		case Return: return shift ? DgEditAction::CommitUp : DgEditAction::CommitDown;
		case Tab:    return shift ? DgEditAction::CommitLeft : DgEditAction::CommitRight;
		case Up:     return typed ? DgEditAction::CommitUp : DgEditAction::None;
		case Down:   return typed ? DgEditAction::CommitDown : DgEditAction::None;
		case Left:   return typed ? DgEditAction::CommitLeft : DgEditAction::None;
		case Right:  return typed ? DgEditAction::CommitRight : DgEditAction::None;
	}
	return DgEditAction::None;
}

// ---- Undo --------------------------------------------------------------------------------------------------

void DgUndoStack::Push(DgEditStep step) {
	m_Steps.resize(m_Done);   // what was undone can't be redone after a new step
	m_Steps.push_back(std::move(step));
	if (m_Steps.size() > m_Limit)
		m_Steps.erase(m_Steps.begin());
	m_Done = m_Steps.size();
}

const DgEditStep* DgUndoStack::Undo() {
	return CanUndo() ? &m_Steps[--m_Done] : nullptr;
}

const DgEditStep* DgUndoStack::Redo() {
	return CanRedo() ? &m_Steps[m_Done++] : nullptr;
}
