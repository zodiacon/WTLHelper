#include "DiffViewModel.h"

#include <algorithm>
#include <climits>
#include <cwctype>
#include <unordered_map>

// ---- Lines -------------------------------------------------------------------------------

DfLines::DfLines(std::wstring_view text) {
	m_Text.reserve(text.size());
	size_t i = 0;
	while (i < text.size()) {
		auto end = text.find_first_of(L"\r\n", i);
		if (end == std::wstring_view::npos) {
			m_Text.append(text.substr(i));
			m_Starts.push_back(m_Text.size());
			m_EndsWithNewline = false;
			break;
		}
		m_Text.append(text.substr(i, end - i));
		m_Starts.push_back(m_Text.size());
		i = end + (text[end] == L'\r' && end + 1 < text.size() && text[end + 1] == L'\n' ? 2 : 1);
	}
}

std::wstring DfLines::Join(std::wstring_view eol) const {
	std::wstring text;
	text.reserve(m_Text.size() + size() * eol.size());
	for (size_t i = 0; i < size(); i++) {
		text += (*this)[i];
		if (i + 1 < size() || m_EndsWithNewline)
			text += eol;
	}
	return text;
}

void DfLines::Replace(size_t first, size_t count, const DfLines& from, size_t fromFirst, size_t fromCount) {
	first = std::min(first, size());
	count = std::min(count, size() - first);
	fromFirst = std::min(fromFirst, from.size());
	fromCount = std::min(fromCount, from.size() - fromFirst);

	bool endsWithNewline = m_EndsWithNewline;
	if (first + count == size()) {
		// the last line is the other text's now, or one of ours that had a line break
		endsWithNewline = fromCount > 0 && fromFirst + fromCount == from.size() ? from.m_EndsWithNewline : true;
	}

	std::wstring text;
	std::vector<size_t> starts;
	text.reserve(m_Text.size() + (from.m_Starts[fromFirst + fromCount] - from.m_Starts[fromFirst]));
	starts.reserve(size() - count + fromCount + 1);
	starts.push_back(0);
	auto add = [&](const DfLines& lines, size_t begin, size_t end) {
		for (size_t i = begin; i < end; i++) {
			text += lines[i];
			starts.push_back(text.size());
		}
	};
	add(*this, 0, first);
	add(from, fromFirst, fromFirst + fromCount);
	add(*this, first + count, size());
	m_Text = std::move(text);
	m_Starts = std::move(starts);
	m_EndsWithNewline = empty() ? true : endsWithNewline;
}

// ---- Myers ---------------------------------------------------------------------------------

namespace {
	// The shortest edit script between two sequences of ids (E. Myers, "An O(ND) Difference Algorithm and Its
	// Variations", 1986), in linear space by finding the middle snake and dividing, as GNU diff does. Marks the
	// elements that aren't part of the common subsequence. Past costLimit a range is split at the furthest point
	// either search reached, so very different inputs cost O(N * limit) rather than O(N * D), not quite minimal.
	class DfMyers {
	public:
		DfMyers(const int* a, int n, const int* b, int m, uint8_t* marksA, uint8_t* marksB, int costLimit)
			: m_A(a), m_B(b), m_MarksA(marksA), m_MarksB(marksB) {
			m_Diags.resize(size_t(n) + m + 3);
			m_Fd = m_Diags.data() + m + 1;
			m_BDiags.resize(size_t(n) + m + 3);
			m_Bd = m_BDiags.data() + m + 1;
			if (costLimit <= 0) {
				costLimit = 1;
				for (size_t diags = size_t(n) + m + 3; diags != 0; diags >>= 2)
					costLimit <<= 1;
				costLimit = std::max(costLimit, 1024);
			}
			m_Limit = costLimit;
		}

