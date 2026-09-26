#pragma once

#include "DockLayout.h"
#include "Json.h"
#include <set>

namespace WTLDock {

// Layout persistence and the text dump. A friend of the model classes so that it can build trees directly.
struct DockSerializer {
	static std::string Save(const DockLayout& layout);
	static bool Load(DockLayout& layout, std::string_view text, const PaneFactory& factory, std::wstring* error);
	static std::wstring Dump(const DockLayout& layout);

private:
	struct Context {
		DockLayout& Layout;
		const PaneFactory& Factory;
		std::set<DockPane*> Used;
		std::wstring Error;
		double Scale{ 1 };		// current DPI / DPI of the file, applied to pixel sizes
	};

	static Json::Value SaveNode(const DockNode& node);
	static Json::Value SaveGroup(const DockGroup& group);
	static std::unique_ptr<DockNode> LoadNode(Context& ctx, const Json::Value& v, GroupLocation where, int depth);
	static std::unique_ptr<DockGroup> LoadGroup(Context& ctx, const Json::Value& v, GroupLocation where);
	static DockPane* ResolvePane(Context& ctx, const std::string& id);
};

}
