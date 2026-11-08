#pragma once

// Function plot: draws math as people type it ("y = x^2", "2 sin x", "r = 1 + cos 3theta", "(cos t, sin t)",
// "x^2 + y^2 < 4") with Direct2D: functions, polar and parametric curves, implicit curves, inequalities and points,
// over a grid with labeled axes. The user pans by dragging, zooms with the wheel, traces a graph (its roots, extrema
// and intersections marked), toggles graphs in the legend and moves the parameters' sliders. The API follows the
// common controls: C-style structures and WM_NOTIFY codes (FPLN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>

#include <memory>

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_FUNCTIONPLOT[] = L"WC_FunctionPlot";

// ---- Styles (the window's low word) ------------------------------------------

#define FPLS_NOLEGEND      0x0001   // no list of the graphs
#define FPLS_NOSLIDERS     0x0002   // no sliders for the parameters
#define FPLS_NOGRID        0x0004   // the axes only
#define FPLS_NOAXES        0x0008   // no axes, ticks or labels
#define FPLS_NOPANZOOM     0x0010   // the user can't move the view
#define FPLS_NOTRACE       0x0020   // no tracing
#define FPLS_EQUALSCALE    0x0040   // x and y always at the same scale (circles look round)
#define FPLS_PITICKS       0x0080   // the x axis in multiples of pi

// ---- Graphs -------------------------------------------------------------------

#define FPLGM_TEXT         0x0001
#define FPLGM_COLOR        0x0002
#define FPLGM_WIDTH        0x0004
#define FPLGM_FLAGS        0x0008
#define FPLGM_RANGE        0x0010
#define FPLGM_LPARAM       0x0020
#define FPLGM_ALL          0x003F

#define FPLGF_HIDDEN       0x0001   // not drawn (the legend shows it grayed; a click there shows it)
#define FPLGF_DASHED       0x0002   // dashed (a strict inequality's boundary always is)
#define FPLGF_NOTRACE      0x0004
#define FPLGF_NOFEATURES   0x0008   // no roots, extrema or intersections marked while tracing it

struct FPLGRAPH {
	UINT     mask;             // FPLGM_*: the members to set, or to fill
	UINT     id;               // the app's, unique; how a graph is named in the API
	LPCWSTR  pszText;          // the expression; GetGraph: valid until the graph changes
	COLORREF color;            // CLR_DEFAULT: the palette's color for its place
	float    width;            // DIPs (2.5)
	UINT     flags;            // FPLGF_*
	double   tMin, tMax;       // FPLGM_RANGE: a parametric curve's t or a polar curve's theta (0 to 2 pi)
	LPARAM   lParam;
};

// What a graph is, once the control has read it.
enum FPLKIND {
	FPLK_FUNCTION,             // y = f(x)
	FPLK_FUNCTIONOFY,          // x = f(y)
	FPLK_POLAR,                // r = f(theta)
	FPLK_PARAMETRIC,           // (x(t), y(t))
	FPLK_IMPLICIT,             // F(x, y) = 0, or an inequality
	FPLK_POINT,                // (a, b)
	FPLK_DEFINITION,           // "c = 2a": defines c for the other graphs
};

// The errors (FPLGRAPHINFO::error), FplErrorCode's values.
enum FPLERROR {
	FPLE_NONE, FPLE_EMPTY, FPLE_UNEXPECTEDCHAR, FPLE_BADNUMBER, FPLE_UNEXPECTEDTOKEN, FPLE_UNEXPECTEDEND,
	FPLE_MISSINGCLOSE, FPLE_UNKNOWNNAME, FPLE_ARGCOUNT, FPLE_MISSINGARGUMENT, FPLE_TOOMANYRELATIONS,
	FPLE_MISPLACEDRELATION, FPLE_MISPLACEDPOINT, FPLE_MISPLACEDCONDITION, FPLE_VARIABLENOTALLOWED, FPLE_NOTAPLOT,
	FPLE_BADPOINT, FPLE_NOTDIFFERENTIABLE, FPLE_REDEFINED, FPLE_INTERNAL,
};