		void Compare(int xoff, int xlim, int yoff, int ylim) {
			for (;;) {
				while (xoff < xlim && yoff < ylim && m_A[xoff] == m_B[yoff])
					xoff++, yoff++;
				while (xoff < xlim && yoff < ylim && m_A[xlim - 1] == m_B[ylim - 1])
					xlim--, ylim--;
				if (xoff == xlim) {
					std::fill(m_MarksB + yoff, m_MarksB + ylim, uint8_t(1));
					return;
				}
				if (yoff == ylim) {
					std::fill(m_MarksA + xoff, m_MarksA + xlim, uint8_t(1));
					return;
				}
				int xmid, ymid;
				Split(xoff, xlim, yoff, ylim, xmid, ymid);
				if ((xmid == xoff && ymid == yoff) || (xmid == xlim && ymid == ylim)) {
					// no progress (shouldn't happen): give up on this range
					std::fill(m_MarksA + xoff, m_MarksA + xlim, uint8_t(1));
					std::fill(m_MarksB + yoff, m_MarksB + ylim, uint8_t(1));
					return;
				}
				// recurse into the smaller part and loop on the other, so the depth stays logarithmic
				if ((xmid - xoff) + (ymid - yoff) < (xlim - xmid) + (ylim - ymid)) {
					Compare(xoff, xmid, yoff, ymid);
					xoff = xmid, yoff = ymid;
				}
				else {
					Compare(xmid, xlim, ymid, ylim);
					xlim = xmid, ylim = ymid;
				}
			}
		}

	private:
		void Split(int xoff, int xlim, int yoff, int ylim, int& xmid, int& ymid) {
			int* fd = m_Fd;
			int* bd = m_Bd;
			const int dmin = xoff - ylim, dmax = xlim - yoff;
			const int fmid = xoff - yoff, bmid = xlim - ylim;
			int fmin = fmid, fmax = fmid, bmin = bmid, bmax = bmid;
			const bool odd = ((fmid - bmid) & 1) != 0;
			fd[fmid] = xoff;
			bd[bmid] = xlim;

			for (int c = 1;; c++) {
				// a step forward
				if (fmin > dmin)
					fd[--fmin - 1] = -1;
				else
					fmin++;
				if (fmax < dmax)
					fd[++fmax + 1] = -1;
				else
					fmax--;
				for (int d = fmax; d >= fmin; d -= 2) {
					int tlo = fd[d - 1], thi = fd[d + 1];
					int x = tlo >= thi ? tlo + 1 : thi;
					int y = x - d;
					while (x < xlim && y < ylim && m_A[x] == m_B[y])
						x++, y++;
					fd[d] = x;
					if (odd && bmin <= d && d <= bmax && bd[d] <= x) {
						xmid = x, ymid = y;
						return;
					}
				}
				// a step backward
				if (bmin > dmin)
					bd[--bmin - 1] = INT_MAX;
				else
					bmin++;
				if (bmax < dmax)
					bd[++bmax + 1] = INT_MAX;
				else
					bmax--;
				for (int d = bmax; d >= bmin; d -= 2) {
					int tlo = bd[d - 1], thi = bd[d + 1];
					int x = tlo < thi ? tlo : thi - 1;
					int y = x - d;
					while (xoff < x && yoff < y && m_A[x - 1] == m_B[y - 1])
						x--, y--;
					bd[d] = x;
					if (!odd && fmin <= d && d <= fmax && x <= fd[d]) {
						xmid = x, ymid = y;
						return;
					}
				}

				if (c >= m_Limit) {
					// too expensive: take the point furthest from either end
					int fxybest = -1, fxbest = xoff;
					for (int d = fmax; d >= fmin; d -= 2) {
						int x = std::min(fd[d], xlim);
						int y = x - d;
						if (ylim < y)
							x = ylim + d, y = ylim;
						if (fxybest < x + y)
							fxybest = x + y, fxbest = x;
					}
					int bxybest = INT_MAX, bxbest = xlim;
					for (int d = bmax; d >= bmin; d -= 2) {
						int x = std::max(xoff, bd[d]);
						int y = x - d;
						if (y < yoff)
							x = yoff + d, y = yoff;
						if (x + y < bxybest)
							bxybest = x + y, bxbest = x;
					}
					if ((xlim + ylim) - bxybest < fxybest - (xoff + yoff))
						xmid = fxbest, ymid = fxybest - fxbest;
					else
						xmid = bxbest, ymid = bxybest - bxbest;
					return;
				}
			}
		}

		const int* m_A;
		const int* m_B;
		uint8_t* m_MarksA;
		uint8_t* m_MarksB;
		std::vector<int> m_Diags, m_BDiags;
		int* m_Fd;
		int* m_Bd;
		int m_Limit;
	};

