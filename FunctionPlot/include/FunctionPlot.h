#pragma once

// Function plot: draws math as people type it ("y = x^2", "2 sin x", "r = 1 + cos 3theta", "(cos t, sin t)",
// "x^2 + y^2 < 4", "f(x) = x^2", "y = d/dx f(x)", "z = x y", "(-y, x)") with Direct2D: functions, polar and parametric
// curves, implicit curves, inequalities, points, surfaces (contours, heat maps) and vector fields, over a grid with
// labeled axes. The user pans by dragging, zooms with the wheel, traces a graph (its roots, extrema
// and intersections marked), toggles graphs in the legend and moves the parameters' sliders. The API follows the
// common controls: C-style structures and WM_NOTIFY codes (FPLN_*) sent to the parent.

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>
#include <atlstr.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

struct ID2D1RenderTarget;

// ---- Window class name -------------------------------------------------------

constexpr wchar_t WC_FUNCTIONPLOT[] = L"WC_FunctionPlot";

// ---- Styles (the window's low word) ------------------------------------------

// In a graph's text theta can be typed as the Greek letter, the word, or q. q is a name of its own when it's a parameter, a
// line defines it, or its line has x, y or another variable (y = q x), a regression (~) or an ODE.

#define FPLS_NOLEGEND      0x0001   // no list of the graphs
#define FPLS_NOSLIDERS     0x0002   // no sliders for the parameters
#define FPLS_NOGRID        0x0004   // the axes only
#define FPLS_NOAXES        0x0008   // no axes, ticks or labels
#define FPLS_NOPANZOOM     0x0010   // the user can't move the view
#define FPLS_NOTRACE       0x0020   // no tracing
#define FPLS_EQUALSCALE    0x0040   // x and y always at the same scale (circles look round)
#define FPLS_PITICKS       0x0080   // the x axis in multiples of pi
#define FPLS_TANGENT       0x0100   // the tangent at a traced point of a function, and its slope
#define FPLS_NOCOLORSCALE  0x0200   // no color scale beside a heat map
#define FPLS_NOANIMATION   0x0800   // undo, redo, Home, F and zooming to a rectangle jump instead of gliding (as they do when
                                    // Windows' client area animations are off)
#define FPLS_ALWAYSANIMATE 0x1000   // they glide even when Windows' client area animations are off
#define FPLS_POLARGRID     0x0400   // circles and spokes for the grid, labeled by radius and angle (not on log axes, or in 3D)

// ---- Graphs -------------------------------------------------------------------

#define FPLGM_TEXT         0x0001
#define FPLGM_COLOR        0x0002
#define FPLGM_WIDTH        0x0004
#define FPLGM_FLAGS        0x0008
#define FPLGM_RANGE        0x0010
#define FPLGM_LPARAM       0x0020
#define FPLGM_LABEL        0x0040
#define FPLGM_OPACITY      0x0080
#define FPLGM_ALL          0x00FF

#define FPLGF_HIDDEN       0x0001   // not drawn (the legend shows it grayed; a click there shows it)
#define FPLGF_DASHED       0x0002   // dashed (a strict inequality's boundary always is)
#define FPLGF_NOTRACE      0x0004
#define FPLGF_NOFEATURES   0x0008   // no roots, extrema or intersections marked while tracing it
#define FPLGF_HEATMAP      0x0010   // a surface (z = F(x, y)): its values as colors
#define FPLGF_NOCONTOURS   0x0020   // a surface: no contour lines
#define FPLGF_NODRAG       0x0040   // a point like (a, b), a and b parameters, can't be dragged (it can by default;
                                    // dragging sets the parameters: FPLN_PARAMCHANGED)
#define FPLGF_DOTTED       0x0080   // a dotted line
#define FPLGF_HOLLOW       0x0100   // points as rings
#define FPLGF_MESHLINES    0x0200   // 3D: lines on a surface: z = F(x, y) in x and y at the box's ticks, a parametric
                                    // surface along its u and v, an implicit one in x, y and z
#define FPLGF_LEVELLINES   0x0400   // 3D: lines on a surface at heights
#define FPLGF_NOSINGULAR   0x0800   // y = f(x), x = f(y): no dashed lines at asymptotes, no open circles at holes

