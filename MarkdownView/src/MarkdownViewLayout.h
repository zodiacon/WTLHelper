#pragma once

// The markdown view's flow: the document's leaves as boxes down a column, each with its indent, padding and margins,
// a list item's marker, and the bars of quotes and alerts over their boxes. A box's height is its text's, measured by
// the control (a DirectWrite layout) when it comes into view and estimated until then; Place stacks the boxes,
// collapsing the margins between them as CSS does. Window-free: covered by MarkdownViewTests.

#include "MarkdownViewModel.h"

struct MdvMetrics {                // all in DIPs, the zoom applied
	float fontSize = 16;           // the body text's
	float headingScale[6] = { 2.0f, 1.5f, 1.25f, 1.0f, 0.875f, 0.85f };
	float codeScale = 0.875f;      // code blocks and spans
	float footnoteScale = 0.85f;
	float lineHeight = 1.5f;       // times the font size
	float headingLineHeight = 1.25f;
	float codeLineHeight = 1.45f;
	float spacing = 1.0f;          // between blocks, times the font size
	float charWidth = 0.5f;        // an average, times the font size: for estimates
	float codeCharWidth = 0.6f;
	float padding = 16;            // above the first box and below the last

	float Em() const { return fontSize; }
};

enum class MdvBoxKind : uint8_t {
	Text,          // a paragraph, a heading, an empty list item's line
	Code,
	Rule,          // a thematic break, or the line over the footnotes
	Table,
	AlertTitle,    // an alert's icon and name
	Summary,       // a <details>' summary with its triangle
};

enum class MdvMarker : uint8_t { None, Disc, Circle, Square, Number, Unchecked, Checked };

struct MdvBox {
	int block = 0;                 // the leaf; an alert, a details, a table, an item or the footnotes for their boxes
	MdvBoxKind kind = MdvBoxKind::Text;
	float indent = 0;              // from the column's left
	float padLeft = 0, padTop = 0, padRight = 0, padBottom = 0;
	float marginTop = 0, marginBottom = 0;
	float scale = 1;               // of the body's font size
	bool muted = false;            // in a quote
	float height = 0;              // the content's: measured, or estimated
	bool measured = false;
	float top = 0;                 // the box's (its padding's), from the flow's top
	MdvMarker marker = MdvMarker::None;   // left of the box: the first box of a list item or a footnote
	int markerBlock = -1;          // that item or footnote

	float Bottom() const { return top + padTop + height + padBottom; }
	float TextWidth(float column) const { return column - indent - padLeft - padRight; }
};

struct MdvSpan {                   // a quote's or an alert's bar beside its boxes
	int block = 0;
	size_t first = 0, last = 0;    // boxes, inclusive
	float indent = 0;
};

struct MdvFlow {
	std::vector<MdvBox> boxes;
	std::vector<MdvSpan> spans;
	float height = 0;
	float padding = 0;

	void Build(const MdvDocument& doc, const MdvMetrics& m);   // details follow MdvBlock::open
	void Estimate(const MdvDocument& doc, const MdvMetrics& m, float column);   // the boxes not measured
	void Place(size_t from = 0);
	size_t BoxAt(float y) const;   // the box at y, or the next one below; boxes.size() past the end
	int BoxOfBlock(int block) const;   // the first box showing a block (or its first leaf), -1 if hidden

	static float EstimateHeight(const MdvDocument& doc, const MdvBox& box, const MdvMetrics& m, float column);
};