	void Diff(const std::vector<int>& a, const std::vector<int>& b, std::vector<uint8_t>& marksA, std::vector<uint8_t>& marksB, int costLimit = 0) {
		marksA.assign(a.size(), 0);
		marksB.assign(b.size(), 0);
		DfMyers myers(a.data(), int(a.size()), b.data(), int(b.size()), marksA.data(), marksB.data(), costLimit);
		myers.Compare(0, int(a.size()), 0, int(b.size()));
	}

	bool IsSpace(wchar_t c) {
		return iswspace(c) != 0;
	}

	bool IsWordChar(wchar_t c) {
		return c == L'_' || iswalnum(c) != 0 || (c >= 0xD800 && c <= 0xDFFF);
	}

	bool IsBlank(std::wstring_view line) {
		return std::all_of(line.begin(), line.end(), IsSpace);
	}

	void Lower(std::wstring& s) {
		if (!s.empty())
			CharLowerBuffW(s.data(), DWORD(s.size()));
	}

	// Ids for strings, equal strings getting equal ids.
	class DfIds {
	public:
		explicit DfIds(size_t expected = 0) {
			m_Ids.reserve(expected);
		}
		int operator()(std::wstring_view key) {
			auto [it, added] = m_Ids.try_emplace(key, int(m_Ids.size()));
			return it->second;
		}
	private:
		std::unordered_map<std::wstring_view, int> m_Ids;
	};

	// The longest increasing subsequence of values (indexes into it).
	std::vector<size_t> Lis(const std::vector<int>& values) {
		std::vector<size_t> tails, prev(values.size(), SIZE_MAX);
		for (size_t i = 0; i < values.size(); i++) {
			auto it = std::lower_bound(tails.begin(), tails.end(), values[i], [&](size_t t, int v) { return values[t] < v; });
			if (it != tails.begin())
				prev[i] = *(it - 1);
			if (it == tails.end())
				tails.push_back(i);
			else
				*it = i;
		}
		std::vector<size_t> result;
		for (size_t i = tails.empty() ? SIZE_MAX : tails.back(); i != SIZE_MAX; i = prev[i])
			result.push_back(i);
		std::reverse(result.begin(), result.end());
		return result;
	}
}

// ---- Lines and words ----------------------------------------------------------------------

std::wstring DfNormalize(std::wstring_view line, const DfOptions& options) {
	std::wstring s;
	switch (options.whitespace) {
		case DfWhitespace::Exact:
			s = line;
			break;
		case DfWhitespace::All:
			s.reserve(line.size());
			for (auto c : line)
				if (!IsSpace(c))
					s += c;
			break;
		case DfWhitespace::Change: {
			s.reserve(line.size());
			bool space = false;
			for (auto c : line) {
				if (IsSpace(c)) {
					space = true;
					continue;
				}
				if (space && !s.empty())
					s += L' ';
				space = false;
				s += c;
			}
			break;
		}
	}
	if (options.ignoreCase)
		Lower(s);
	return s;
}

std::vector<DfToken> DfTokenize(std::wstring_view line) {
	std::vector<DfToken> tokens;
	size_t i = 0;
	while (i < line.size()) {
		size_t start = i;
		bool space = IsSpace(line[i]);
		if (space) {
			while (i < line.size() && IsSpace(line[i]))
				i++;
		}
		else if (IsWordChar(line[i])) {
			while (i < line.size() && IsWordChar(line[i]))
				i++;
		}
		else {
			i++;
		}
		tokens.push_back({ uint32_t(start), uint32_t(i - start), space });
	}
	return tokens;
}

namespace {
	// Two lines' tokens as ids, for diffing. Spaces are left out when the options ignore them, and all equal when
	// only their amount is ignored.
	struct TokenSeq {
		std::vector<DfToken> tokens;     // the tokens compared
		std::vector<int> ids;
	};