struct FPLGRAPHINFO {
	FPLKIND  kind;
	FPLERROR error;            // FPLE_NONE: it reads
	int      errorPos;         // the part of the text the error is about (UTF-16 offsets)
	int      errorLen;
	WCHAR    szError[128];     // the message
	WCHAR    szName[64];       // the name it's about (FPLE_UNKNOWNNAME: one an app may add as a parameter); a definition's name
	double   value;            // FPLK_DEFINITION: its value
	COLORREF color;            // the color it's drawn in
};

// ---- Parameters ------------------------------------------------------------------

#define FPLPF_NOSLIDER     0x0001   // no slider for it

struct FPLPARAM {
	LPCWSTR pszName;           // a name the expressions use ("a", "k", "alpha")
	double  value;
	double  min, max;          // the slider's range
	double  step;              // 0: any value
	UINT    flags;             // FPLPF_*
};

// ---- The view -----------------------------------------------------------------------

struct FPLVIEW {
	double xMin, xMax, yMin, yMax;
};

// ---- Colors ---------------------------------------------------------------------------

#define FPL_PALETTE 8

struct FPLCOLORS {
	COLORREF crBack;
	COLORREF crGrid;               // the major lines
	COLORREF crGridMinor;
	COLORREF crAxis;
	COLORREF crLabel;              // the ticks' numbers
	COLORREF crPanelBack;          // the legend and the sliders
	COLORREF crPanelText;
	COLORREF crPanelBorder;
	COLORREF crError;
	COLORREF crPalette[FPL_PALETTE];   // the graphs', by their place
};

// ---- Features and tracing -----------------------------------------------------------

enum FPLFEATUREKIND {
	FPLF_ROOT,
	FPLF_MINIMUM,
	FPLF_MAXIMUM,
	FPLF_INTERSECTION,
	FPLF_YINTERCEPT,
	FPLF_NONE = -1,
};

struct FPLFEATURE {
	FPLFEATUREKIND kind;
	double x, y;
};

struct FPLTRACE {
	UINT           id;             // the graph traced; 0 with valid FALSE: nothing is
	BOOL           valid;
	double         x, y;
	FPLFEATUREKIND feature;        // the point is one of the graph's features, else FPLF_NONE
};

// ---- Notifications --------------------------------------------------------------------

#define FPLN_FIRST          (0U - 3450U)   // outside the common controls ranges
#define FPLN_VIEWCHANGED    (FPLN_FIRST - 0)   // NMFPLVIEW; the user panned or zoomed
#define FPLN_PARAMCHANGED   (FPLN_FIRST - 1)   // NMFPLPARAM; the user moved a slider
#define FPLN_TRACE          (FPLN_FIRST - 2)   // NMFPLTRACE; the traced point changed (or tracing stopped)
#define FPLN_GRAPHTOGGLED   (FPLN_FIRST - 3)   // NMFPLGRAPH; the user showed or hid a graph in the legend

struct NMFPLVIEW {
	NMHDR   hdr;
	FPLVIEW view;
};

struct NMFPLPARAM {
	NMHDR   hdr;
	LPCWSTR pszName;
	double  value;
};

struct NMFPLTRACE {
	NMHDR    hdr;
	FPLTRACE trace;
};

struct NMFPLGRAPH {
	NMHDR hdr;
	UINT  id;
	BOOL  hidden;
};

// ---- Control class --------------------------------------------------------------------------

struct FplState;

class CFunctionPlotCtrl : public ATL::CWindowImpl<CFunctionPlotCtrl> {
public:
	DECLARE_WND_CLASS_EX(L"WC_FunctionPlot", CS_DBLCLKS, -1)

	CFunctionPlotCtrl();
	~CFunctionPlotCtrl();

	// sent to every window by WTLHelper when its dark mode is switched (registered by name: no dependency)
	inline static const UINT ThemeChangedMessage = ::RegisterWindowMessageW(L"ThemeChanged");