struct FPLGRAPH {
	UINT     mask;             // FPLGM_*: the members to set, or to fill
	UINT     id;               // the app's, unique; how a graph is named in the API
	LPCWSTR  pszText;          // the expression; GetGraph: valid until the graph changes
	COLORREF color;            // CLR_DEFAULT: the palette's color for its place
	float    width;            // DIPs (2.5)
	UINT     flags;            // FPLGF_*
	double   tMin, tMax;       // FPLGM_RANGE: a parametric curve's t or a polar curve's theta (0 to 2 pi)
	LPARAM   lParam;
	LPCWSTR  pszLabel;         // FPLGM_LABEL: beside its points, or at its curve's right end in the view; {x} and
	                           // {y} are its coordinates ("A({x}, {y})"); GetGraph: valid until the graph changes
	float    opacity;          // FPLGM_OPACITY: its lines' and points' (1); in 3D the whole graph's, drawn see-through
	                           // in any order; and
	float    fillOpacity;      // its regions' and heat maps' (negative: 0.22, a heat map 0.85); 2D only
};

// What a graph is, once the control has read it.
enum FPLKIND {
	FPLK_FUNCTION,             // y = f(x)
	FPLK_FUNCTIONOFY,          // x = f(y)
	FPLK_POLAR,                // r = f(theta)
	FPLK_PARAMETRIC,           // (x(t), y(t))
	FPLK_IMPLICIT,             // F(x, y) = 0, or an inequality
	FPLK_POINT,                // (a, b)
	FPLK_DEFINITION,           // "c = 2a": defines c for the other graphs; "g(a, b) = a b" (a function of others);
	                           // "u1 = x - 1", a substitution: the name stands for that, in the graphs
	FPLK_SURFACE,              // z = F(x, y): contours and, with FPLGF_HEATMAP, a heat map
	FPLK_VECTORFIELD,          // (P(x, y), Q(x, y)): arrows
	FPLK_SPACECURVE,           // (x(t), y(t), z(t)): 3D only
	FPLK_PARAMSURFACE,         // (x(u, v), y(u, v), z(u, v)): 3D only; u and v over the graph's range
	FPLK_POINT3D,              // (a, b, c): 3D only
	FPLK_IMPLICITSURFACE,      // F(x, y, z) = 0 ("x^2 + y^2 + z^2 = 25"): 3D only
	FPLK_INTEGRAL,             // integral(f, lo, hi) alone: the area shaded, its value (FPLGRAPHINFO::value)
	FPLK_REGRESSION,           // y1 ~ m x1 + b: m and b fitted to lists (GetValue); FPLGRAPHINFO::value is R squared
	FPLK_BETWEEN,              // f(x) < y < g(x) or f(y) < x < g(y): the region between two curves
	FPLK_SLOPEFIELD,           // y' = F(x, y): its slopes; with ", y(a) = b" (any number) the solutions through (a, b) too
	                           // (a and b parameters alone: the point can be dragged)
	FPLK_SECONDORDER,          // y'' = F(x, y, y') and on to y''''', ", y(a) = b, y'(a) = c ..." (any number): the solutions
	FPLK_ODESYSTEM,            // x' = P, y' = Q (of x, y, t), ", (a, b)" (any number): without t arrows, and trajectories
	                           // through the points from t = 0 (FPLGM_RANGE: only over that t)
	FPLK_ODESYSTEM3D,          // x' = P, y' = Q, z' = R (of x, y, z, t), ", (a, b, c)": trajectories as tubes: 3D only
};

// 2D draws the plane; 3D a box the user orbits (Direct3D 11), with surfaces, space curves and points, and the 2D
// curves and points on its z = 0 plane.
enum FPLMODE {
	FPLM_2D,
	FPLM_3D,
};

struct FPLVIEW3D {
	double xMin, xMax, yMin, yMax, zMin, zMax;
	float  yaw;                // radians around z from the +x axis
	float  pitch;              // radians above the xy plane
};

