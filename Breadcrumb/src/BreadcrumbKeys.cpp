#include "BreadcrumbData.h"

#include <algorithm>

using namespace BrcDraw;

namespace {
	RECT PartRect(const BrcLayout& l, const BrcHit& part) {
		auto at = [](const std::vector<RECT>& v, int i) {
			return i >= 0 && i < (int)v.size() ? v[i] : RECT{};
		};
		switch (part.part) {
			case BrcPart::Back: return l.back;
			case BrcPart::Forward: return l.forward;
			case BrcPart::Recent: return l.recent;
			case BrcPart::Up: return l.up;
			case BrcPart::Overflow: return l.overflow;
			case BrcPart::Text: return at(l.text, part.index);
			case BrcPart::Chevron: return at(l.chevron, part.index);
			case BrcPart::History: return l.history;
			case BrcPart::Action: return at(l.actions, part.index);
			default: return {};
		}
	}

	bool Drops(const BrcHit& part) {
		return part.part == BrcPart::Chevron || part.part == BrcPart::Overflow || part.part == BrcPart::Recent;
	}

	// the keys' part in order, or the last segment's text if it isn't there (else the first part); -1 if none
	int FocusIndex(const std::vector<BrcHit>& order, const BrcHit& focus) {
		auto it = std::find(order.begin(), order.end(), focus);
		if (it != order.end())
			return (int)(it - order.begin());
		for (int k = (int)order.size() - 1; k >= 0; k--)
			if (order[k].part == BrcPart::Text)
				return k;
		return order.empty() ? -1 : 0;
	}
}

bool CBreadcrumbCtrl::Enabled(const BrcHit& part) {
	auto& d = *m_Data;
	switch (part.part) {
		case BrcPart::Back: return CanGo(-1);
		case BrcPart::Forward: return CanGo(1);
		case BrcPart::Recent: return d.backForward.Size() > 1;
		case BrcPart::Up: return d.segments.size() > 1;
		case BrcPart::History: return !(GetStyle() & BRCS_NOEDIT) && !d.history.Paths().empty();
		case BrcPart::Action: return part.index >= 0 && part.index < (int)d.buttons.size() && !(d.buttons[part.index].state & BRCBS_DISABLED);
		case BrcPart::Text:
		case BrcPart::Chevron:
		case BrcPart::Overflow:
			return true;
		default:
			return false;
	}
}

std::vector<BrcHit> CBreadcrumbCtrl::FocusOrder() {
	EnsureLayout();
	auto& d = *m_Data;
	auto& l = d.layout;
	std::vector<BrcHit> order;
	auto add = [&](BrcHit part) {
		RECT rc = PartRect(l, part);
		if (!::IsRectEmpty(&rc) && Enabled(part))
			order.push_back(part);
	};
	add({ BrcPart::Back });
	add({ BrcPart::Forward });
	add({ BrcPart::Recent });
	add({ BrcPart::Up });
	if (!d.editing) {
		add({ BrcPart::Overflow });
		for (int i = l.firstVisible; i < (int)d.segments.size(); i++) {
			add({ BrcPart::Text, i });
			add({ BrcPart::Chevron, i });
		}
	}
	add({ BrcPart::History });
	for (int i = 0; i < (int)d.buttons.size(); i++)
		add({ BrcPart::Action, i });
	return order;
}

void CBreadcrumbCtrl::SetKeyFocus(const BrcHit& part) {
	m_Data->keyFocus = part;
	Invalidate(FALSE);
}

bool CBreadcrumbCtrl::KeyDown(UINT vk) {
	auto& d = *m_Data;
	auto order = FocusOrder();
	int i = FocusIndex(order, d.keyFocus);
	if (i < 0)
		return false;
	d.keyFocus = order[i];
	switch (vk) {
		case VK_LEFT:
			SetKeyFocus(order[std::max(0, i - 1)]);
			return true;
		case VK_RIGHT:
			SetKeyFocus(order[std::min((int)order.size() - 1, i + 1)]);
			return true;
		case VK_HOME:
			SetKeyFocus(order.front());
			return true;
		case VK_END:
			SetKeyFocus(order.back());
			return true;
		case VK_RETURN:
		case VK_SPACE:
			Activate(order[i]);
			return true;
		case VK_DOWN:
		case VK_F4: {
			// a segment's list (Down on its text too), the folded ones, the recent locations; else F4: the history
			auto part = order[i];
			if (part.part == BrcPart::Text) {
				BrcHit chevron{ BrcPart::Chevron, part.index };
				if (std::find(order.begin(), order.end(), chevron) != order.end())
					part = chevron;
			}
			if (Drops(part)) {
				SetKeyFocus(part);
				OpenDropDown(part, false, true);
			}
			else if (vk == VK_F4)
				ShowHistory();
			return true;
		}
	}
	return false;
}

void CBreadcrumbCtrl::Activate(const BrcHit& part) {
	switch (part.part) {
		case BrcPart::Text: Navigate(BRCNS_SEGMENT, part.index, -1); break;
		case BrcPart::Chevron:
		case BrcPart::Overflow:
		case BrcPart::Recent:
			OpenDropDown(part, false, true);
			break;
		case BrcPart::Back: Go(-1); break;
		case BrcPart::Forward: Go(1); break;
		case BrcPart::Up: GoUp(); break;
		case BrcPart::History: ShowHistory(); break;
		case BrcPart::Action: ClickButton(part.index); break;
		default: break;
	}
}

bool CBreadcrumbCtrl::DropAdjacent(int step) {
	auto& d = *m_Data;
	std::vector<BrcHit> drops;
	for (auto& part : FocusOrder())
		if (Drops(part))
			drops.push_back(part);
	auto it = std::find(drops.begin(), drops.end(), d.opened);
	if (it == drops.end() || drops.size() < 2)
		return false;
	// around past either end, as a menu bar
	const int n = (int)drops.size();
	auto next = drops[((int)(it - drops.begin()) + step + n) % n];
	SetKeyFocus(next);
	OpenDropDown(next, false, true);
	return true;
}

void CBreadcrumbCtrl::PaintFocus(WTL::CDCHandle dc, const BRCCOLORS& c) {
	auto& d = *m_Data;
	if (::GetFocus() != m_hWnd || d.editing)
		return;
	auto order = FocusOrder();
	int i = FocusIndex(order, d.keyFocus);
	if (i < 0)
		return;
	RECT rc = PartRect(d.layout, order[i]);
	::InflateRect(&rc, 0, -Scale(2));
	WTL::CPen pen;
	pen.CreatePen(PS_INSIDEFRAME, std::max(1, Scale(1)), c.crAccent);
	auto oldPen = dc.SelectPen(pen);
	auto oldBrush = dc.SelectBrush((HBRUSH)::GetStockObject(NULL_BRUSH));
	dc.RoundRect(&rc, { Scale(6), Scale(6) });
	dc.SelectBrush(oldBrush);
	dc.SelectPen(oldPen);
}
