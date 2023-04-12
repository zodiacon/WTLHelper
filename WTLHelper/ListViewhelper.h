#pragma once

struct IListView;
struct ColumnsState;

struct ListViewHelper abstract final {
	static bool SaveAll(PCWSTR path, CListViewCtrl& lv, PCWSTR separator = L",", bool includeHeaders = true);
	static bool SaveAllToKey(CRegKey& key, CListViewCtrl& lv, bool includeHeaders = true);
	static CString GetRowAsString(CListViewCtrl const& lv, int row, PCWSTR separator = L"\t");
	static CString GetSelectedRowsAsString(CListViewCtrl const& lv, PCWSTR separator = L"\t", PCWSTR cr = L"\r\n");
	static int FindItem(CListViewCtrl const& lv, PCWSTR text, bool partial);
	static int SearchItem(CListViewCtrl const& lv, PCWSTR text, bool down, bool caseSenstive);

	static int FindRow(CListViewCtrl const& lv, PCWSTR rowText, int start = -1);
	static int FindRow(CListViewCtrl const& lv, int colStart, int count, PCWSTR rowText, int start = -1);
	static IListView* GetIListView(HWND hListView);
	static CString GetRowColumnsAsString(CListViewCtrl const& lv, int row, int start, int count, PCWSTR separator = L"\t");
	static CString GetAllRowsAsString(CListViewCtrl const& lv, PCWSTR separator = L"\t", PCWSTR cr = L"\r\n");
	static bool WriteColumnsState(ColumnsState const& state, IStream* stm);
	static bool ReadColumnsState(ColumnsState& state, IStream* stm);
};

struct SelectedItemsView : std::ranges::view_interface<SelectedItemsView> {
	struct Iterator {
		Iterator(SelectedItemsView const& view, bool end = false) : _view(view), _end(end) {
			if(!end)
				_index = view._lv.GetNextItem(-1, LVNI_SELECTED);
		}
		int operator*() const {
			return _index;
		}
		int operator++(int) {
			auto index = _index;
			_index = _view._lv.GetNextItem(_index, LVNI_SELECTED);
			if (_index == -1)
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
		SelectedItemsView const& _view;
		bool _end;
	};

	SelectedItemsView() = default;
	explicit SelectedItemsView(CListViewCtrl const& lv) : _lv(lv), _count(lv.GetSelectedCount()) {}

	Iterator begin() {
		return Iterator(*this);
	}
	Iterator end() {
		return Iterator(*this, true);
	}

	Iterator const begin() const {
		return Iterator(*this);
	}
	Iterator const end() const {
		return Iterator(*this, true);
	}

	size_t size() const {
		return _count;
	}

private:
	CListViewCtrl const& _lv;
	int _count;
};
