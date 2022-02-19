#pragma once

struct IListView;

struct ListViewHelper {
	static bool SaveAll(PCWSTR path, CListViewCtrl& lv, bool includeHeaders = true);
	static bool SaveAllToKey(CRegKey& key, CListViewCtrl& lv, bool includeHeaders = true);
	static CString GetRowAsString(CListViewCtrl& lv, int row, PCWSTR separator = L"\t");
	static CString GetSelectedRowsAsString(CListViewCtrl& lv, PCWSTR separator = L"\t");
	static int FindItem(CListViewCtrl& lv, PCWSTR text, bool partial);
	IListView* GetIListView(HWND hListView);
};

struct SelectedItemsView : std::ranges::view_interface<SelectedItemsView> {
	struct Iterator {
		Iterator(SelectedItemsView& view, bool end = false) : _view(view), _end(end) {
			if(!end)
				_index = view._lv.GetNextItem(-1, LVNI_SELECTED);
		}
		int operator*() const {
			return _index;
		}
		int operator++(int) {
			auto index = _index;
			_index = _view._lv.GetNextItem(_index, LVNI_SELECTED);
			if (_index = -1)
				_end = true;
			return index;
		}
		int operator++() {
			_index = _view._lv.GetNextItem(_index, LVNI_SELECTED);
			if (_index == -1)
				_end = true;
			return _index;
		}
		bool operator==(Iterator other) const {
			if (_end && other._end)
				return true;
			return false;
		}
		bool operator!=(Iterator other) const {
			return !(*this == other);
		}

		int _index{ -1 };
		SelectedItemsView& _view;
		bool _end;
	};

	SelectedItemsView() = default;
	explicit SelectedItemsView(CListViewCtrl& lv) : _lv(lv), _count(lv.GetSelectedCount()) {}

	Iterator begin() {
		return Iterator(*this);
	}
	Iterator end() {
		return Iterator(*this, true);
	}

	size_t size() const {
		return _count;
	}

private:
	CListViewCtrl& _lv;
	int _count;
};