// The errors (FPLGRAPHINFO::error), FplErrorCode's values.
enum FPLERROR {
	FPLE_NONE, FPLE_EMPTY, FPLE_UNEXPECTEDCHAR, FPLE_BADNUMBER, FPLE_UNEXPECTEDTOKEN, FPLE_UNEXPECTEDEND,
	FPLE_MISSINGCLOSE, FPLE_UNKNOWNNAME, FPLE_ARGCOUNT, FPLE_MISSINGARGUMENT, FPLE_TOOMANYRELATIONS,
	FPLE_MISPLACEDRELATION, FPLE_MISPLACEDPOINT, FPLE_MISPLACEDCONDITION, FPLE_VARIABLENOTALLOWED, FPLE_NOTAPLOT,
	FPLE_BADPOINT, FPLE_NOTDIFFERENTIABLE, FPLE_REDEFINED, FPLE_LISTNOTALLOWED, FPLE_NOTALIST, FPLE_BADINITIAL, FPLE_ODEORDER, FPLE_RESERVEDNAME, FPLE_BADINDEX, FPLE_INTERNAL,
};

struct FPLGRAPHINFO {
	FPLKIND  kind;
	FPLERROR error;            // FPLE_NONE: it reads
	int      errorPos;         // the part of the text the error is about (UTF-16 offsets)
	int      errorLen;
	WCHAR    szError[128];     // the message
	WCHAR    szName[64];       // the name it's about (FPLE_UNKNOWNNAME: one an app may add as a parameter); a definition's name
	double   value;            // FPLK_DEFINITION, FPLK_INTEGRAL: its value; FPLK_REGRESSION: R squared
	COLORREF color;            // the color it's drawn in
	BOOL     incomplete;       // an ODE's solution ran out of its step budget before the view's edge (the legend says so)
};

// ---- Parameters ------------------------------------------------------------------

#define FPLPF_NOSLIDER     0x0001   // no slider for it
#define FPLPF_AUTO         0x0002   // not chosen by name, but added because a graph needed it: if it's q, a line with r and no
                                    // other variable still reads q as theta

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
	BOOL           hasZ;           // z is: a surface's value under the mouse (2D), the point's z (3D)
	double         z;
	BOOL           hasSlope;       // a function's slope there (dy/dx; dx/dy for x = f(y))
	double         slope;
};

// ---- Notifications --------------------------------------------------------------------

#define FPLN_FIRST          (0U - 3450U)   // outside the common controls ranges
#define FPLN_VIEWCHANGED    (FPLN_FIRST - 0)   // NMFPLVIEW; the user panned or zoomed
#define FPLN_PARAMCHANGED   (FPLN_FIRST - 1)   // NMFPLPARAM; the user moved a slider
#define FPLN_TRACE          (FPLN_FIRST - 2)   // NMFPLTRACE; the traced point changed (or tracing stopped)
#define FPLN_GRAPHTOGGLED   (FPLN_FIRST - 3)   // NMFPLGRAPH; the user showed or hid a graph in the legend
#define FPLN_VIEW3DCHANGED  (FPLN_FIRST - 4)   // NMFPLVIEW3D; the user orbited, panned or zoomed the 3D view

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