	void TokenIds(std::wstring_view a, std::wstring_view b, const DfOptions& options, TokenSeq& sa, TokenSeq& sb) {
		std::vector<std::wstring> keys;
		auto ta = DfTokenize(a), tb = DfTokenize(b);
		keys.reserve(ta.size() + tb.size());
		DfIds ids(ta.size() + tb.size());
		auto fill = [&](std::wstring_view line, const std::vector<DfToken>& tokens, TokenSeq& seq) {
			seq.tokens.reserve(tokens.size());
			seq.ids.reserve(tokens.size());
			for (auto& t : tokens) {
				if (t.space && options.whitespace == DfWhitespace::All)
					continue;
				if (t.space && options.whitespace == DfWhitespace::Change)
					keys.emplace_back(L" ");
				else
					keys.emplace_back(line.substr(t.start, t.length));
				if (options.ignoreCase)
					Lower(keys.back());
				seq.tokens.push_back(t);
				seq.ids.push_back(ids(keys.back()));
			}
		};
		fill(a, ta, sa);
		fill(b, tb, sb);
	}

	// the characters of a and b that differ, by characters
	void CharDiff(std::wstring_view a, std::wstring_view b, bool ignoreCase, std::vector<uint8_t>& marksA, std::vector<uint8_t>& marksB) {
		std::vector<int> ca(a.begin(), a.end()), cb(b.begin(), b.end());
		if (ignoreCase) {
			for (auto& c : ca)
				c = int(UINT_PTR(CharLowerW(LPWSTR(UINT_PTR(c)))));
			for (auto& c : cb)
				c = int(UINT_PTR(CharLowerW(LPWSTR(UINT_PTR(c)))));
		}
		Diff(ca, cb, marksA, marksB);
	}

	void AddSpan(std::vector<DfSpan>& spans, uint32_t start, uint32_t length) {
		if (length == 0)
			return;
		if (!spans.empty() && spans.back().start + spans.back().length == start)
			spans.back().length += length;
		else
			spans.push_back({ start, length });
	}

	// Joins spans split only by whitespace.
	void JoinSpans(std::wstring_view line, std::vector<DfSpan>& spans) {
		if (spans.size() < 2)
			return;
		size_t out = 0;
		for (size_t i = 1; i < spans.size(); i++) {
			auto& last = spans[out];
			auto gapStart = last.start + last.length;
			auto gap = line.substr(gapStart, spans[i].start - gapStart);
			if (IsBlank(gap))
				last.length = spans[i].start + spans[i].length - last.start;
			else
				spans[++out] = spans[i];
		}
		spans.resize(out + 1);
	}
}

double DfSimilarity(std::wstring_view a, std::wstring_view b, const DfOptions& options) {
	TokenSeq sa, sb;
	TokenIds(a, b, options, sa, sb);
	std::vector<uint8_t> ma, mb;
	Diff(sa.ids, sb.ids, ma, mb);
	size_t total = 0, common = 0;
	for (size_t i = 0; i < sa.tokens.size(); i++) {
		if (sa.tokens[i].space)
			continue;
		total += sa.tokens[i].length;
		if (!ma[i])
			common += 2 * sa.tokens[i].length;
	}
	for (auto& t : sb.tokens)
		if (!t.space)
			total += t.length;
	return total == 0 ? 1.0 : double(common) / total;
}

