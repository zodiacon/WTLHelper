#pragma once

#include <vector>
#include <algorithm>
#include <functional>

template<typename T>
class SortedFilteredVector {
public:
	SortedFilteredVector(size_t capacity = 16) {
		reserve(capacity);
	}

	SortedFilteredVector& operator=(std::vector<T> const& other) {
		m_items = other;
		size_t count;
		m_indices.resize(count = other.size());
		for (size_t i = 0; i < count; i++)
			m_indices[i] = i;
		return *this;
	}

	void reserve(size_t capacity) {
		m_items.reserve(capacity);
		m_indices.reserve(capacity);
	}

	void clear() {
		m_items.clear();
		m_indices.clear();
	}

	bool empty() const {
		return m_items.empty();
	}

	void push_back(const T& value) {
		m_items.push_back(value);
		if (m_Filter == nullptr || m_Filter(value, m_items.size() - 1))
			m_indices.push_back(m_indices.size());
	}

	void push_back(T&& value) {
		if (m_Filter == nullptr || m_Filter(value, m_items.size() - 1))
			m_indices.push_back(m_indices.size());
		m_items.push_back(std::move(value));
	}

	void shrink_to_fit() {
		m_items.shrink_to_fit();
		m_indices.shrink_to_fit();
	}

	void Remove(size_t index) {
		auto realIndex = m_indices[index];
		m_items.erase(m_items.begin() + realIndex);
		m_indices.erase(m_indices.begin() + index);
		for (size_t i = 0; i < m_indices.size(); i++) {
			if (m_indices[i] >= realIndex)
				m_indices[i]--;
		}
	}

	void ClearSort() {
		int c = 0;
		for (auto& i : m_indices)
			i = c++;
	}

	typename std::vector<T>::const_iterator begin() const {
		return m_items.begin();
	}

	typename std::vector<T>::const_iterator end() const {
		return m_items.end();
	}

	template<typename Iterator>
	void append(Iterator begin, Iterator end) {
		for (auto it = begin; it != end; ++it)
			push_back(std::move(*it));
	}

	template<typename Iterator>
	void insert(size_t at, Iterator begin, Iterator end) {
		//
		// only call after ResetSort and no filter
		//
		size_t count = end - begin;
		m_items.insert(m_items.begin() + at, begin, end);
		std::vector<size_t> indices(count);
		for (size_t c = 0; c < count; ++c) {
			indices[c] = c + at;
		}
		m_indices.insert(m_indices.begin() + at, indices.begin(), indices.end());
		for (size_t c = at + count; c < m_indices.size(); c++)
			m_indices[c] += count;
	}

	void Set(std::vector<T> items) {
		m_items = std::move(items);
		auto count = m_items.size();
		m_indices.clear();
		m_indices.reserve(count);
		for (decltype(count) i = 0; i < count; i++)
			m_indices.push_back(i);
	}

	const T& operator[](size_t index) const {
		return m_items[m_indices[index]];
	}

	T& operator[](size_t index) {
		return m_items[m_indices[index]];
	}

	const T& GetReal(size_t index) const {
		return m_items[index];
	}

	void Sort(std::function<bool(const T& value1, const T& value2)> compare) {
		std::sort(m_indices.begin(), m_indices.end(), [&](size_t i1, size_t i2) {
			return compare(m_items[i1], m_items[i2]);
			});
	}

	void Sort(size_t start, size_t end, std::function<bool(const T& value1, const T& value2)> compare) {
		if (start >= m_indices.size())
			return;

		std::sort(m_indices.begin() + start, end == 0 ? m_indices.end() : (m_indices.begin() + end), [&](size_t i1, size_t i2) {
			return compare(m_items[i1], m_items[i2]);
			});
	}

	size_t size() const {
		return m_indices.size();
	}

	size_t TotalSize() const {
		return m_items.size();
	}

	void Filter(std::function<bool(const T&, size_t)> predicate, bool append = false) {
		m_Filter = predicate;
		if (!append) {
			m_indices.clear();
		}
		auto count = m_items.size();
		if (predicate == nullptr && !append) {
			for (decltype(count) i = 0; i < count; i++)
				m_indices.push_back(i);
		}
		else if (append) {
			std::vector<size_t> indices2(m_indices);
			int j = 0;
			for (decltype(count) i = 0; i < m_indices.size(); i++, j++) {
				if (!predicate(m_items[m_indices[i]], (int)i)) {
					indices2.erase(indices2.begin() + j);
					j--;
				}
			}
			m_indices = std::move(indices2);
		}
		else {
			for (decltype(count) i = 0; i < count; i++)
				if (predicate(m_items[i], int(i)))
					m_indices.push_back(i);
		}
	}

	bool erase(size_t index) {
		if (index >= m_items.size())
			return false;

		m_items.erase(m_items.begin() + m_indices[index]);
		m_indices.erase(m_indices.begin() + index);
		for (; index < m_indices.size(); index++)
			m_indices[index]--;

		return true;
	}

	const std::vector<T>& GetRealAll() const {
		return m_items;
	}

	const std::vector<T> GetItems() const {
		std::vector<T> items;
		items.reserve(size());
		for (size_t i = 0; i < size(); i++)
			items.push_back(m_items[m_indices[i]]);
		return items;
	}

	const std::vector<T> GetAllItems() const {
		return m_items;
	}

private:
	std::vector<T> m_items;
	std::vector<size_t> m_indices;
	std::function<bool(const T&, size_t)> m_Filter;
};

