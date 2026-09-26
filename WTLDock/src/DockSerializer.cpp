#include "DockSerializer.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace WTLDock {

using Json::Value;

namespace {

constexpr int FormatVersion = 1;
constexpr int MaxDepth = 32;
constexpr double CoordinateLimit = 1e6;

std::string ToUtf8(const std::wstring& s) {
	if (s.empty())
		return {};
	int size = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
	std::string result(size, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), size, nullptr, nullptr);
	return result;
}

std::wstring FromUtf8(const std::string& s) {
	if (s.empty())
		return {};
	int size = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring result(size, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), result.data(), size);
	return result;
}

const char* const SideNames[] = { "left", "right", "top", "bottom" };

const char* StateName(PaneState state) {
	switch (state) {
		case PaneState::Document: return "document";
		case PaneState::AutoHide: return "autohide";
		case PaneState::Floating: return "floating";
		default: return "docked";
	}
}

bool ParseSide(const Value* v, DockSide& side) {
	if (!v || !v->IsString())
		return false;
	for (int i = 0; i < SideCount; i++) {
		if (v->String == SideNames[i]) {
			side = (DockSide)i;
			return true;
		}
	}
	return false;
}

bool ParseState(const Value* v, PaneState& state) {
	if (!v || !v->IsString())
		return false;
	for (auto s : { PaneState::Docked, PaneState::Document, PaneState::AutoHide, PaneState::Floating }) {
		if (v->String == StateName(s)) {
			state = s;
			return true;
		}
	}
	return false;
}

bool ParseInt(const Value* v, int& result) {
	if (!v || !v->IsNumber() || !std::isfinite(v->Number))
		return false;
	result = (int)std::clamp(v->Number, -CoordinateLimit, CoordinateLimit);
	return true;
}

bool ParseInts(const Value* v, int* values, int count) {
	if (!v || !v->IsArray() || (int)v->Items.size() != count)
		return false;
	for (int i = 0; i < count; i++)
		if (!ParseInt(&v->Items[i], values[i]))
			return false;
	return true;
}

Value MakeInts(std::initializer_list<int> values) {
	Value a = Value::MakeArray();
	for (int i : values)
		a.Push(Value::MakeNumber(i));
	return a;
}

Value SaveSize(const SizeSpec& size) {
	Value o = Value::MakeObject();
	o.Add(size.IsStar() ? "star" : "px", Value::MakeNumber(size.Value));
	return o;
}

bool ParseSize(const Value* v, SizeSpec& size) {
	if (!v || !v->IsObject() || v->Items.size() != 1 || !v->Items[0].IsNumber())
		return false;
	double value = v->Items[0].Number;
	if (!std::isfinite(value) || value <= 0 || value > CoordinateLimit)
		return false;
	if (v->Keys[0] == "star")
		size = SizeSpec::Star(value);
	else if (v->Keys[0] == "px")
		size = SizeSpec::Px(value);
	else
		return false;
	return true;
}

std::wstring FormatNumber(double d) {
	return std::format(L"{}", d);
}

void DumpNode(std::wstring& out, const DockNode& node, bool showSize) {
	if (auto split = node.AsSplit()) {
		out += split->GetAxis() == Axis::Horizontal ? L"H(" : L"V(";
		bool first = true;
		for (auto& c : split->Children()) {
			if (!first)
				out += L' ';
			first = false;
			DumpNode(out, *c, true);
		}
		out += L')';
	}
	else {
		auto group = node.AsGroup();
		out += group->IsDocument() ? L"D[" : L"T[";
		for (size_t i = 0; i < group->Panes().size(); i++) {
			if (i)
				out += L',';
			if (group->Panes().size() > 1 && (int)i == group->ActiveIndex())
				out += L'>';
			out += group->Panes()[i]->Id();
		}
		out += L']';
	}
	if (showSize) {
		if (node.Size.IsStar()) {
			if (node.Size.Value != 1)
				out += L"*" + FormatNumber(node.Size.Value);
		}
		else {
			out += L"@" + FormatNumber(node.Size.Value);
		}
	}
}

bool HasDocumentGroup(const DockNode& node) {
	if (auto split = node.AsSplit()) {
		return std::any_of(split->Children().begin(), split->Children().end(), [](auto& c) { return HasDocumentGroup(*c); });
	}
	return node.AsGroup()->IsDocument();
}

}

//
// save
//

Value DockSerializer::SaveGroup(const DockGroup& group) {
	Value o = Value::MakeObject();
	o.Add("type", Value::MakeString("group"));
	o.Add("kind", Value::MakeString(group.IsDocument() ? "document" : "tool"));
	Value panes = Value::MakeArray();
	for (auto p : group.Panes())
		panes.Push(Value::MakeString(ToUtf8(p->Id())));
	o.Add("panes", std::move(panes));
	if (auto active = group.ActivePane())
		o.Add("active", Value::MakeString(ToUtf8(active->Id())));
	o.Add("tabsBottom", Value::MakeBool(group.TabsAtBottom));
	return o;
}