void DfIntraLine(std::wstring_view a, std::wstring_view b, const DfOptions& options, std::vector<DfSpan>& spansA, std::vector<DfSpan>& spansB) {
	spansA.clear();
	spansB.clear();
	TokenSeq sa, sb;
	TokenIds(a, b, options, sa, sb);
	std::vector<uint8_t> ma, mb;
	Diff(sa.ids, sb.ids, ma, mb);

	// with only the amount of whitespace ignored, a space changed at either end of the line doesn't count
	auto ignored = [&](const TokenSeq& seq, size_t i, std::wstring_view line) {
		auto& t = seq.tokens[i];
		return t.space && options.whitespace == DfWhitespace::Change && (t.start == 0 || t.start + t.length == line.size());
	};

	size_t i = 0, j = 0;
	const size_t n = sa.ids.size(), m = sb.ids.size();
	while (i < n || j < m) {
		if (i < n && j < m && !ma[i] && !mb[j]) {
			i++, j++;
			continue;
		}
		size_t i0 = i, j0 = j;
		while (i < n && ma[i])
			i++;
		while (j < m && mb[j])
			j++;
		if (i == i0 && j == j0) {   // inconsistent marks (can't happen); take what's left
			i = n, j = m;
		}
		if (i - i0 == 1 && j - j0 == 1 && !sa.tokens[i0].space && !sb.tokens[j0].space) {
			// one word changed into another: if it mostly stayed, show the characters
			auto& ta = sa.tokens[i0];
			auto& tb = sb.tokens[j0];
			auto wa = a.substr(ta.start, ta.length), wb = b.substr(tb.start, tb.length);
			std::vector<uint8_t> ca, cb;
			CharDiff(wa, wb, options.ignoreCase, ca, cb);
			size_t same = std::count(ca.begin(), ca.end(), uint8_t(0));
			if (2.0 * same / (wa.size() + wb.size()) >= 0.5) {
				for (size_t k = 0; k < ca.size(); k++)
					if (ca[k])
						AddSpan(spansA, ta.start + uint32_t(k), 1);
				for (size_t k = 0; k < cb.size(); k++)
					if (cb[k])
						AddSpan(spansB, tb.start + uint32_t(k), 1);
				continue;
			}
		}
		for (size_t k = i0; k < i; k++)
			if (!ignored(sa, k, a))
				AddSpan(spansA, sa.tokens[k].start, sa.tokens[k].length);
		for (size_t k = j0; k < j; k++)
			if (!ignored(sb, k, b))
				AddSpan(spansB, sb.tokens[k].start, sb.tokens[k].length);
	}
	JoinSpans(a, spansA);
	JoinSpans(b, spansB);
}

// ---- Diff ----------------------------------------------------------------------------------

namespace {
	// Slides a block that only inserts (or only deletes) along the equal lines around it (GNU diff's and git's
	// "sliding"): to end on a blank line if one of its positions does, else as far down as it goes. The equal
	// blocks on either side stay non-empty.
	void SlideBlocks(std::vector<DfBlock>& blocks, const std::vector<int>& idsL, const std::vector<int>& idsR, const DfLines& left, const DfLines& right) {
		for (size_t k = 1; k + 1 < blocks.size(); k++) {
			auto& block = blocks[k];
			auto& prev = blocks[k - 1];
			auto& next = blocks[k + 1];
			if (!block.changed || prev.changed || next.changed || (block.leftCount == 0) == (block.rightCount == 0))
				continue;
			const bool onRight = block.leftCount == 0;
			auto& ids = onRight ? idsR : idsL;
			auto& lines = onRight ? right : left;
			const size_t start = onRight ? block.right : block.left;
			const size_t count = onRight ? block.rightCount : block.leftCount;

			size_t up = 0;
			while (up + 1 < prev.leftCount && ids[start - up - 1] == ids[start + count - up - 1])
				up++;
			size_t down = 0;
			while (down + 1 < next.leftCount && ids[start + down] == ids[start + count + down])
				down++;
			if (up == 0 && down == 0)
				continue;

			ptrdiff_t delta = ptrdiff_t(down);
			for (ptrdiff_t d = ptrdiff_t(down); d >= -ptrdiff_t(up); d--) {
				if (IsBlank(lines[start + count - 1 + d])) {
					delta = d;
					break;
				}
			}
			if (delta == 0)
				continue;
			auto shift = [](size_t& value, ptrdiff_t by) { value = size_t(ptrdiff_t(value) + by); };
			shift(prev.leftCount, delta);
			shift(prev.rightCount, delta);
			shift(next.left, delta);
			shift(next.right, delta);
			shift(next.leftCount, -delta);
			shift(next.rightCount, -delta);
			shift(block.left, delta);
			shift(block.right, delta);
		}
	}