struct NMFPLVIEW3D {
	NMHDR     hdr;
	FPLVIEW3D view;
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
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_COPY, OnCopy)
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
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
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_FPL_PAINT3D, OnPaint3D)
		MESSAGE_HANDLER(WM_FPL_SAMPLED, OnSampled)
	END_MSG_MAP()

	enum : UINT_PTR { AnimationTimer = 1, TransitionTimer = 2 };
	enum : UINT {
		WM_FPL_PAINT3D = WM_USER + 130,        // the 3D view's window asks for a frame
		WM_FPL_SAMPLED = WM_USER + 131,        // the sampling thread has results
	};

	// in 3D the frames are drawn in a window of their own over the control: invalidating the control redraws it too
	BOOL Invalidate(BOOL bErase = TRUE);

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
	// a parameter's value sweeping its slider's range and back (4 seconds a way), as its play button does
	BOOL AnimateParameter(LPCWSTR pszName, BOOL play);
	BOOL IsParameterAnimating(LPCWSTR pszName) const;
	void SetDegrees(BOOL degrees);             // the trigonometric functions in degrees
	// Any name's value now: a parameter, a definition ("c = 2a"), a fitted name, an aggregate ("mean(L)"); NaN if
	// there's none.
	double GetValue(LPCWSTR pszName) const;

	// ---- Lists (a table's columns) ----
	// A list of numbers the expressions use by name: (x1, y1) is a point per element, y1 ~ m x1 + b fits m and b,
	// mean(x1) is one number. Adds it, or replaces the values of the one with the name.
	BOOL SetList(LPCWSTR pszName, const double* values, int count);
	int GetList(LPCWSTR pszName, double* values, int max) const;   // its count (a defined list's too); up to max filled
	BOOL RemoveList(LPCWSTR pszName);
	int GetListCount() const;                  // the app's lists
	LPCWSTR GetListName(int index) const;
	BOOL GetDegrees() const;

	// ---- The view ----
	void SetView(const FPLVIEW& view);         // with FPLS_EQUALSCALE, grown to equal scales
	FPLVIEW GetView() const;
	// Log scales (base 10) on x and y: ranges that aren't positive become 3 decades under their top (or 0.1 to 100).
	// Not in 3D; no equal scales on a log axis.
	void SetLogAxes(BOOL logX, BOOL logY);
	BOOL IsLogX() const;
	BOOL IsLogY() const;
	void SetHomeView(const FPLVIEW* view);     // where ResetView (and Home) goes; null: x -10 to 10, y to match
	void ResetView(BOOL animate = FALSE);      // animate: it glides there, then FPLN_VIEWCHANGED
	void Zoom(double factor);                  // about the center; > 1 zooms in
	BOOL ClientToPlane(POINT pt, double& x, double& y) const;
	POINT PlaneToClient(double x, double y) const;

	// ---- 3D ----
	BOOL SetMode(FPLMODE mode);                // FALSE if 3D can't start (no Direct3D 11, not even WARP)
	FPLMODE GetMode() const;
	void SetView3D(const FPLVIEW3D& view);
	FPLVIEW3D GetView3D() const;
	void ResetView3D(BOOL animate = FALSE);    // x, y and z from -10 to 10, the camera where it starts
	BOOL IsSoftwareRendering() const;          // 3D on WARP, the software rasterizer

	// ---- Fitting the view, and undo ----
	// The view around what the graphs draw (F too; 2D only): points and curves whole, functions' values over the view's
	// x; FALSE if there's nothing to fit. Right-dragging (or Shift-dragging) a rectangle zooms to it.
	BOOL ZoomToFit(BOOL animate = FALSE);
	// The user's changes, a step back or forward (Ctrl+Z, Ctrl+Y): sliders, dragged points, graphs shown or hidden,
	// the view (2D and 3D). The app's changes aren't steps, and don't end them (names and ids are matched).
	BOOL Undo();
	BOOL Redo();
	BOOL CanUndo() const;
	BOOL CanRedo() const;
	void ClearHistory();

	// ---- Axis titles ----
	// At the x axis's right end, the y axis's top, and in 3D in place of the axes' names; null or "" for none.
	void SetAxisTitles(LPCWSTR pszX, LPCWSTR pszY, LPCWSTR pszZ = nullptr);
	LPCWSTR GetAxisTitle(int axis) const;      // 0 x, 1 y, 2 z

	// ---- Tracing and features ----
	BOOL GetTrace(FPLTRACE& trace) const;
	void ClearTrace();
	// Starts a trace from the keyboard (T does too): the first function at the view's middle, else nothing. With a
	// trace, Left/Right step along its graph (Shift: finer), Ctrl+Left/Right go to its previous or next root, extremum
	// or intersection, Up/Down to the other functions at its x, Escape ends it.
	BOOL StartTrace();
	// A function's roots, extrema, y intercept and intersections with the other functions over the view; the
	// number there are (up to max filled).
	int GetFeatures(UINT id, FPLFEATURE* features, int max) const;

	// ---- State ----
	// Everything the plot shows as text, to keep and give back: the graphs (text, color, width, flags, range), the
	// parameters, the lists, degrees, the 2D view, the 3D box and camera, the mode and the panel's fold (not lParam,
	// not the styles). SetState replaces them all; FALSE (and nothing changed) if the text isn't a plot's state.
	CString GetState() const;
	BOOL SetState(LPCWSTR pszState);

	// ---- The panel (the legend and the sliders) ----
	void SetPanelCollapsed(BOOL collapsed);    // folded to its button (the user's click on it too)
	BOOL IsPanelCollapsed() const;

	// ---- Export ----
	// The view as a PNG, scale times its size in DIPs (COM must be initialized on the thread: WIC); in 3D, the frame
	// as it's shown (scale is ignored).
	BOOL ExportPng(LPCWSTR pszPath, float scale = 1);
	// The view as SVG: the grid, the axes and their labels, the graphs (not heat maps) and the legend. 2D only.
	BOOL ExportSvg(LPCWSTR pszPath);
	// The plot on the clipboard (Ctrl+C, WM_COPY): a bitmap at the screen's resolution (CF_DIB), a "PNG", in 2D the
	// SVG ("image/svg+xml"), and the graphs' texts a line each (CF_UNICODETEXT). COM must be up.
	BOOL Copy();
	// The plot drawn into a printer's (or any) DC, as large as fits in rc keeping its shape, centered: an image at the
	// device's resolution up to 8 times the window's size, stretched with HALFTONE past that. COM must be up.
	BOOL PrintTo(HDC hdc, const RECT& rc);

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
	LRESULT OnRButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(UINT, WPARAM, LPARAM, BOOL&) { Copy(); return 0; }
	LRESULT OnRButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCaptureChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetCursor(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnColorsChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint3D(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSampled(UINT, WPARAM, LPARAM, BOOL&);

	// FunctionPlotCtrl.cpp
	void Rebuild();                            // texts, names or the angle unit changed
	void ValuesChanged();                      // parameters' values changed
	void ViewChanged(bool notify);             // the view moved: sample again
	void Resample();
	void SetViewport(double xMin, double xMax, double yMin, double yMax);
	void UpdateTrace(POINT pt, bool dragging);
	// undo: a step from the state now (a burst of one kind within 0.7 s is one step; kind < 0: always a new one)
	void NoteIncomplete();
	void Remember(int kind);
	struct FplSnapshot TakeSnapshot() const;
	void Restore(const struct FplSnapshot& snapshot);
	struct FplEnds CaptureEnds() const;
	bool BeginTransition(const struct FplEnds& from);	// from there to where things are now; false: it just jumps
	void ApplyTransition(double t);
	void EndTransition();                      // to its end at once, if one is running
	void NotifyTransition();
	void EndBox(POINT pt);
	bool TraceKey(UINT vk);                    // the keys of a trace
	void ApplyTraceHit(const struct FplTraceHit& hit);   // a keyboard's: pinned, kept in view
	void SetTraceHit(int graph, int feature, double x, double y, bool valid,
		double z = std::numeric_limits<double>::quiet_NaN());
	void NotifyTrace();
	void NotifyView();
	float ToDips(int pixels) const;
	struct FplPanelHit HitPanel(POINT pt) const;
	void DragSlider(int row, float x);
	void SetParam(int param, double value, bool snap);   // a slider's value: sampled again, notified
	int NextSlider(bool back) const;           // Tab's row: -1 the plot itself, -2 out of the control
	COLORREF GraphColor(int index) const;

	// FunctionPlotPaint.cpp
	void Paint();
	HRESULT PaintTo(ID2D1RenderTarget* target, float dpi, bool exporting);
	// the plot as pixels (premultiplied BGRA, top-down) at scale times its size in DIPs (3D: the frame as shown)
	bool RenderImage(float scale, std::vector<uint32_t>& pixels, UINT& width, UINT& height);
	std::string BuildSvg();
	bool CreateTarget();
	void DiscardTarget();
	void BuildGeometries();                    // the sampled lines and fills as Direct2D geometries
	void BuildGeometry(int graph);
	void LayoutPanel();                        // the legend's and sliders' rows
	void PaintHeatmaps();
	void PaintGrid();
	void PaintGraphs();
	void PaintContourLabels();
	void PaintLabels();
	void PaintTrace();                         // 2D and 3D
	void PaintGraphLabels();
	void PaintBox();
	void PaintColorScales();
	void PaintPoints3D();
	void PaintPanel();
	void Paint3D(bool present);
	void PaintTicks3D();
	bool Ensure3D();                           // the renderer and the meshes on the GPU
	void Upload3D();
	void NotifyView3D();

	std::unique_ptr<FplState> m_State;
};