	BEGIN_MSG_MAP(CFunctionPlotCtrl)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_STYLECHANGED, OnStyleChanged)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_SETFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnFocus)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_CAPTURECHANGED, OnCaptureChanged)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
		MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnColorsChanged)
		MESSAGE_HANDLER(WM_THEMECHANGED, OnColorsChanged)
		MESSAGE_HANDLER(ThemeChangedMessage, OnColorsChanged)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
	END_MSG_MAP()

	// ---- Graphs ----
	BOOL AddGraph(const FPLGRAPH& graph);      // false if the id is taken (or 0)
	BOOL SetGraph(const FPLGRAPH& graph);      // the masked members of the graph with the id
	BOOL GetGraph(FPLGRAPH& graph) const;      // the masked members of the graph with the id
	BOOL GetGraphInfo(UINT id, FPLGRAPHINFO& info) const;
	BOOL RemoveGraph(UINT id);
	void RemoveAllGraphs();
	int GetGraphCount() const;
	UINT GetGraphId(int index) const;          // 0 if there's none
	BOOL MoveGraph(UINT id, int index);        // its place: the legend's order and the palette's color
	BOOL ShowGraph(UINT id, BOOL show);

	// ---- Parameters ----
	BOOL SetParameter(const FPLPARAM& param);  // adds it, or changes the one with the name
	BOOL GetParameter(LPCWSTR pszName, FPLPARAM& param) const;   // pszName in param points at the control's copy
	BOOL SetParameterValue(LPCWSTR pszName, double value);
	double GetParameterValue(LPCWSTR pszName) const;             // NaN if there's none
	BOOL RemoveParameter(LPCWSTR pszName);
	int GetParameterCount() const;
	BOOL GetParameterAt(int index, FPLPARAM& param) const;
	void SetDegrees(BOOL degrees);             // the trigonometric functions in degrees
	BOOL GetDegrees() const;

	// ---- The view ----
	void SetView(const FPLVIEW& view);         // with FPLS_EQUALSCALE, grown to equal scales
	FPLVIEW GetView() const;
	void SetHomeView(const FPLVIEW* view);     // where ResetView (and Home) goes; null: x -10 to 10, y to match
	void ResetView();
	void Zoom(double factor);                  // about the center; > 1 zooms in
	BOOL ClientToPlane(POINT pt, double& x, double& y) const;
	POINT PlaneToClient(double x, double y) const;

	// ---- Tracing and features ----
	BOOL GetTrace(FPLTRACE& trace) const;
	void ClearTrace();
	// A function's roots, extrema, y intercept and intersections with the other functions over the view; the
	// number there are (up to max filled).
	int GetFeatures(UINT id, FPLFEATURE* features, int max) const;

	// ---- Look ----
	void SetColors(const FPLCOLORS* colors);   // null: back to the system's (and the dark mode's)
	FPLCOLORS GetColors() const;
	// high contrast: the system's colors only, over the app's
	void ForceHighContrast(int value);         // testing: -1 the system's setting, 0 off, 1 on
	bool IsHighContrast() const;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnStyleChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);

	// FunctionPlotCtrl.cpp
	void Rebuild();                            // texts, names or the angle unit changed
	void ValuesChanged();                      // parameters' values changed
	void ViewChanged(bool notify);             // the view moved: sample again
	void Resample();
	void SetViewport(double xMin, double xMax, double yMin, double yMax);
	void UpdateTrace(POINT pt, bool dragging);
	void SetTraceHit(int graph, int feature, double x, double y, bool valid);
	void NotifyTrace();
	void NotifyView();
	float ToDips(int pixels) const;
	struct FplPanelHit HitPanel(POINT pt) const;
	void DragSlider(int param, float x);
	COLORREF GraphColor(int index) const;

	// FunctionPlotPaint.cpp
	void Paint();
	bool CreateTarget();
	void DiscardTarget();
	void BuildGeometries();                    // the sampled lines and fills as Direct2D geometries
	void LayoutPanel();                        // the legend's and sliders' rows
	void PaintGrid();
	void PaintGraphs();
	void PaintLabels();
	void PaintTrace();
	void PaintPanel();

	std::unique_ptr<FplState> m_State;
};