Value DockSerializer::SaveNode(const DockNode& node) {
	Value o;
	if (auto split = node.AsSplit()) {
		o = Value::MakeObject();
		o.Add("type", Value::MakeString("split"));
		o.Add("axis", Value::MakeString(split->GetAxis() == Axis::Horizontal ? "h" : "v"));
		Value kids = Value::MakeArray();
		for (auto& c : split->Children())
			kids.Push(SaveNode(*c));
		o.Add("children", std::move(kids));
	}
	else {
		o = SaveGroup(*node.AsGroup());
	}
	o.Add("size", SaveSize(node.Size));
	return o;
}

std::string DockSerializer::Save(const DockLayout& layout) {
	Value root = Value::MakeObject();
	root.Add("version", Value::MakeNumber(FormatVersion));
	root.Add("dpi", Value::MakeNumber(layout.m_Dpi));
	root.Add("appVersion", Value::MakeNumber(layout.m_AppVersion));
	root.Add("main", SaveNode(*layout.m_Root));

	Value bars = Value::MakeObject();
	for (int side = 0; side < SideCount; side++) {
		Value groups = Value::MakeArray();
		for (auto& g : layout.m_AutoHide[side]) {
			Value o = SaveGroup(*g);
			o.Add("hideLength", Value::MakeNumber(g->AutoHideLength));
			groups.Push(std::move(o));
		}
		bars.Add(SideNames[side], std::move(groups));
	}
	root.Add("autoHide", std::move(bars));

	Value floats = Value::MakeArray();
	for (auto& f : layout.m_Floats) {
		Value o = Value::MakeObject();
		o.Add("rect", MakeInts({ f->m_Rect.left, f->m_Rect.top, f->m_Rect.right, f->m_Rect.bottom }));
		o.Add("dpi", Value::MakeNumber(f->m_Dpi));
		o.Add("root", SaveNode(*f->m_Root));
		floats.Push(std::move(o));
	}
	root.Add("floats", std::move(floats));

	Value panes = Value::MakeArray();
	for (auto& p : layout.m_Panes) {
		Value o = Value::MakeObject();
		o.Add("id", Value::MakeString(ToUtf8(p->Id())));
		o.Add("lastState", Value::MakeString(StateName(p->m_LastState)));
		o.Add("lastSide", Value::MakeString(SideNames[(int)p->m_LastSide]));
		const RECT& rc = p->m_LastFloatRect;
		o.Add("lastFloat", MakeInts({ rc.left, rc.top, rc.right, rc.bottom }));
		o.Add("preferred", MakeInts({ p->PreferredSize.cx, p->PreferredSize.cy }));
		panes.Push(std::move(o));
	}
	root.Add("panes", std::move(panes));

	return Json::Write(root) + "\n";
}

//
// load
//

DockPane* DockSerializer::ResolvePane(Context& ctx, const std::string& id) {
	auto wide = FromUtf8(id);
	if (auto pane = ctx.Layout.FindPane(wide))
		return pane;
	return ctx.Factory && !wide.empty() ? ctx.Factory(ctx.Layout, wide) : nullptr;
}

// Returns null for a group that is skipped (a document group in an auto-hide bar) or, with ctx.Error set, for a bad one.
std::unique_ptr<DockGroup> DockSerializer::LoadGroup(Context& ctx, const Value& v, GroupLocation where) {
	auto kindValue = v.Find("kind");
	auto panes = v.Find("panes");
	if (!kindValue || !kindValue->IsString() || (kindValue->String != "tool" && kindValue->String != "document")) {
		ctx.Error = L"group without a valid kind";
		return nullptr;
	}
	if (!panes || !panes->IsArray()) {
		ctx.Error = L"group without a pane list";
		return nullptr;
	}

	const PaneKind kind = kindValue->String == "document" ? PaneKind::Document : PaneKind::Tool;
	if (kind == PaneKind::Document && where == GroupLocation::AutoHide)
		return nullptr;

	auto group = std::make_unique<DockGroup>(kind);
	for (auto& item : panes->Items) {
		if (!item.IsString()) {
			ctx.Error = L"pane id is not a string";
			return nullptr;
		}
		auto pane = ResolvePane(ctx, item.String);
		if (pane && pane->Kind() == kind && ctx.Used.insert(pane).second)
			group->m_Panes.push_back(pane);
	}

	if (auto active = v.Find("active"); active && active->IsString()) {
		auto id = FromUtf8(active->String);
		for (size_t i = 0; i < group->m_Panes.size(); i++)
			if (group->m_Panes[i]->Id() == id)
				group->m_Active = (int)i;
	}
	if (auto bottom = v.Find("tabsBottom"); bottom && bottom->Kind == Value::Type::Bool)
		group->TabsAtBottom = bottom->Bool;
	int length;
	if (ParseInt(v.Find("hideLength"), length) && length > 0)
		group->AutoHideLength = std::max(1, (int)std::lround(length * ctx.Scale));
	return group;
}

