#include "pch.h"
#include "IListView.h"

int IListView::GetSelectedIndex() const {
    int selected = -1;
    GetSelectedCount(&selected);
    if (selected != 1)
        return -1;
    LVITEMINDEX idx;
    return GetNextItem(LVITEMINDEX{ -1, -1 }, LVNI_SELECTED, &idx) == S_OK ? idx.iItem : -1;
}

int IListView::GetItemCount() const {
    int count;
    return S_OK == GetItemCount(&count) ? count : -1;
}

int IListView::GetTopIndex() const {
    int top;
    return GetTopIndex(&top) == S_OK ? top : -1;
}

int IListView::GetCountPerPage() const {
    int count;
    return GetCountPerPage(&count) == S_OK ? count : -1;
}

bool IListView::IsItemVisible(int index) const {
    BOOL visible{ false };
    IsItemVisible(LVITEMINDEX{ index, -1 }, &visible);
    return visible;
}

CString IListView::GetItemText(int row, int column) const {
    CString text;
    GetItemText(row, column, text.GetBufferSetLength(256), 256);
    text.FreeExtra();
    return text;
}

ULONG IListView::GetItemState(int row, ULONG mask) const {
    ULONG state;
    return S_OK == GetItemState(row, 0, mask, &state) ? state : 0;
}

int IListView::GetNextItem(int item, ULONG mask) const {
    LVITEMINDEX next{ -1, -1 };
    GetNextItem(LVITEMINDEX{ item, -1 }, mask, &next);
    return next.iItem;
}

int IListView::GetSelectedCount() const {
    int count;
    return S_OK == GetSelectedCount(&count) ? count : 0;
}

UINT IListView::GetHoverTime() const {
    UINT hover;
    return S_OK == GetHoverTime(&hover) ? hover : 0;
}

int IListView::GetColumnWidth(int columnIndex) const {
    int width;
    return S_OK == GetColumnWidth(columnIndex, &width) ? width : -1;
}

HWND IListView::GetHeader() const {
    HWND h;
    return S_OK == GetHeaderControl(&h) ? h : nullptr;
}
