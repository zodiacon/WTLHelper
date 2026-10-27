// The markdown view's flow: the document's leaves as boxes down a column (see MarkdownViewLayout.h).

#include "MarkdownViewLayout.h"

#include <algorithm>
#include <cmath>

namespace {

struct Context {
	float indent = 0;
	float scale = 1;
	bool muted = false;
	bool tight = false;            // in a tight list's item
	int listDepth = 0;
};

struct Builder {
	const MdvDocument& doc;
	const MdvMetrics& m;
	MdvFlow& flow;

	float Em(const Context& c) const { return m.Em() * c.scale; }

	MdvBox& Add(int block, MdvBoxKind kind, const Context& c) {
		MdvBox box;
		box.block = block;
		box.kind = kind;
		box.indent = c.indent;
		box.scale = c.scale;
		box.muted = c.muted;
		flow.boxes.push_back(box);
		return flow.boxes.back();
	}

	// a container's space below it, on its last box
	void SpaceAfter(size_t first, float space) {
		if (flow.boxes.size() > first)
			flow.boxes.back().marginBottom = std::max(flow.boxes.back().marginBottom, space);
	}

	void Children(int b, const Context& c) {
		for (int child : doc.blocks[b].children)
			Block(child, c);
	}

	void Block(int b, const Context& c) {
		auto& block = doc.blocks[b];
		const float em = Em(c);
		const float spacing = m.spacing * em;
		switch (block.kind) {
			case MdvKind::Document:
				Children(b, c);
				break;
			case MdvKind::Paragraph: {
				auto& box = Add(b, MdvBoxKind::Text, c);
				box.marginBottom = c.tight ? 0 : spacing;
				break;
			}
			case MdvKind::Heading: {
				Context h = c;
				h.scale = c.scale * m.headingScale[std::clamp(block.level, 1, 6) - 1];
				auto& box = Add(b, MdvBoxKind::Text, h);
				box.marginTop = 1.5f * em;
				box.marginBottom = spacing;
				if (block.level <= 2)
					box.padBottom = 0.3f * Em(h) + 1;   // the line under it
				break;
			}
			case MdvKind::Code: {
				Context code = c;
				code.scale = c.scale * m.codeScale;
				auto& box = Add(b, MdvBoxKind::Code, code);
				box.padLeft = box.padRight = box.padTop = box.padBottom = em;
				box.marginBottom = spacing;
				break;
			}
			case MdvKind::Rule: {
				auto& box = Add(b, MdvBoxKind::Rule, c);
				box.height = std::max(1.0f, 0.25f * em);
				box.measured = true;
				box.marginTop = box.marginBottom = 1.5f * em;
				break;
			}
			case MdvKind::Quote:
			case MdvKind::Alert: {
				const size_t first = flow.boxes.size();
				Context inner = c;
				inner.indent = c.indent + 1.25f * em;
				inner.tight = false;
				inner.muted = block.kind == MdvKind::Quote || c.muted;
				if (block.kind == MdvKind::Alert) {
					auto& title = Add(b, MdvBoxKind::AlertTitle, inner);
					title.height = m.lineHeight * em;
					title.measured = true;
					title.marginBottom = 0.25f * em;
				}
				Children(b, inner);
				if (flow.boxes.size() > first) {
					flow.spans.push_back({ b, first, flow.boxes.size() - 1, c.indent });
					// the space below a quote's last child is the quote's
					flow.boxes.back().marginBottom = spacing;
				}
				break;
			}
			case MdvKind::List: {
				const size_t first = flow.boxes.size();
				Context inner = c;
				inner.listDepth = c.listDepth + 1;
				inner.tight = block.tight;
				for (size_t i = 0; i < block.children.size(); i++) {
					const size_t itemFirst = flow.boxes.size();
					Item(block.children[i], inner);
					if (i > 0 && block.tight && flow.boxes.size() > itemFirst)
						flow.boxes[itemFirst].marginTop = std::max(flow.boxes[itemFirst].marginTop, 0.25f * em);
				}
				// a list in a list item has no space of its own
				const bool nested = block.parent >= 0 && doc.blocks[block.parent].kind == MdvKind::Item;
				if (!nested)
					SpaceAfter(first, spacing);
				break;
			}
			case MdvKind::Table: {
				auto& box = Add(b, MdvBoxKind::Table, c);
				box.padLeft = box.padRight = box.padTop = box.padBottom = 0.5f * em;
				box.marginBottom = spacing;
				break;
			}
			case MdvKind::Details: {
				const size_t first = flow.boxes.size();
				auto& summary = Add(b, MdvBoxKind::Summary, c);
				summary.padLeft = 1.25f * em;
				summary.marginBottom = block.open ? 0.5f * em : 0;
				if (block.open) {
					Context inner = c;
					inner.tight = false;
					Children(b, inner);
				}
				SpaceAfter(first, spacing);
				break;
			}
			case MdvKind::Footnotes: {
				const size_t first = flow.boxes.size();
				auto& rule = Add(b, MdvBoxKind::Rule, c);
				rule.height = 1;
				rule.measured = true;
				rule.marginTop = 2 * em;
				rule.marginBottom = em;
				Context inner = c;
				inner.scale = c.scale * m.footnoteScale;
				for (int fn : block.children)
					Item(fn, inner);
				SpaceAfter(first, spacing);
				break;
			}
			default:
				Children(b, c);
				break;
		}
	}