std::unique_ptr<DockNode> DockSerializer::LoadNode(Context& ctx, const Value& v, GroupLocation where, int depth) {
	if (depth > MaxDepth) {
		ctx.Error = L"layout nested too deeply";
		return nullptr;
	}
	auto type = v.Find("type");
	if (!v.IsObject() || !type || !type->IsString()) {
		ctx.Error = L"node without a type";
		return nullptr;
	}

	SizeSpec size = SizeSpec::Star();
	if (auto sizeValue = v.Find("size"); sizeValue && !ParseSize(sizeValue, size)) {
		ctx.Error = L"bad node size";
		return nullptr;
	}
	if (!size.IsStar())
		size.Value = std::max(1.0, std::round(size.Value * ctx.Scale));

	std::unique_ptr<DockNode> node;
	if (type->String == "split") {
		auto axis = v.Find("axis");
		auto children = v.Find("children");
		if (!axis || !axis->IsString() || (axis->String != "h" && axis->String != "v") || !children || !children->IsArray()) {
			ctx.Error = L"bad split";
			return nullptr;
		}
		auto split = std::make_unique<DockSplit>(axis->String == "h" ? Axis::Horizontal : Axis::Vertical);
		for (auto& c : children->Items) {
			auto child = LoadNode(ctx, c, where, depth + 1);
			if (!ctx.Error.empty())
				return nullptr;
			if (child)
				split->m_Children.push_back(std::move(child));
		}
		node = std::move(split);
	}
	else if (type->String == "group") {
		node = LoadGroup(ctx, v, where);
		if (!node)
			return nullptr;
	}
	else {
		ctx.Error = L"unknown node type";
		return nullptr;
	}
	node->Size = size;
	return node;
}

