#pragma once

#include <windows.h>
#include <oleacc.h>
#include <atlbase.h>

#include <functional>
#include <string>
#include <vector>

namespace WTLDock {

// One thing a docking window draws that a screen reader should know about: a caption, a button, a tab.
struct AccElement {
	std::wstring Name;
	LONG Role{ ROLE_SYSTEM_CLIENT };
	LONG State{};				// STATE_SYSTEM_*
	RECT Screen{};				// empty: not visible (a tab scrolled out of the strip)
	std::wstring Action;		// what the default action is called; empty: there is none
	std::function<void()> Invoke;	// the default action
	std::function<void()> Select;	// accSelect: making a tab the active one
};

// What the accessible object of a docking window asks of the window. The lists are made anew for every question:
// the windows draw their own chrome, so what is there is whatever the layout says now.
class IDockAccessibleOwner {
public:
	virtual HWND AccWindow() const = 0;
	virtual std::wstring AccName() const = 0;
	virtual LONG AccRole() const = 0;
	// the children that are not windows: caption, buttons, tabs (numbered from 1 in this order)
	virtual std::vector<AccElement> AccElements() const = 0;
	// the child windows that belong in the tree (numbered after the elements)
	virtual std::vector<HWND> AccChildWindows() const = 0;
};

//
// The IAccessible (MSAA) object of the client area of a group window or of the host: the window's own children
// (the content) plus the elements the owner draws. Properties of the window itself come from the standard object.
// Reference counted; when the window goes, the owner calls Detach and the object answers with errors from then on.
//
class DockAccessible final : public IAccessible {
public:
	static DockAccessible* Create(IDockAccessibleOwner* owner);
	void Detach();

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;

	// IDispatch
	STDMETHODIMP GetTypeInfoCount(UINT* count) override;
	STDMETHODIMP GetTypeInfo(UINT index, LCID lcid, ITypeInfo** info) override;
	STDMETHODIMP GetIDsOfNames(REFIID riid, LPOLESTR* names, UINT count, LCID lcid, DISPID* ids) override;
	STDMETHODIMP Invoke(DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS* params, VARIANT* result, EXCEPINFO* excep, UINT* argErr) override;

	// IAccessible
	STDMETHODIMP get_accParent(IDispatch** parent) override;
	STDMETHODIMP get_accChildCount(long* count) override;
	STDMETHODIMP get_accChild(VARIANT child, IDispatch** disp) override;
	STDMETHODIMP get_accName(VARIANT child, BSTR* name) override;
	STDMETHODIMP get_accValue(VARIANT child, BSTR* value) override;
	STDMETHODIMP get_accDescription(VARIANT child, BSTR* description) override;
	STDMETHODIMP get_accRole(VARIANT child, VARIANT* role) override;
	STDMETHODIMP get_accState(VARIANT child, VARIANT* state) override;
	STDMETHODIMP get_accHelp(VARIANT child, BSTR* help) override;
	STDMETHODIMP get_accHelpTopic(BSTR* helpFile, VARIANT child, long* topic) override;
	STDMETHODIMP get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override;
	STDMETHODIMP get_accFocus(VARIANT* child) override;
	STDMETHODIMP get_accSelection(VARIANT* children) override;
	STDMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override;
	STDMETHODIMP accSelect(long flags, VARIANT child) override;
	STDMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override;
	STDMETHODIMP accNavigate(long direction, VARIANT start, VARIANT* end) override;
	STDMETHODIMP accHitTest(long left, long top, VARIANT* child) override;
	STDMETHODIMP accDoDefaultAction(VARIANT child) override;
	STDMETHODIMP put_accName(VARIANT child, BSTR name) override;
	STDMETHODIMP put_accValue(VARIANT child, BSTR value) override;

private:
	DockAccessible(IDockAccessibleOwner* owner, HWND window);
	~DockAccessible() = default;

	struct Snapshot {
		std::vector<AccElement> Elements;
		std::vector<HWND> Windows;
	};
	enum class Kind { Invalid, Self, Element, Window };

	bool Alive() const;
	Snapshot Take() const;
	// what a child id refers to: the element (index into Snapshot::Elements) or the window (index into Windows)
	static Kind Resolve(const VARIANT& child, const Snapshot& s, size_t& index);
	static HRESULT ChildVariant(const Snapshot& s, size_t zeroBasedChild, VARIANT* out);

	LONG m_Ref{ 1 };
	IDockAccessibleOwner* m_Owner;
	HWND m_Window;
	CComPtr<IAccessible> m_Std;
};

}