	// Pairs the lines of a changed block that are alike enough, in order, most alike overall.
	void PairLines(const DfLines& left, const DfLines& right, const DfBlock& block, const DfOptions& options, std::vector<std::pair<size_t, size_t>>& pairs) {
		constexpr double threshold = 0.5;
		const size_t n = block.leftCount, m = block.rightCount;
		if (n * m > 64 * 64) {
			// too big to weigh every pairing: by position
			for (size_t i = 0; i < std::min(n, m); i++)
				if (DfSimilarity(left[block.left + i], right[block.right + i], options) >= threshold)
					pairs.emplace_back(block.left + i, block.right + i);
			return;
		}
		std::vector<double> sim(n * m), score((n + 1) * (m + 1), 0);
		for (size_t i = 0; i < n; i++)
			for (size_t j = 0; j < m; j++)
				sim[i * m + j] = DfSimilarity(left[block.left + i], right[block.right + j], options);
		auto at = [&](size_t i, size_t j) -> double& { return score[i * (m + 1) + j]; };
		for (size_t i = 1; i <= n; i++) {
			for (size_t j = 1; j <= m; j++) {
				double best = std::max(at(i - 1, j), at(i, j - 1));
				double s = sim[(i - 1) * m + j - 1];
				if (s >= threshold)
					best = std::max(best, at(i - 1, j - 1) + s);
				at(i, j) = best;
			}
		}
		size_t first = pairs.size();
		for (size_t i = n, j = m; i > 0 && j > 0;) {
			double s = sim[(i - 1) * m + j - 1];
			if (s >= threshold && at(i, j) == at(i - 1, j - 1) + s) {
				pairs.emplace_back(block.left + i - 1, block.right + j - 1);
				i--, j--;
			}
			else if (at(i, j) == at(i - 1, j))
				i--;
			else
				j--;
		}
		std::reverse(pairs.begin() + first, pairs.end());
	}
}

DfDiff DfCompute(const DfLines& left, const DfLines& right, const DfOptions& options) {
	const size_t n = left.size(), m = right.size();

	// the lines as ids; a last line with no line break differs from the same line with one (as in git)
	std::vector<std::wstring> normalized;
	const bool exact = options.whitespace == DfWhitespace::Exact && !options.ignoreCase;
	normalized.reserve(exact ? 2 : n + m);
	DfIds ids(n + m);
	std::vector<int> idsL(n), idsR(m);
	for (size_t i = 0; i < n + m; i++) {
		auto line = i < n ? left[i] : right[i - n];
		const bool noBreak = i < n ? i + 1 == n && !left.EndsWithNewline() : i + 1 == n + m && !right.EndsWithNewline();
		if (!exact || noBreak) {
			normalized.push_back(exact ? std::wstring(line) : DfNormalize(line, options));
			if (noBreak)
				normalized.back() += L'\n';   // never in a line
			line = normalized.back();
		}
		(i < n ? idsL[i] : idsR[i - n]) = ids(line);
	}

	// anchors: lines that appear once on each side, the longest run of them in the same order on both
	int idCount = 0;
	for (auto id : idsL)
		idCount = std::max(idCount, id + 1);
	for (auto id : idsR)
		idCount = std::max(idCount, id + 1);
	std::vector<int> countL(idCount), countR(idCount), posR(idCount);
	for (auto id : idsL)
		countL[id]++;
	for (size_t j = 0; j < m; j++) {
		countR[idsR[j]]++;
		posR[idsR[j]] = int(j);
	}
	std::vector<int> candL, candR;
	for (size_t i = 0; i < n; i++) {
		auto id = idsL[i];
		if (countL[id] == 1 && countR[id] == 1) {
			candL.push_back(int(i));
			candR.push_back(posR[id]);
		}
	}
	auto lis = Lis(candR);

	std::vector<uint8_t> marksL(n), marksR(m);
	DfMyers myers(idsL.data(), int(n), idsR.data(), int(m), marksL.data(), marksR.data(), options.costLimit);
	int x = 0, y = 0;
	for (auto k : lis) {
		myers.Compare(x, candL[k], y, candR[k]);
		x = candL[k] + 1;
		y = candR[k] + 1;
	}
	myers.Compare(x, int(n), y, int(m));

	DfDiff diff;
	size_t i = 0, j = 0;
	while (i < n || j < m) {
		DfBlock block;
		block.left = i;
		block.right = j;
		if (i < n && j < m && !marksL[i] && !marksR[j]) {
			while (i < n && j < m && !marksL[i] && !marksR[j])
				i++, j++;
		}
		else {
			block.changed = true;
			while (i < n && marksL[i])
				i++;
			while (j < m && marksR[j])
				j++;
			if (i == block.left && j == block.right)   // inconsistent marks (can't happen); the rest changed
				i = n, j = m;
		}
		block.leftCount = i - block.left;
		block.rightCount = j - block.right;
		diff.blocks.push_back(block);
	}
	SlideBlocks(diff.blocks, idsL, idsR, left, right);

	diff.leftPair.assign(n, -1);
	diff.rightPair.assign(m, -1);
	std::vector<std::pair<size_t, size_t>> pairs;
	for (auto& block : diff.blocks) {
		if (!block.changed)
			continue;
		diff.changes++;
		diff.deleted += block.leftCount;
		diff.inserted += block.rightCount;
		if (block.leftCount > 0 && block.rightCount > 0)
			PairLines(left, right, block, options, pairs);
	}
	diff.pairs.reserve(pairs.size());
	for (auto [l, r] : pairs) {
		DfLinePair pair;
		pair.left = l;
		pair.right = r;
		if (options.intraLine)
			DfIntraLine(left[l], right[r], options, pair.leftSpans, pair.rightSpans);
		diff.leftPair[l] = diff.rightPair[r] = int(diff.pairs.size());
		diff.pairs.push_back(std::move(pair));
	}
	return diff;
}

