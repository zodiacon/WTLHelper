#include "pch.h"
#include "Selection.h"

void Selection::SetSimple(int64_t offset, int64_t len) {
	m_Type = SelectionType::Simple;
	m_Offset = offset;
	m_Length = len;
}

void Selection::SetBox(int64_t offset, int bytesPerLine, int width, int height) {
	m_Type = SelectionType::Box;
	m_Offset = offset;
	m_Width = width;
	m_Height = height;
	m_BytesPerLine = bytesPerLine;
}

void Selection::SetAnchor(int64_t offset) {
	m_Anchor = offset;
}

int64_t Selection::GetOffset() const {
	return m_Offset;
}

int64_t Selection::GetAnchor() const {
	return m_Anchor;
}

bool Selection::IsSelected(int64_t offset) const {
	switch (m_Type) {
		case SelectionType::Simple:
			return offset >= m_Offset && offset < m_Offset + m_Length;

		case SelectionType::Box:
			if (offset < m_Offset)
				return false;
			return (offset - m_Offset) % m_BytesPerLine < m_Width && (offset - m_Offset) / m_BytesPerLine < m_Height;
	}
	ATLASSERT(false);
	return false;
}

bool Selection::IsEmpty() const {
	return m_Offset < 0;
}

SelectionType Selection::GetSelectionType() const {
	return m_Type;
}

int64_t Selection::GetLength() const {
	return m_Length;
}

void Selection::Clear() {
	m_Type = SelectionType::Simple;
	m_Offset = m_Anchor = -1;
	m_Length = 0;
}