bool DockSerializer::Load(DockLayout& layout, std::string_view text, const LoadOptions& options, std::wstring* error) {
	Context ctx{ layout, options.Factory, {}, {} };
	auto fail = [&](const std::wstring& message) {
		if (error)
			*error = message;
		return false;
	};

	Value root;
	std::string parseError;
	if (!Json::Parse(text, root, &parseError))
		return fail(L"invalid JSON: " + FromUtf8(parseError));
	if (!root.IsObject())
		return fail(L"the layout is not an object");
	int version = 0;
	if (!ParseInt(root.Find("version"), version) || version != FormatVersion)
		return fail(L"unsupported layout version");

	int fileAppVersion = 0;
	ParseInt(root.Find("appVersion"), fileAppVersion);
	if (fileAppVersion < options.MinAppVersion)
		return fail(L"the layout was saved by an older version of the application");

	int fileDpi = 96;
	if (auto dpi = root.Find("dpi")) {
		if (!ParseInt(dpi, fileDpi) || fileDpi < 48 || fileDpi > 960)
			return fail(L"bad dpi");
	}
	ctx.Scale = (double)layout.m_Dpi / fileDpi;

	// build everything on the side; the live layout is only touched once all of it is valid
	auto mainValue = root.Find("main");
	if (!mainValue)
		return fail(L"no main layout");
	auto mainNode = LoadNode(ctx, *mainValue, GroupLocation::Main, 0);
	if (!mainNode)
		return fail(ctx.Error.empty() ? L"invalid main layout" : ctx.Error);
	if (!mainNode->IsSplit())
		return fail(L"the main layout must be a split");
	if (!HasDocumentGroup(*mainNode))
		return fail(L"the main layout has no document group");

	std::vector<std::unique_ptr<DockGroup>> bars[SideCount];
	if (auto barsValue = root.Find("autoHide")) {
		for (int side = 0; side < SideCount; side++) {
			auto list = barsValue->Find(SideNames[side]);
			if (!list)
				continue;
			if (!list->IsArray())
				return fail(L"bad auto-hide list");
			for (auto& item : list->Items) {
				if (!item.IsObject())
					return fail(L"bad auto-hide group");
				auto group = LoadGroup(ctx, item, GroupLocation::AutoHide);
				if (!ctx.Error.empty())
					return fail(ctx.Error);
				if (group)
					bars[side].push_back(std::move(group));
			}
		}
	}

	std::vector<std::unique_ptr<DockFloat>> floats;
	if (auto floatsValue = root.Find("floats")) {
		if (!floatsValue->IsArray())
			return fail(L"bad float list");
		int nextId = 0;
		for (auto& item : floatsValue->Items) {
			int rc[4];
			auto rootValue = item.Find("root");
			if (!ParseInts(item.Find("rect"), rc, 4) || !rootValue)
				return fail(L"bad floating window");
			// a floating window has the DPI of its own monitor: its sizes are kept as they are (files from before
			// there was one had them at the DPI of the file, like the main window)
			int floatDpi = 0;
			const bool hasDpi = ParseInt(item.Find("dpi"), floatDpi) && floatDpi >= 48 && floatDpi <= 960;
			const double savedScale = ctx.Scale;
			if (hasDpi)
				ctx.Scale = 1;
			auto node = LoadNode(ctx, *rootValue, GroupLocation::Float, 1);
			ctx.Scale = savedScale;
			if (!ctx.Error.empty())
				return fail(ctx.Error);
			if (!node || !node->IsSplit())
				continue;
			RECT rect{ rc[0], rc[1], rc[2], rc[3] };
			if (IsRectEmpty(&rect))
				return fail(L"empty floating window rectangle");
			std::unique_ptr<DockFloat> window(new DockFloat(++nextId, rect, hasDpi ? floatDpi : layout.m_Dpi));
			window->m_Root.reset(static_cast<DockSplit*>(node.release()));
			floats.push_back(std::move(window));
		}
	}

	// commit
	layout.m_Root.reset(static_cast<DockSplit*>(mainNode.release()));
	for (int side = 0; side < SideCount; side++)
		layout.m_AutoHide[side] = std::move(bars[side]);
	layout.m_Floats = std::move(floats);
	layout.m_NextFloatId = (int)layout.m_Floats.size();

	std::set<std::wstring> known;		// the panes the file has heard of
	if (auto panesValue = root.Find("panes"); panesValue && panesValue->IsArray()) {
		for (auto& item : panesValue->Items) {
			auto id = item.Find("id");
			if (id && id->IsString())
				known.insert(FromUtf8(id->String));
			auto pane = id && id->IsString() ? layout.FindPane(FromUtf8(id->String)) : nullptr;
			if (!pane)
				continue;
			PaneState state;
			DockSide side;
			int rc[4], preferred[2];
			if (ParseState(item.Find("lastState"), state) && (state != PaneState::Document) == (pane->Kind() == PaneKind::Tool))
				pane->m_LastState = state;
			if (ParseSide(item.Find("lastSide"), side))
				pane->m_LastSide = side;
			if (ParseInts(item.Find("lastFloat"), rc, 4))
				pane->m_LastFloatRect = { rc[0], rc[1], rc[2], rc[3] };
			if (ParseInts(item.Find("preferred"), preferred, 2) && preferred[0] > 0 && preferred[1] > 0)
				pane->PreferredSize = { (int)std::lround(preferred[0] * ctx.Scale), (int)std::lround(preferred[1] * ctx.Scale) };
		}
	}

	layout.Commit();

	if (options.ShowNewPanes) {
		std::vector<DockPane*> fresh;
		for (auto& p : layout.m_Panes)
			if (!p->m_Group && !known.contains(p->Id()))
				fresh.push_back(p.get());
		for (auto p : fresh)
			layout.Show(p);
	}
	return true;
}

//
// dump
//

std::wstring DockSerializer::Dump(const DockLayout& layout) {
	std::wstring out = L"main: ";
	DumpNode(out, *layout.m_Root, false);

	static const wchar_t* const names[] = { L"left", L"right", L"top", L"bottom" };
	for (int side = 0; side < SideCount; side++) {
		if (layout.m_AutoHide[side].empty())
			continue;
		out += std::wstring(L"\nautohide ") + names[side] + L":";
		for (auto& g : layout.m_AutoHide[side]) {
			out += L' ';
			DumpNode(out, *g, false);
			out += L"@" + FormatNumber(g->AutoHideLength);
		}
	}
	for (auto& f : layout.m_Floats) {
		out += std::format(L"\nfloat: ({},{},{},{}) ", f->m_Rect.left, f->m_Rect.top, f->m_Rect.right, f->m_Rect.bottom);
		DumpNode(out, *f->m_Root, false);
	}

	std::wstring hidden;
	for (auto& p : layout.m_Panes)
		if (!p->m_Group)
			hidden += (hidden.empty() ? L"" : L" ") + p->Id();
	if (!hidden.empty())
		out += L"\nhidden: " + hidden;
	return out;
}

std::string DockLayout::Save() const {
	return DockSerializer::Save(*this);
}

bool DockLayout::Load(std::string_view text, const LoadOptions& options, std::wstring* error) {
	return DockSerializer::Load(*this, text, options, error);
}

std::wstring DockLayout::Dump() const {
	return DockSerializer::Dump(*this);
}

}
