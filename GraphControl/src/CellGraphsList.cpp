#include "CellGraphs.h"
#include "CellGraphsModel.h"

namespace GraphCtrl {

CellGraphs::Column& CellGraphs::Set(int column, CellKind kind) {
    if ((size_t)column >= m_Columns.size())
        m_Columns.resize(column + 1);
    auto& c = m_Columns[column];
    c = {};
    c.Kind = kind;
    return c;
}

void CellGraphs::SetSparkline(int column, const SparklineStyle& style) {
    if (column > 0)
        Set(column, CellKind::Sparkline).Sparkline = style;
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
            if (!m_Before.empty() && ::EqualRect(&rc, &m_BeforeRect) && Cells::ReadCell(cd->hdc, rc, m_After)) {
                Cells::CellBackground(m_Before.data(), m_After.data(), rc.right - rc.left, rc.bottom - rc.top);
                Cells::WriteCell(cd->hdc, rc, m_After);
            }
            m_Before.clear();
            Draw(m_Columns[subItem], { cd->hdr.hwndFrom, item, cd->lItemlParam, subItem }, cd->hdc, rc);
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
            if (data.Samples)
                DrawSparkline(hdc, rc, *data.Samples, column.Sparkline, dpi);
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
            DrawMeter(hdc, rc, data.Value, style, dpi);
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

} // namespace GraphCtrl