	// a list item or a footnote: its boxes indented, the first with the marker
	void Item(int b, const Context& c) {
		auto& block = doc.blocks[b];
		Context inner = c;
		inner.indent = c.indent + 2 * Em(c);
		if (block.kind == MdvKind::Footnote)
			inner.tight = true;
		const size_t first = flow.boxes.size();
		Children(b, inner);
		if (flow.boxes.size() == first)
			Add(b, MdvBoxKind::Text, inner);   // an empty item still shows its marker
		auto& box = flow.boxes[first];
		box.markerBlock = b;
		if (block.kind == MdvKind::Footnote)
			box.marker = MdvMarker::Number;
		else if (block.task != MdvTask::None)
			box.marker = block.task == MdvTask::Checked ? MdvMarker::Checked : MdvMarker::Unchecked;
		else if (doc.blocks[block.parent].ordered)
			box.marker = MdvMarker::Number;
		else
			box.marker = c.listDepth <= 1 ? MdvMarker::Disc : c.listDepth == 2 ? MdvMarker::Circle : MdvMarker::Square;
		if (block.kind == MdvKind::Footnote && flow.boxes.size() > first)
			flow.boxes.back().marginBottom = std::max(flow.boxes.back().marginBottom, 0.5f * Em(c));
	}
};

}  // namespace

void MdvFlow::Build(const MdvDocument& doc, const MdvMetrics& m) {
	boxes.clear();
	spans.clear();
	padding = m.padding;
	if (doc.blocks.empty())
		return;
	Builder builder{ doc, m, *this };
	builder.Block(0, Context{});
}

float MdvFlow::EstimateHeight(const MdvDocument& doc, const MdvBox& box, const MdvMetrics& m, float column) {
	const float em = m.Em() * box.scale;
	const float width = std::max(box.TextWidth(column), em);
	auto& block = doc.blocks[box.block];
	switch (box.kind) {
		case MdvBoxKind::Code: {
			const auto& text = block.content.text;
			const size_t lines = 1 + std::count(text.begin(), text.end(), L'\n');
			return lines * m.codeLineHeight * em;
		}
		case MdvBoxKind::Table: {
			size_t rows = block.children.size();
			return std::max<size_t>(rows, 1) * m.lineHeight * em * 1.4f;
		}
		case MdvBoxKind::Rule:
		case MdvBoxKind::AlertTitle:
			return box.height;
		default: {
			const auto& text = block.content.text;
			const float lineHeight = (block.kind == MdvKind::Heading ? m.headingLineHeight : m.lineHeight) * em;
			const size_t perLine = std::max<size_t>(1, (size_t)(width / (m.charWidth * em)));
			size_t lines = 0, run = 0;
			for (wchar_t ch : text) {
				if (ch == L'\n') {
					lines += 1 + run / perLine;
					run = 0;
				}
				else
					run++;
			}
			lines += 1 + (run > 0 ? (run - 1) / perLine : 0);
			return lines * lineHeight;
		}
	}
}

void MdvFlow::Estimate(const MdvDocument& doc, const MdvMetrics& m, float column) {
	for (auto& box : boxes)
		if (!box.measured)
			box.height = EstimateHeight(doc, box, m, column);
}

void MdvFlow::Place(size_t from) {
	from = std::min(from, boxes.size());
	float y = padding;
	if (from > 0)
		y = boxes[from - 1].Bottom();
	for (size_t i = from; i < boxes.size(); i++) {
		auto& box = boxes[i];
		const float gap = i == 0 ? 0 : std::max(boxes[i - 1].marginBottom, box.marginTop);
		box.top = y + gap;
		y = box.Bottom();
	}
	height = y + padding;
}

size_t MdvFlow::BoxAt(float y) const {
	// the last box starting at or above y
	auto it = std::upper_bound(boxes.begin(), boxes.end(), y, [](float v, const MdvBox& b) { return v < b.top; });
	if (it == boxes.begin())
		return 0;
	size_t i = (it - boxes.begin()) - 1;
	return y < boxes[i].Bottom() ? i : i + 1;
}

int MdvFlow::BoxOfBlock(int block) const {
	for (size_t i = 0; i < boxes.size(); i++)
		if (boxes[i].block == block || boxes[i].markerBlock == block)
			return (int)i;
	return -1;
}
