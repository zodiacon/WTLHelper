#include "CellGraphs.h"
#include "CellGraphsModel.h"

#include <commctrl.h>
#include <cstdio>
#include <iterator>

#pragma comment(lib, "comctl32.lib")

namespace GraphCtrl {

CellGraphs::Column& CellGraphs::Set(int column, CellKind kind) {
    if ((size_t)column >= m_Columns.size())
        m_Columns.resize(column + 1);
    auto& c = m_Columns[column];
    auto format = std::move(c.Format);   // the format stays the column's
    c = {};
    c.Kind = kind;
    c.Format = std::move(format);
    return c;
}

void CellGraphs::SetSparkline(int column, const SparklineStyle& style) {
    if (column <= 0)
        return;
    auto& c = Set(column, CellKind::Sparkline);
    c.Sparkline = style;
    if (style.Thresholds)
        c.Thresholds.assign(style.Thresholds, style.Thresholds + style.ThresholdCount);
}

void CellGraphs::SetColumns(int column, const ColumnStyle& style) {
    if (column <= 0)
        return;
    auto& c = Set(column, CellKind::Columns);
    c.Columns = style;
    if (style.Thresholds)
        c.Thresholds.assign(style.Thresholds, style.Thresholds + style.ThresholdCount);
}

void CellGraphs::SetMeter(int column, const MeterStyle& style) {
    if (column <= 0)
        return;
    auto& c = Set(column, CellKind::Meter);
    c.Meter = style;
    if (style.Thresholds)
        c.Thresholds.assign(style.Thresholds, style.Thresholds + style.ThresholdCount);
}

void CellGraphs::SetStacked(int column, const StackedStyle& style) {
    if (column <= 0)
        return;
    auto& c = Set(column, CellKind::Stacked);
    c.Stacked = style;
    if (style.Colors)
        c.Colors.assign(style.Colors, style.Colors + style.ColorCount);
}

void CellGraphs::RemoveColumn(int column) {
    if (column > 0 && (size_t)column < m_Columns.size())
        m_Columns[column] = {};
}

void CellGraphs::SetFormat(int column, ValueFormatFn format) {
    if (column <= 0)
        return;
    if ((size_t)column >= m_Columns.size())
        m_Columns.resize(column + 1);
    m_Columns[column].Format = std::move(format);
}

CellKind CellGraphs::GetKind(int column) const {
    return column > 0 && (size_t)column < m_Columns.size() ? m_Columns[column].Kind : CellKind::None;
}

LRESULT CellGraphs::CustomDraw(NMLVCUSTOMDRAW* cd, LRESULT result) {
    return CustomDraw(&cd->nmcd, cd->iSubItem, cd->nmcd.dwItemSpec, result);
}

LRESULT CellGraphs::CustomDraw(NMCUSTOMDRAW* cd, int subItem, UINT_PTR item, LRESULT result) {
    if (m_Columns.empty() || (result & CDRF_SKIPDEFAULT))
        return result;
    switch (cd->dwDrawStage) {
        case CDDS_PREPAINT:
            return result | CDRF_NOTIFYITEMDRAW;

        case CDDS_ITEMPREPAINT:
            return result | CDRF_NOTIFYSUBITEMDRAW;

        // The list draws the cell's background and text between the two: what it changed, less the text, is the
        // background the graph goes on (Cells::CellBackground).
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
            if (GetKind(subItem) == CellKind::None)
                return result;
            if (!Cells::ReadCell(cd->hdc, cd->rc, m_Before))
                m_Before.clear();
            m_BeforeRect = cd->rc;
            return result | CDRF_NOTIFYPOSTPAINT;

        case CDDS_ITEMPOSTPAINT | CDDS_SUBITEM:
        {
            if (GetKind(subItem) == CellKind::None)
                return result;
            const RECT& rc = cd->rc;
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            // the part of the cell being painted: a row at the bottom of the list can be cut off, and the pixels
            // below it are nothing the list drew
            RECT clip, visible;
            if (::GetClipBox(cd->hdc, &clip) == ERROR || !::IntersectRect(&visible, &rc, &clip)) {
                m_Before.clear();
                return result;
            }
            int probeY = (visible.top + visible.bottom) / 2 - rc.top;
            bool before = !m_Before.empty() && ::EqualRect(&rc, &m_BeforeRect);
            {
                // the drawing takes the text out in its own copy of the cell; with nothing drawn (no data), it's
                // taken out here
                Cells::BackgroundScope scope(before ? m_Before : std::vector<uint32_t>(), w, h, probeY);
                Draw(m_Columns[subItem], { cd->hdr.hwndFrom, item, cd->lItemlParam, subItem }, cd->hdc, rc);
                if (before && !scope.Used() && Cells::ReadCell(cd->hdc, rc, m_After)) {
                    Cells::CellBackground(m_Before.data(), m_After.data(), w, h, 0, probeY);
                    Cells::WriteCell(cd->hdc, rc, m_After);
                }
            }
            m_Before.clear();
            // a tooltip showing for this cell follows the new data
            if (m_Tip && subItem == m_TipColumn && item == m_TipItem && ::IsWindowVisible(m_Tip))
                ::PostMessage(m_Tip, TTM_UPDATE, 0, 0);
            return result;
        }
    }
    return result;
}

void CellGraphs::Draw(const Column& column, const CellRef& cell, HDC hdc, const RECT& rc) {
    CellData data;
    if (!m_Data || !m_Data(cell, data))
        return;
    UINT dpi = ::GetDpiForWindow(cell.Window);
    if (dpi == 0)
        dpi = 96;
    auto thresholds = column.Thresholds.empty() ? nullptr : column.Thresholds.data();
    switch (column.Kind) {
        case CellKind::Sparkline:
            if (data.Samples) {
                auto style = column.Sparkline;
                style.Thresholds = thresholds;
                style.ThresholdCount = column.Thresholds.size();
                DrawSparkline(hdc, rc, *data.Samples, style, dpi);
            }
            break;

        case CellKind::Columns:
            if (data.Samples) {
                auto style = column.Columns;
                style.Thresholds = thresholds;
                style.ThresholdCount = column.Thresholds.size();
                DrawColumns(hdc, rc, *data.Samples, style, dpi);
            }
            break;

        case CellKind::Meter:
        {
            auto style = column.Meter;
            style.Thresholds = thresholds;
            style.ThresholdCount = column.Thresholds.size();
            DrawMeter(hdc, rc, data.Value, data.Text, style, dpi);
            break;
        }

        case CellKind::Stacked:
        {
            auto style = column.Stacked;
            style.Colors = column.Colors.empty() ? nullptr : column.Colors.data();
            style.ColorCount = column.Colors.size();
            DrawStacked(hdc, rc, data.Values, data.Count, style, dpi);
            break;
        }

        default:
            break;
    }
}


// ---- Tooltips -----------------------------------------------------------------------
//
// One tool over the list, moved to the graph cell under the mouse (an empty rectangle elsewhere), with its text
// asked for (TTN_GETDISPINFO, sent to the list) when it shows. The tooltip control watches the mouse itself
// (TTF_SUBCLASS); the list's subclass only finds the cell.

namespace {

constexpr UINT_PTR k_TipTool = 1;

TTTOOLINFOW ToolInfo(HWND list) {
    TTTOOLINFOW ti{ sizeof(ti) };
    ti.hwnd = list;
    ti.uId = k_TipTool;
    return ti;
}

std::wstring FormatValue(const ValueFormatFn& format, float value) {
    wchar_t buffer[64];
    if (format)
        format(value, buffer, std::size(buffer));
    else
        swprintf_s(buffer, L"%.1f", value);
    return buffer;
}

} // namespace

void CellGraphs::EnableTooltips(HWND list, std::function<UINT_PTR(int row)> rowToItem) {
    DisableTooltips();
    if (!::IsWindow(list))
        return;
    m_Tip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, list, nullptr, nullptr, nullptr);
    if (!m_Tip)
        return;
    m_TipList = list;
    m_RowToItem = std::move(rowToItem);
    m_TipRow = m_TipColumn = -1;
    auto ti = ToolInfo(list);
    ti.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    ::SendMessage(m_Tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    ::SendMessage(m_Tip, TTM_SETMAXTIPWIDTH, 0, ::MulDiv(400, ::GetDpiForWindow(list), 96));
    ::SetWindowSubclass(list, ListProc, (UINT_PTR)this, (DWORD_PTR)this);
}

void CellGraphs::DisableTooltips() {
    if (m_TipList)
        ::RemoveWindowSubclass(m_TipList, ListProc, (UINT_PTR)this);
    if (m_Tip)
        ::DestroyWindow(m_Tip);
    m_Tip = m_TipList = nullptr;
    m_RowToItem = {};
    m_TipRow = m_TipColumn = -1;
}

// the tool follows the graph cell under pt (list client coordinates)
void CellGraphs::TrackTip(POINT pt) {
    LVHITTESTINFO hit{};
    hit.pt = pt;
    int row = -1, column = -1;
    if (ListView_SubItemHitTest(m_TipList, &hit) >= 0 && GetKind(hit.iSubItem) != CellKind::None) {
        row = hit.iItem;
        column = hit.iSubItem;
    }
    if (row == m_TipRow && column == m_TipColumn)
        return;
    m_TipRow = row;
    m_TipColumn = column;
    m_TipItem = row < 0 ? 0 : m_RowToItem ? m_RowToItem(row) : (UINT_PTR)row;
    ::SendMessage(m_Tip, TTM_POP, 0, 0);
    auto ti = ToolInfo(m_TipList);
    if (row >= 0)
        ListView_GetSubItemRect(m_TipList, row, column, LVIR_BOUNDS, &ti.rect);
    ::SendMessage(m_Tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
}

std::wstring CellGraphs::TipText() {
    if (m_TipRow < 0 || GetKind(m_TipColumn) == CellKind::None || !m_Data)
        return {};
    LVITEMW item{ LVIF_PARAM };
    item.iItem = m_TipRow;
    ListView_GetItem(m_TipList, &item);
    CellData data;
    if (!m_Data({ m_TipList, m_TipItem, item.lParam, m_TipColumn }, data))
        return {};
    if (data.Tooltip)
        return data.Tooltip;

    const auto& column = m_Columns[m_TipColumn];
    auto format = [&](float value) { return FormatValue(column.Format, value); };
    switch (column.Kind) {
        case CellKind::Sparkline:
        case CellKind::Columns:
        {
            if (!data.Samples)
                return {};
            size_t slots = column.Kind == CellKind::Sparkline ? column.Sparkline.Slots : column.Columns.Slots;
            auto stats = Cells::Stats(*data.Samples, slots ? slots : data.Samples->Capacity());
            if (stats.Count == 0)
                return L"No samples";
            return L"Last: " + format(stats.Last) + L"\nMinimum: " + format(stats.Min) + L"\nMaximum: " +
                format(stats.Max) + L"\nAverage: " + format(stats.Average);
        }

        case CellKind::Meter:
            if (data.Text)
                return data.Text;
            return IsMissing(data.Value) ? std::wstring() : format(data.Value);

        case CellKind::Stacked:
        {
            std::wstring text;
            for (size_t i = 0; data.Values && i < data.Count; i++) {
                if (i)
                    text += L" / ";
                text += IsMissing(data.Values[i]) ? L"-" : format(data.Values[i]);
            }
            return text;
        }

        default:
            return {};
    }
}

LRESULT CALLBACK CellGraphs::ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto self = (CellGraphs*)data;
    switch (msg) {
        case WM_MOUSEMOVE:
            self->TrackTip({ (short)LOWORD(lParam), (short)HIWORD(lParam) });
            break;

        // what's under the mouse moved
        case WM_MOUSEWHEEL:
        case WM_VSCROLL:
        case WM_HSCROLL:
        case WM_KEYDOWN:
        {
            LRESULT result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
            POINT pt;
            ::GetCursorPos(&pt);
            ::ScreenToClient(hwnd, &pt);
            self->TrackTip(pt);
            return result;
        }

        case WM_NOTIFY:
        {
            auto hdr = (NMHDR*)lParam;
            if (hdr->hwndFrom == self->m_Tip && hdr->code == TTN_GETDISPINFOW) {
                auto info = (NMTTDISPINFOW*)lParam;
                self->m_TipText = self->TipText();
                info->lpszText = self->m_TipText.data();
                return 0;
            }
            break;
        }

        case WM_NCDESTROY:
            self->DisableTooltips();
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace GraphCtrl