size_t DfBlockOfLine(const DfDiff& diff, bool right, size_t line) {
	auto it = std::partition_point(diff.blocks.begin(), diff.blocks.end(), [&](const DfBlock& b) {
		return (right ? b.right + b.rightCount : b.left + b.leftCount) <= line;
	});
	return size_t(it - diff.blocks.begin());
}

// ---- Rows ----------------------------------------------------------------------------------

std::vector<DfRow> DfBuildRows(const DfDiff& diff, DfLayout layout, int context, const std::vector<uint8_t>& expanded) {
	std::vector<DfRow> rows;
	size_t total = 0;
	for (auto& b : diff.blocks)
		total += layout == DfLayout::Unified && b.changed ? b.leftCount + b.rightCount : std::max(b.leftCount, b.rightCount);
	rows.reserve(total);

	for (size_t k = 0; k < diff.blocks.size(); k++) {
		auto& b = diff.blocks[k];
		auto block = uint32_t(k);
		auto equal = [&](size_t from, size_t to) {
			for (size_t i = from; i < to; i++)
				rows.push_back({ DfRowKind::Equal, false, int(b.left + i), int(b.right + i), block });
		};
		if (!b.changed) {
			size_t before = k > 0 ? size_t(context) : 0;
			size_t after = k + 1 < diff.blocks.size() ? size_t(context) : 0;
			if (context < 0 || (k < expanded.size() && expanded[k]) || before + after + 2 > b.leftCount) {
				equal(0, b.leftCount);
				continue;
			}
			equal(0, before);
			rows.push_back({ DfRowKind::Fold, false, int(b.left + before), int(b.right + before), block, uint32_t(b.leftCount - before - after) });
			equal(b.leftCount - after, b.leftCount);
			continue;
		}

		auto changed = [&](int l, int r, bool paired) {
			rows.push_back({ DfRowKind::Changed, paired, l, r, block });
		};
		if (layout == DfLayout::Unified) {
			for (size_t i = b.left; i < b.left + b.leftCount; i++)
				changed(int(i), -1, diff.leftPair[i] >= 0);
			for (size_t j = b.right; j < b.right + b.rightCount; j++)
				changed(-1, int(j), diff.rightPair[j] >= 0);
			continue;
		}
		// side by side: the lines between pairs next to each other, then the pair
		size_t i = b.left, j = b.right;
		const size_t li = b.left + b.leftCount, lj = b.right + b.rightCount;
		auto zip = [&](size_t toI, size_t toJ) {
			while (i < toI || j < toJ) {
				changed(i < toI ? int(i) : -1, j < toJ ? int(j) : -1, false);
				if (i < toI)
					i++;
				if (j < toJ)
					j++;
			}
		};
		while (i < li) {
			int p = diff.leftPair[i];
			if (p < 0) {
				// the next pair in this block, if any
				size_t next = i;
				while (next < li && diff.leftPair[next] < 0)
					next++;
				if (next == li)
					break;
				zip(next, diff.pairs[diff.leftPair[next]].right);
				continue;
			}
			zip(i, diff.pairs[p].right);
			changed(int(i), int(diff.pairs[p].right), true);
			i++;
			j = diff.pairs[p].right + 1;
		}
		zip(li, lj);
	}
	return rows;
}

int DfNextChange(const std::vector<DfRow>& rows, int row, bool forward) {
	const int count = int(rows.size());
	auto inChange = [&](int r) { return r >= 0 && r < count && rows[r].kind == DfRowKind::Changed; };
	if (forward) {
		int r = std::max(row, -1);
		if (inChange(r)) {
			auto block = rows[r].block;
			while (r < count && rows[r].block == block)
				r++;
		}
		else
			r++;
		while (r < count && rows[r].kind != DfRowKind::Changed)
			r++;
		return r < count ? r : -1;
	}
	int r = std::min(row, count);
	if (inChange(r)) {
		auto block = rows[r].block;
		while (r >= 0 && rows[r].block == block)
			r--;
	}
	else
		r--;
	while (r >= 0 && rows[r].kind != DfRowKind::Changed)
		r--;
	if (r < 0)
		return -1;
	while (r > 0 && rows[r - 1].block == rows[r].block)
		r--;
	return r;
}

int DfRowOfLine(const std::vector<DfRow>& rows, bool right, size_t line) {
	for (size_t r = 0; r < rows.size(); r++) {
		int l = right ? rows[r].right : rows[r].left;
		if (l < 0)
			continue;
		if (rows[r].kind == DfRowKind::Fold ? line < size_t(l) + rows[r].hidden : line <= size_t(l))
			return int(r);
	}
	return -1;
}

// ---- Merging and text ---------------------------------------------------------------------

void DfMerge(DfLines& left, DfLines& right, const DfBlock& block, bool toRight) {
	if (toRight)
		right.Replace(block.right, block.rightCount, left, block.left, block.leftCount);
	else
		left.Replace(block.left, block.leftCount, right, block.right, block.rightCount);
}

std::wstring DfUnifiedText(const DfLines& left, const DfLines& right, const DfDiff& diff, int context, std::wstring_view leftName, std::wstring_view rightName, std::wstring_view eol) {
	std::wstring text;
	if (diff.Identical())
		return text;
	context = std::max(context, 0);
	text.append(L"--- ").append(leftName).append(eol);
	text.append(L"+++ ").append(rightName).append(eol);

	auto range = [](size_t start, size_t count) {
		auto s = std::to_wstring(count == 0 ? start : start + 1);
		if (count != 1)
			s += L"," + std::to_wstring(count);
		return s;
	};
	auto line = [&](wchar_t prefix, const DfLines& lines, size_t index) {
		text += prefix;
		text.append(lines[index]).append(eol);
		if (index + 1 == lines.size() && !lines.EndsWithNewline())
			text.append(L"\\ No newline at end of file").append(eol);
	};

	auto& blocks = diff.blocks;
	for (size_t k = 0; k < blocks.size(); k++) {
		if (!blocks[k].changed)
			continue;
		// the changed blocks close enough to share a hunk
		size_t last = k;
		while (last + 2 < blocks.size() && blocks[last + 1].leftCount <= size_t(2 * context))
			last += 2;
		size_t before = k > 0 ? std::min(size_t(context), blocks[k - 1].leftCount) : 0;
		size_t after = last + 1 < blocks.size() ? std::min(size_t(context), blocks[last + 1].leftCount) : 0;
		size_t leftStart = blocks[k].left - before, rightStart = blocks[k].right - before;
		size_t leftEnd = blocks[last].left + blocks[last].leftCount + after;
		size_t rightEnd = blocks[last].right + blocks[last].rightCount + after;
		text.append(L"@@ -").append(range(leftStart, leftEnd - leftStart)).append(L" +").append(range(rightStart, rightEnd - rightStart)).append(L" @@").append(eol);

		for (size_t i = leftStart; i < blocks[k].left; i++)
			line(L' ', left, i);
		for (size_t b = k; b <= last; b++) {
			auto& block = blocks[b];
			if (block.changed) {
				for (size_t i = 0; i < block.leftCount; i++)
					line(L'-', left, block.left + i);
				for (size_t i = 0; i < block.rightCount; i++)
					line(L'+', right, block.right + i);
			}
			else {
				for (size_t i = 0; i < block.leftCount; i++)
					line(L' ', left, block.left + i);
			}
		}
		for (size_t i = 0; i < after; i++)
			line(L' ', left, blocks[last + 1].left + i);
		k = last;
	}
	return text;
}
