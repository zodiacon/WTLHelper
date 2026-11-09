# FunctionPlot

How to build and test is in the root `CLAUDE.md`.

## Overview

`FunctionPlot`: a 2D function plotter (`CFunctionPlotCtrl`, window class `WC_FunctionPlot`, `FPL`-prefixed C-style API: `FPLGRAPH`, `FPLPARAM`, `FPLVIEW`, `FPLCOLORS`, `FPLN_*` from `0U-3450U`; internal names `Fpl*`), drawn with Direct2D in 2D and Direct3D 11 in 3D (`SetMode(FPLM_3D)`); accessibility is out of scope. Three layers: the window-free expression engine (`src/FunctionPlotExpr.h`), the window-free 2D model (`src/FunctionPlotModel.h`), and the control's window-free graphs (`src/FunctionPlotGraphs.h`) under the control itself.

Like DiffView it follows WTLHelper's dark mode through `GetSysColor` and the registered `ThemeChanged` message, with a palette for dark backgrounds; high contrast uses the system colors over the app's (`ForceHighContrast` for testing).

The first size sets the home view (x from -10 to 10); later sizes keep the center and scales.

## The expression engine

### Parsing

`FplParse` (`FunctionPlotParse.cpp`) reads math as people type it into a tree of `FplNode`s kept in one vector by index (`FplAst`; nodes never change once made, so subtrees are shared): implicit products (`2x`, `x(x+1)`, `2 sin x cos x`), a function's argument without parentheses up to an operator or the next function (`sin 2x` is sin(2x), `sin x/2` is sin(x)/2), `sin^2 x`, `sin^-1 x` (asin), `|x|`, `x!`, `{cond: value, ..., default}` and `{0<x<1}` (1 or NaN, so `x^2 {0<x<2}` restricts the domain), points `(a, b)` only as the whole expression, one relation at most at the top, and the Unicode characters people paste (written as code points: the repo has no `/utf-8`). '^' binds tighter than unary minus and is right associative; implicit products bind as `*`; a number can't follow implicitly (`3 4`, `x 2` are errors).

A word (four letters or more, with no x or y, whose split has only single letters and at least one unknown: `speed`, `rate`) stays one name, as does a Greek letter's name that doesn't split into known names; `ax`, `abc`, `axy`, `sina` are products. That holds with its letters all known if they mix parameters with variables or constants (with a parameter `a`, `rate` isn't r a t e, and `beta` and `eta` likewise, so a slider on one line doesn't change what another means); all parameters (`abcd`) or none are a product. A name that isn't known whole is split into known ones (`xy`, `sinx`, `pix`: fewest unknown letters, then fewest parts); one that can't be stays whole, reporting its first unknown letter (`2ax`: `a`, for an app to offer a parameter), and an unknown name alone before `=` with no known part is a definition. Errors are `FplError` codes with a UTF-16 position and length.

`q` is theta too (the key that gives it in the Symbol font, and `d/dq`), last in `Lookup`, so a parameter or function of that name has it, except a parameter flagged `FPLPF_AUTO` (`weakQ`) in a line with r and no other variable; a line starting `q =` or containing `~`, an ODE, or a line using another variable than r (`y = q x` wants a slider, while `y = theta x` is an implicit curve; `Lexer::Run` scans again) has it as a plain name (`m_PlainQ`), `FplBuild` reports it as unknown when theta wouldn't read, `FplParseFunction` reads its head so (`q(x) =`, `f(q) =`), and `FplPlot::Build` makes it plain in every line when a line defines it (`FplParseOptions::plainQ`).

Names added since people had plots are `IsYieldingName`: a parameter or function of that name has it, and a line defining it makes it one (`FindOwnName`); the short ones that read as products (`tg`, `rad`, `deg`, `gcd`...) are functions only with parentheses (`IsCallOnlyName`, `Lookup`'s `called`).

### Functions

The functions (`FunctionPlot/README.md` lists them for users, with the syntax, and needs the same changes as `g_Funcs` and `g_Aliases`): the usual trigonometric, hyperbolic and exponential ones, with their reciprocals and inverses (`acot` is pi/2 - atan, continuous), `log(x, b)` and `log10`, `trunc`, `frac`, `clamp`, `erfc`, `lgamma`, `nCr`, `nPr` (exact for whole numbers, by gamma between), `gcd`, `lcm` (of their arguments rounded, so they draw steps), `step`, `sinc` (sin(x)/x, its first two derivatives by series near 0: `SincD1`, `SincD2`, which print as `sinc'` and `sinc''`), `square`, `triangle`, `sawtooth` (period 2 pi, as sin; in the trigonometric range, so degrees apply), `normpdf`, `normcdf` (1 to 3 arguments), `deg`, `rad`, and other names (`tg`, `ctg`, `cosec`...). Those only ways of writing others are `Parser::Expand`'s (never evaluated).

The special functions (`beta`, `besselj`/`y`/`i`/`k` of any real order, `airyai`/`airybi`, `zeta`, `lambertw`/`lambertwm1`, `ellipk`/`e`/`f` by the parameter m) are over `<cmath>`'s, which only take non-negative orders and arguments (`FunctionPlotEval.cpp`: reflections, `Airy` by series within 1 and order 1/3 and 2/3 Bessel functions past that, `Lambert` by Halley's method); `AiryAiD`, `AiryBiD` print as primes, and `EllipEInc` as `ellipe` with two arguments (`Expand`).

Built-in functions take primes too (`sin''(x)`, `sin' 2x`): `Parser::Function` derives the one-argument call in a name of its own and puts the argument in, so there's no chain rule, as with `f'(2x)`.

`d/dx` (`d/dy`, `d/dt`, `d/dtheta`) is one lexer token whatever `d` is, taking its argument as a function does, `FplDerive`d in the parse.

`integral(f, lo, hi)` (or U+222B) integrates over x and `integral(f, t, lo, hi)` over a standard variable (`FplOp::Integral`, its variable bound in the body: `FplVariablesOf` leaves it out, derivatives by Leibniz's rule, printed with the variable named); compiled, the body is a sub-program (`FplProgram::m_Subs`) that the `Integral` instruction runs on a copy of the slots, integrated by adaptive Simpson (`FplIntegrate`: 8 pieces, then 1e-10 of their size, NaN where the integrand is, a budget of 200,000 evaluations), and an integral without free names folds to a constant.

`sum(f, k, a, b)` and `prod` (`FplOp::Series`, `∑`, `∏`): the lexer finds their indexes before tokens are made (`FindIndexes`), `FplParse` then rejects one used outside its sum, the compiled body is a sub-program with the index in a slot past all the names (`m_Indexes`, one more per enclosing sum), so `FplCode::Sum` copies only what the body reads; at most 100,000 terms; derivatives term by term, a product's by `f'/f`.

### Your own functions and substitutions

The user's functions (`FplUserFunction`: a name, parameters, a body in a tree of its own; `FplParseOptions::functions`) are found by `FplParseFunction` ("f(x) = ...": a name not known whole, though it may contain known parts as "area" does, then parameter names, then `=`; the body parsed with the parameters known; a user function's name again, or a parameter twice, is `Redefined`); the lexer knows them by name (so name splitting does: `2fx` is 2 f(x)), and a call is inlined (`CopyTree`: the body copied with the arguments put in for the parameters all at once, so g(y, x) swaps them), `f'(x)` and `f''(x)` (`'` or U+2032) deriving the body by its first parameter first.

A substitution, `a = x - 1`, is a user function with no parameters (`FplPlot::Build`, where the name is unknown and what it equals has variables; `UserCall` puts the body in with no arguments, whole, so `2a` is `2(x - 1)`), found round after round with the definitions and drawing nothing; a name already taken, a parameter's too, is `Redefined`, and a second `a = ...` is caught by its text, as `a` would be put in and it would read as an equation. A failed `u(x) = ...` or `u = ...` on a variable's name is `ReservedName` (`DefinesVariable`), only when it doesn't read, since `z = x y` and `r = 2` are plots.

### Lists and regressions

Lists: `L = [1, 2, a]` (`FplOp::List`, only as a definition's value: `ListNotAllowed` elsewhere) and the app's (`FplPlot::lists`, `SetList`) are parameters whose slot holds one element; `mean`, `median`, `stdev`, `sum`, `count`, `min` and `max` of one parse to the parameter named "mean(L)" (`FplAggregateName`, `FplParseOptions::lists`; `NotAList` for one of a number), each list's 7 kept right after it.

`y1 ~ m x1 + b` (`FplRel::Fit`) is a `Regression`: `FplPlot` finds its unknown names (parsing again with each `UnknownName` added) after the definitions' rounds, then the definitions again so they can use them, and fits them in `UpdateValues` by Levenberg-Marquardt (forward-difference Jacobian, from the last fit's values), with R squared from its left side.

### Classifying, deriving and compiling

`FplClassify` (`FunctionPlotCalc.cpp`) tells the kind: y = f(x) (a bare expression too), x = f(y), r = f(theta), (x(t), y(t)), a point, a definition, else implicit F = lhs - rhs with theta and r as atan2(y, x) and hypot(x, y); inequalities turned around to y rel f(x).

`FplDerive` is symbolic (partial: other names are constants; piecewise by branch, min/max as piecewise, factorial and gamma through `digamma`), then `FplSimplify` (folding, x+0, 1*x, numbers first); `FplToString` prints so the parser reads it back the same.

`FplCompile` (`FunctionPlotEval.cpp`) folds the parts without names and emits a stack machine (`FplProgram`: constant integer powers multiply, piecewise jumps; slots x, y, t, theta, then the parameters in the order given); `FplEvalTree` is the slow reference the tests check it against. `FplPow` makes a negative base to a rational power with an odd denominator real ((-8)^(1/3) = -2); `mod` takes the divisor's sign; `FplBuild` does it all.

Degrees are applied as each trigonometric call is made (no longer a pass at the end), so derivatives taken in the parse see the pi/180.

`z`, `u` and `v` are standard variables too (slots x, y, t, theta, z, u, v, then the parameters from `FplFirstParam` = 7): `z = F(x, y)` (either way round) is a `Surface`, a tuple in x and y a `VectorField`, and 3-tuples are 3D: in t a `SpaceCurve`, in u and v a `ParametricSurface`, constant a `Point3D`; any other equation with z (and no t, u, v, theta or r) is an `ImplicitSurface`, F = lhs - rhs (an inequality with z isn't a plot).

Release evaluates a 16-instruction expression in about 22 ns; `FplBuild` also compiles a function's derivative (`df`) for the analysis.

## The 2D model

### The view and ticks

The 2D model works in screen DIPs (y down): `FplViewport` maps the plane to the screen (zoom about a point, pan, resize keeping the scales, equal scales); `FplMakeTicks` picks majors 1, 2 or 5 times a power of ten, or multiples of pi (pi/12 up), with minors between, and `FPLS_POLARGRID` replaces the grid and the ticks' labels with circles and spokes (`FplMakePolarGrid`: polylines cut to the view, so a circle many screens across costs little; 30 degrees apart with the origin in view, about 12 over the angles in view otherwise; radii along the first direction with room, `FplFormatAngle` for the angles, as fractions of pi with `FPLS_PITICKS`; the SVG too; a square grid on log axes and none in 3D). `FplFormatTick` labels them (U+2212 minus, "3C0/4" as code points, a mantissa times 10 with superscript digits when fixed would be longer than 8 characters, decided for the whole axis).

### Log axes

Log axes (`SetLogAxes`, base 10, 2D): `FplViewport::logX`/`logY` keep the ranges in the plane's values (`FixLogRanges` makes them positive), and the mapping, panning, zooming and resizing work in "axis units", their logarithms (`AxisX`, `FromAxisX`; `ScaleX` is DIPs per axis unit, `DipsPerUnitX(x)` DIPs per unit at a value), so a value that isn't positive is NaN on the screen and breaks a line. There's no 0 on a log axis: `AxisLineX`/`AxisLineY` put the axis far off, so the labels go along the edge. `FplMakeLogTicks` gives majors at powers of 10 (every 1, 2, 5, 10... decades), minors at 2 to 9 times them, and under a decade the linear ticks spaced for the range's top; functions, features, stepping and fitting go by the logarithm, fields on a nice step of it (`FieldGrid`); no equal scales and no tangent line on a log axis, and `SetLogAxes` clears undo.

### Sampling

`FplSampleCurve` (`FunctionPlotSample.cpp`) samples t -> (x, y) adaptively: halved where the midpoint is off the chord by more than `tolerance` or the chord is longer than `maxChord` (a step's midpoint lies on its chord), not where all three points are far off on the same side; at `maxDepth` a still-bent interval is halved toward its bigger gap 64 times, and a gap that stays wider than `jump` is a discontinuity (tan, floor, 1/x break; |x|, cbrt, atan(1000x) don't); where the domain stops the edge is found by bisection, so sqrt(x) starts at 0. Lines are then clipped (`FplClip`, Liang-Barsky with a trivial reject and each cut computed on its edge from the nearer end, since sampled ends can be 1e300 away) to the view grown by its size on every side; fills are clamped to it.

Implicit curves and regions (`FplSampleImplicit`, `FplSampleRegion`) are marching squares: 8-DIP cells, those with a sign change (and any the curve runs into through a shared edge, a worklist, so there are no cracks) split to 2-DIP cells kept in dense arrays (values and edge crossings by vertex), crossings found by Illinois regula falsi, and a crossing whose values don't shrink toward zero (an asymptote, the atan2 branch cut of `theta = 1`) is dropped from the lines; saddles are decided by the center's value; region polygons all wind the same way so one geometry fills them without seams.

`FplSample` turns a built expression into lines, fills (an inequality's: a function's pieces closed to the far edge, a polar one through atan2/hypot) and points, `dashed` for strict ones; a surface into a heat map (`FplSampleHeatmap`: values at 4-DIP cell centers, colored over the 2nd to 98th percentile so a pole doesn't wash it out; `FplColormap` is viridis) and contours (`FplContourLevels`: about 10 nice levels in that range; `FplSampleContours`: `FplSampleImplicit` of F - level each, so about 25 ms for a full-screen surface in Release, the slowest kind); a vector field into arrows (`FplSampleField`: on the plane's points a nice step apart, at least `arrowSpacing` DIPs, so they don't swim while panning; lengths by magnitude against the 95th percentile, at least a fifth of the longest).

### Regions between curves

`f < y < g` (or `>`, `<=`; `f(y) < x < g(y)`, so `1 < x < 3` is a band) is `FplKind::Between`: `Parser::Top` takes a chain only the same way round with `x` or `y` alone in the middle (anything else stays `TooManyRelations`), as `And(Cmp, Cmp)`, and `FplClassify` puts the lower side first and names the middle (`def.name`). Its fill is where lower <= upper, evenly along the sides' variable (in axis units), each stretch's ends found by halving, with both sides drawn over them (dashed only if both are strict), so it traces, intersects and fits by its sides; a side off a log axis is at its low edge.

### Features and intersections

`FplFindFeatures` (`FunctionPlotAnalysis.cpp`) scans y = f(x) for roots (sign changes refined by Brent's method, `FplBrent`, kept only when the values shrink toward zero) and extrema (sign changes of f', `df` or differences; kept when f is continuous there and no higher than the slopes at the samples allow, so a corner counts and a pole doesn't), touching roots (an extremum within `yTolerance` of zero) and the y intercept; `FplFindIntersections` does the same for f - g.

Intersections go beyond y = f(x): `FindFeatures` gives any graph with a curve (x = f(y), polar, parametric, implicit, an ODE's solutions, regions' boundaries) `Intersection` features where it meets the others' curves in the view (two y = f(x) still meet by their formulas), from what was last sampled, so `OnSampled` finds them again when a heavy graph's full version arrives. `FplFindCurveIntersections` crosses the two curves' lines through a grid of the view (`SegmentGrid`, 24-DIP cells), then refines on the curves (`FplCurve`: `G(x, y)` for an equation, `C(t)` for a parametric curve, neither for an ODE's solution): Newton on two equations or two parameters, Brent along a parametric curve in an equation, kept only within 3 DIPs of the lines' crossing. Touching without crossing is a short stretch of the lines within a third of a DIP, taken at its middle (so to about a DIP). Two samplings of one curve cross again and again at tiny angles: 3 or more crossings in a stretch over 150 DIPs are dropped, while 1 or 2 stay, which keeps shallow real crossings. Features sort by x then y and `NextFeature` takes the y too, for two at one x.

### Asymptotes and holes

y = f(x) and x = f(y) get asymptotes and holes (`FPLGF_NOSINGULAR` turns them off). A hole is one undefined point, which sampling never lands on, so candidates come from the tree (`CollectSingular`, `FplExpression::singular`): the factors of denominators and of bases to negative powers, the arguments of logarithms, and those of tan and sec (their cos, `wrap` 1), csc and cot (their sin, 2). `FindSingularities` finds their zeros in view (a sign change by Brent, kept if the atom's value really shrinks; a touching zero, `x^2 - 2x + 1`, by a local minimum of its size) and tells each by the function half a DIP away and 8 and 64 times closer on each side: the differences not shrinking while it grows means it runs off (so a logarithm's steady growth counts), shrinking means it settles on a limit (extrapolated). Any side running off is an asymptote; both settling on one limit within half a DIP is a hole, unless the atom is exactly 0 and the function has a value there. Numbers can't tell a denominator's zero in a piecewise branch not taken from a true cancellation, as both are smooth, so atoms inside `{ }` are `guarded`: they make asymptotes only. Past 256 zeros in view there are none, as they couldn't be told apart.

### Slow graphs and the sampling thread

The heavy graphs (implicit curves and regions, surfaces; in 3D implicit surfaces: `FplPlot::IsHeavy`) are sampled coarsely at once and fully on a sampling thread (`FplWorker` in `src/FunctionPlotWorker.*`, window-free: a job per graph holds copies of the expression and slots, a newer job for a graph replaces a waiting one, results are numbered by generation so only the latest is taken, and `WM_FPL_SAMPLED` is posted; `Rebuild` cancels); exports resample synchronously (`syncSampling`).

A curve whose program has sub-programs and is slow by the clock (`FplPlot::EvalCost`, measured at each `IsHeavy` as the average over five places in the view, since an integral's cost grows with its limits, stopping once it's past 200 microseconds in all: 8 microseconds an evaluation) is heavy: `CoarseOptions` gives as many straight lines as 6 ms of evaluations make, and the full one comes from the thread and stretches meanwhile; past 50 microseconds `FindFeatures` leaves it to the thread (`FplFunctionFeatures`, `FplJob::features` for the traced function, `RequestSlowFeatures` when a trace starts on one, keeping what's drawn with `viewOnly`), without intersections or curve meetings; `GetFeatures` scans it anyway, as it was asked for. A slow `z = F(x, y)` in 3D is heavy too (24 by 24, then 96 by 96); 3D's tubes and parametric surfaces aren't.

## ODEs

### First order: slopes and solutions

ODEs: `FplSplitOde` finds `y' = F` (U+2032, or `dy/dx = F`) with any number of `, y(a) = b` before anything is parsed (`FplBuild`; errors at their places in the whole text), which is `FplKind::SlopeField` (`FplExpression::starts`: a and b of each). `FplSampleSlopes` draws its slopes as `FplGeometry::marks` (segments, fainter under a solution); `FplSolveOde` solves through each point with Dormand-Prince 5(4), both ways to the view grown by its width: steps of at most `odeStep` DIPs in x, `odeTolerance` DIPs of error in y, chords of at most `maxChord`/2 DIPs.

Near a vertical tangent (`y' = -x/y`) the error estimate alone lets steps through that jump about, so where a slope is steep on the screen (past 10:1) a step may not change it by more than half; a slope past 1000:1 that's still growing on an accepted step ends the solution. Growing matters: a stiff transient starts as steep but falls, and carries on. A step that moved too far is cut by at most tenfold (`Shrink`): on a stiff equation an unstable trial's move is astronomical, and cutting by its ratio went straight past the smallest step allowed, so nothing was drawn at all.

### Stiffness

Every solver starts explicit and turns implicit when an equation shows itself stiff (`StiffWatch`, Hairer's test: h times the Rayleigh quotient of the Jacobian along the last two stages' difference, under -3.25 three trials running, rejected ones included; the sign keeps tangents and blow-ups explicit, as solutions spread apart there, and a wrong guess only costs an order, the error being watched either way). `Ros2Step` is Verwer's ROS2, L-stable and 2nd order, a linear solve with a forward-difference Jacobian on the state plus the independent variable; the error is against its 1st order result. Implicit, the smallest step is taken whatever it does, as a stiff transient can be a jump, and the tangent checks on F don't apply: F off the slow solution by a rounding error is huge and means nothing.

### Budgets

Budgets: 30,000 steps each way in 2D (`MaxOdeSteps`, 100,000 for 3D systems, which their length limits) and `maxEvals`; running out sets `FplGeometry::incomplete` (`FplGraph::incomplete3D` for a mesh), which the legend says on a line of its own (`NoteIncomplete`, `FPLGRAPHINFO::incomplete`). A fast oscillation (`y'' = -1e8 y`) isn't stiffness, as nothing shrinks, so that's how it ends. Reaching the view's edge, leaving the screen or a vertical tangent aren't incomplete.

### Higher orders and systems

`FplSplitOde` also takes `y'' = F` with `, y(a) = b, y'(a) = c` pairs at one a (`BadInitial` otherwise; `FplKind::SecondOrder`, no field) and `x' = P, y' = Q` (or `dx/dt`, `dy/dt`) with `, (a, b)` points (`FplKind::OdeSystem`: `FplSampleField`'s arrows, fainter under the trajectories). `y'` in a second-order F is the t slot: the text is written over in place as ` t`, the same length, so errors stay in their places, after a pass with ` 0` that catches a t of the user's own. Both go through `Step2` (Dormand-Prince for two values): `FplSolveOde2` as `FplSolveOde`, the slope's error counting by what it does to y over the step; `FplSolveSystem` both ways in time with steps of at most `odeStep` DIPs on the screen, ending where it leaves the view grown by its size, settles (a speed under 1e-6), closes on itself (back within 1.5 DIPs after 20, when the other way is skipped) or has run 8 times around the view, so a pendulum's orbit costs one lap per frame.

ODEs go up to the fifth order and 3D, in `src/FunctionPlotOde.cpp` around `StepN` (Dormand-Prince for n values): `FplSolveOdeN` takes y and its derivatives, a derivative's error counting by what it does to y over the step (h^k/k!). `y'''' = F` with `, y(a) = b, y'(a) = c, y''(a) = d, y'''(a) = e` groups is still `FplKind::SecondOrder` (`FplExpression::odeOrder`; the conditions in order and at one a, else `BadInitial`; past the fifth `OdeOrder`). `y'`, `y''`, `y'''` and `y''''` in F are the t, u, v and z slots, written over in place at the same length; the first pass, which catches a t, u, v or z of the user's own, stands `e` in for them, as a name: a number there made `2y'` read as `2 0`, an error. A y with as many primes as the equation stays as written, an error.

Systems may use t, from t = 0 (`FplSolveSystemT`): then there are no arrows, as they'd be true of one moment, and it neither settles nor closes, and the check for steps too small goes by the step and not the speed, since `x' = sin t` rests at t = 0.

`x' = P, y' = Q, z' = R` with `, (a, b, c)` is `FplKind::OdeSystem3D`, 3D only: `FplSolveSystem3` works in the cube's units (steps of at most 0.02, so the box's size and the axes' different ranges don't matter), ending far outside the box, on settling or closing, or after 100 cube units, so Lorenz's has an end; `Mesh3D` makes tubes and a sphere at each start.

`FPLGM_RANGE` (`FplSampleOptions::odeRange`) limits both kinds of system to that t.

### Dragging initial points

The first condition's point can be dragged when a and b are parameters alone (`FindDragged`; `points[0]`, kept even when it isn't anywhere).

Every initial point can be dragged by its own parameters: `FplGraph::drags` by point, so `geometry.points` keeps one per condition even where one isn't anywhere (`DragOf`, `DraggableAt(..., &point)`, `dragPoint`, `hotPointIndex`).

## The plot (`FplPlot`)

`FplPlot` (`FunctionPlotGraphs.cpp`) holds the app's graphs (by id; text, color or the palette's by place, width, `FPLGF_*` flags, a t/theta range) and parameters (name, value, slider range and step): `Build` finds the definitions (`c = 2a`, an unknown name alone before `=`) and the user's functions (a graph `f(x) = ...` is drawn as what its one parameter makes it: x as y = f(x), y as x = f(y), t as a curve, theta as r = f(theta); others draw nothing) round after round so one may use another written later, gives them slots after the parameters, and builds the rest with every name; a second definition of a name, or one of an app's parameter, is `FplErrorCode::Redefined` (added for this); a cycle defines nothing (unknown names). A graph that reads a list (`listSlots`, found with `FplProgram::Reads`, as `UsesSlot` knows only 64 slots) is sampled and meshed once per element (points up to 100,000, curves 200), and tracing takes it as polylines, without features.

`UpdateValues` works the definitions out in order, as many rounds as there are (a cycle stays NaN).

`Sample` samples the drawable graphs; `FindFeatures` finds one function's features over the view plus its intersections with the other drawable functions; `Nearest` (features first, within a snap distance, then the nearest polyline point, a function's moved exactly onto it) and `TraceAt` (a dragged trace follows the mouse's x on a function) say what a trace is on; `FplFormatCoordinate` prints a traced coordinate to a DIP's precision.

Keyboard tracing: `Step` (along a function by DIPs of x, past undefined stretches; other kinds along their polylines by arc length), `NextFeature`, `NextGraphAt` (the other functions at the trace's x) and `Slope` (the derivative's program, else differences; `FPLS_TANGENT` draws the tangent and labels the slope).

A point whose coordinate is an app's parameter alone, `(a, b)` or `(a, f(a))`, is draggable (`FindDragged` at `Build`, `DraggableAt`; not with `FPLGF_NODRAG`), setting the parameters as the sliders do.

`FplSaveState`/`FplLoadState` (`FunctionPlotState.cpp`) write and read a plot as text, a line per setting with tab-separated fields and escaped texts (an unknown escape kept as written), skipping lines they don't know.

Hovering (`Nearest` with `surfaces`, not a click, which pans) where no curve is near traces the topmost `z = F(x, y)` surface at the mouse itself, with its value (`FplTraceHit::z`, `FPLTRACE::hasZ`/`z`, "z = ..." in the label); `Pick3D` is 3D's: the camera's `Ray` through the mouse against each drawable graph's mesh (`FplIntersect`, Moller-Trumbore, hits outside the cube skipped as they aren't drawn), the nearest hit refined to an explicit surface's exact z or a point's own place.

Graphs can have a label (`pszLabel`, `{x}`/`{y}` for its coordinates, `FplPlot::LabelPlaces`: beside points, at a curve's rightmost point in view), an opacity and a fill opacity (`FPLGM_OPACITY`), and be dotted (`FPLGF_DOTTED`) or hollow points (`FPLGF_HOLLOW`); `SetAxisTitles` names the axes (2D at the axes' ends, 3D in place of the names); all of it goes into the saved state and the SVG.

## The control

### Painting

The control (`src/FunctionPlotCtrl.cpp` for the API and input, `src/FunctionPlotPaint.cpp` for drawing; state `FplState` in `src/FunctionPlotData.h`) works in DIPs on an HWND render target like MarkdownView's: each change samples again (`Resample`, cheap enough to do per mouse move) and turns the polylines and polygons into one path geometry per graph (fills with `D2D1_FILL_MODE_WINDING`, so the marching cells don't seam).

Painting order: the grid (aliased, on pixel centers; no minor lines in high contrast), the axes, the regions (translucent), the curves (round joins; dashed for strict inequalities and `FPLGF_DASHED`), points, the ticks' labels on a patch of the background (along the edge when an axis is off screen; `FPLS_PITICKS` for the x axis), the traced graph's features and the trace with its label, then the panel at the top left (the button beside it folds it to itself, `SetPanelCollapsed`): the legend (a swatch, hollow when hidden or failing, a click toggles it, `FPLN_GRAPHTOGGLED`; a definition's value; an error's message in red) (an integral's value, a list's count, a regression's fitted values and R squared on a second line) and a slider per parameter (`FPLN_PARAMCHANGED`) with a play button (`AnimateParameter`: the range in 4 s and back on a 16 ms timer, steps of at most 0.1 s).

Tab goes along the sliders (`WM_GETDLGCODE` wants Tab only while there's a next one, `NextSlider`), and the focused one (a dotted frame) takes the arrows (a step, or a hundredth of the range), Page Up/Down, Home/End and Space (play); Escape gives the keys back to panning.

Surfaces paint first: the heat map as a bitmap made from the sampled pixels on the target (dropped with it), under the grid; contours under the curves with their levels labeled at the middle of each level's longest line; arrows as one geometry of shafts and two-stroke heads.

A color scale (`PaintColorScales`, `FplMakeColorScale`; `FPLS_NOCOLORSCALE`) stands at the right: one per heat map in 2D with its own range (the map's lo and hi, so the 2nd to 98th percentile), marked with the graph's color when there are several; in 3D one for all, as surfaces are colored by height in the box, with no patch, which would fade the box's ticks.

Painting goes through `FplState::rt`, so `ExportPng` paints the same into a WIC bitmap render target (scale times the size, the window's brushes and bitmaps set aside meanwhile, no panel button or hot and focus marks; COM must be up) and saves a PNG; `ExportSvg` writes `FplWriteSvg` (window-free in `FunctionPlotExport.cpp`: heat maps as embedded PNGs from `FplEncodePng`, uncompressed so no codec is needed, so a file of about 500 KB for a 1000 by 800 surface with its contours, the grid, axes, labels, fills, contours, curves, arrows, points, color scales and legend).

### Input

The keys: with a trace (T starts one at the view's middle, `StartTrace`) Left/Right step along it (Shift finer), Ctrl+Left/Right go between its features, Up/Down to the other functions, the view following a point that would leave it (the trace is pinned, as a dragged one).

The mouse: on a draggable point it drags it (a ring around it, the move cursor); on a graph it traces (hovering, or a drag that stays after the button's release until a click elsewhere or Escape; `FPLN_TRACE`), elsewhere it pans; the wheel zooms about the mouse (Ctrl: x only, Shift: y only, both with `FPLS_EQUALSCALE`); the keys pan (arrows), zoom (+/-) and go home (Home, `SetHomeView`); `FPLN_VIEWCHANGED` for the user's moves.

`GetFeatures` finds any function's features over the view; `GetValue` any name's value (fitted ones too); `SetList`/`GetList`/`RemoveList` the app's lists; `GetState`/`SetState` the whole plot as text (SetState's 2D ranges are kept for the first size when the window has none yet).

F or `ZoomToFit` fits the view to what the graphs draw (`FplPlot::FitBounds`: points and curves whole, functions' values over the view's x by their 2nd to 98th percentile), and a right-drag (or Shift+drag) zooms to a rectangle (`FplViewport::ZoomTo`; a right click that isn't a drag still makes `WM_CONTEXTMENU`).

Undo (`FplHistory` of `FplSnapshot`s: the parameters by name, the graphs hidden by id, the 2D view, the 3D box and camera; Ctrl+Z/Ctrl+Y, `Undo`/`Redo`) keeps the user's changes only, a burst of one kind (wheel notches, keys) within 0.7 s being one step (`Remember`); `SetState` clears it.

### Copying and printing

`Copy` (Ctrl+C, `WM_COPY`) puts `CF_DIB`, "PNG", in 2D "image/svg+xml" and the graphs' texts on the clipboard; `PrintTo(hdc, rc)` draws an image into a DC, as large as fits keeping its shape (`RenderImage`, shared with `ExportPng`, up to 8 times the window's size, then `StretchDIBits` with `HALFTONE`).

### Animation

A playing parameter crosses its range in `FplParam::sweepSeconds` (4 at first, `SetAnimationSpeed`, saved; `FplSweep`).

### Transitions and stretching

Transitions (`FplTransition`, `CaptureEnds`, `BeginTransition`, `SetTransitionTime`, 220 ms at first, 0 to jump, on `TransitionTimer`, eased): undo and redo (parameters, view, box, camera), Home, F and zooming to a rectangle glide there, and `ResetView`, `ResetView3D` and `ZoomToFit` with `animate`; `FplInterpolateRange` changes a width geometrically with edges in straight lines, in axis units, so by decades on a log axis, and `FplInterpolateAngle` goes the short way round. `FPLS_NOANIMATION` or Windows' client area animations being off make them jump, and `FPLS_ALWAYSANIMATE` overrides that setting (the demo sets it). Any input, resize, export or API change takes one straight to its end (`EndTransition`), which is when its notifications go; a parameter playing is left alone.

While a heavy graph's full version is on its way after only the view moved (`FplState::viewOnly`, set by `ViewChanged`), its last full drawing stretches to the view in its old rectangle, with the coarse one around that (`FplGraphPaths::stale`, `FplMapBetween`, `DrawAroundStale`; transformed geometries, so strokes keep their width; the heat map's bitmap and contours' labels too). Past 8 times the scale or with no overlap it's dropped, and a change of values drops it. 2D only.

## Math display

Graphs' texts show as mathematics in the legend (`FPLS_PLAINLEGEND` turns it off), in PNGs and in the SVG.

`FplParseOptions::display` makes the parser keep what was typed, for drawing and never evaluating: parentheses (`FplOp::Group`), `d/dx` (`Deriv`), the user's calls and aggregates (`Named`), primes on names, commas at the top (`Sequence`), numbers' text, no expansions or degrees, and names nothing knows as names; `FplNode::implicit` marks a product with no sign.

`FplLayoutMath` (`src/FunctionPlotMath.*`, window-free, measured through `FplMathMeasure`) makes text and stroke items around a baseline (a text's box is a fixed height so baselines don't jump): since a Group holds what parentheses were typed, it needs no precedence rules, only leaving them out where a fraction, power, root or function's own parentheses show the grouping (and adding them around a sum's, product's, integral's or `d/dx`'s sum, `Operand`, as their arguments' parentheses aren't in the tree, and around one of those with a power or a factor after it, `Whole`, which would look like part of what it applies to). Radicals, bars and delimiters are strokes that grow; Cambria Math for upright text and symbols, Cambria for italic variables, with Cambria Math's very tall lines handled by asking each layout for its baseline (`FunctionPlotMathDraw.cpp`, `FplMakeMathDrawing`, `FplDrawMath`).

Sliders' names go the same way, if they fit beside the track, and a definition that's only a number shows no value after it (`DefinitionIsNumber`). `FplFitMath` tries sizes; what doesn't read or fit stays as typed, and a graph with an error too.

The panel's graph rows are as tall as they need (`FplPanelRow::main`, cached in `mathCache` by text and room, cleared by `Rebuild`).

The usual symbols, in the layout and the exports alike: `FplGreekSymbol`/`FplGreekWord` for names (capitals upright; a Greek name that doesn't read as all known names stays one name in `Lexer::Identifier` rather than letting its letters become parameters), `FplSymbolOf` for functions (gamma, digamma, zeta, Airy, Bessel with its order as a subscript, W, elliptic, H, normal), `nCr` as a binomial, `mod` between its arguments.

`FplToLatex` and `FplToMathML` (`FunctionPlotMathText.cpp`: one walk over the display parse with a `Writer` for each format; `GetGraphLatex`, `GetGraphMathML`) show what the layout shows, with the same parentheses, and have to change with it.

`CFunctionPlotMathCtrl` (`WC_FunctionPlotMath`) shows one text, as a preview or a formula, with `SetText(text, plot)` reading it by a plot's names.

## 3D

### Meshes

3D (`src/FunctionPlot3D.h`, window-free and tested): `FplBox3` maps the space's ranges to the cube [-1, 1]^3 (clamped to +-4 so floats stay sane; the pixel shader discards what's outside the cube), `FplCamera` orbits the cube's center (yaw, pitch, distance 6 so the cube fits at its 0.62 rad field of view; row-vector, right-handed, z-up matrices with Direct3D's 0 to 1 depth), and the meshes are made on the CPU: `FplSurfaceMesh` (z = F(x, y) on a 96 x 96 grid), `FplParametricMesh` (64 x 64 over the graph's range, 0 to 2 pi), normals from the grid's neighbors, cells left out where a corner is undefined or an edge jumps (`IsJump`: a gap over 0.05 cube units that doesn't close in 16 halvings toward its bigger half: floor, tan, poles, not the merely steep); `FplSampleCurve3` (1024 samples, broken the same way) into `FplTubes` (parallel-transported rings), points as `FplSphere`s, `FplImplicitMesh` (F(x, y, z) = 0 by marching tetrahedra: a 48^3 grid in `Build3D`, each cell split into the 6 tetrahedra around its 0-7 diagonal, Kuhn's, so neighbors' faces match and the mesh is closed; a crossing per grid edge, kept in a dense array by grid point and the 7 edge directions the tetrahedra use, found by 6 halvings and a secant step and dropped when its values don't shrink (an asymptote); normals from the grid's central differences interpolated along the edge, so no evaluations beyond the grid and the halvings: a gyroid's 200,000 triangles in about 45 ms in Release), `FplBoxEdges`, and `FplBoxTicks` (the numbers along the three edges toward the camera, and the axes' names).

`FplPlot::Build3D` makes each drawable graph's mesh: surfaces (implicit ones too), space curves, 3D points, and the 2D curves and points on the z = 0 plane (implicit curves, regions and fields as said below).

`(P, Q, R)` of x, y and z is `FplKind::VectorField3D`, 3D only: `FplFieldMesh3` puts solid arrows, centered, on the axes' nice ticks about 9 to a side and none on the box's faces, so they stay put as the box moves, with lengths in the cube's units against the 95th percentile.

### 2D kinds on the z = 0 plane

In 3D the 2D ODE kinds show their solutions and points on the z = 0 plane (`Mesh3D` samples them with `FplSample` for a view of the box's x and y and makes tubes); their slopes and arrows, and a vector field's, lie flat on the plane as thin quads (`FplFlatSegments`, `FplGraph::fieldMesh`, `gpuFields`, in the see-through pass at `fieldOpacity`, fainter under solutions; not picked). Implicit curves, regions' boundaries and `Between`'s sides go the same way. `Pick3D` puts a hit on any of them on the plane.

Regions show on the plane too, always see-through, flat and in the graph's fill opacity (`FplGraph::fillMesh`, `gpuFills`, drawn in the translucent pass with `FplDrawStyle::unlit`): `FplFillMesh` turns any polygons into spans by the winding rule, 400 rows to the box, since the sampler's polygons are of several shapes (marching squares' cells, a function's region, what's between two curves); an edge is a staircase, which the boundary's tube covers.

### Dragging points

Points can be dragged in 3D: `Points3D` lists a graph's in DragOf's order (`(a, b, c)`, a 2D point or an ODE's start on z = 0, a 3D system's start), `DraggableAt3D` finds one by its projection, on the level it's at (`FplDragOnPlane`) or up and down with Ctrl or when only its z has a parameter (`FplDragUpright`, the height on the upright line nearest the ray); `PaintPoints3D` rings them. A solid surface in front of a point hides its ring and it can't be grabbed through (`HiddenBySurface`: a ray against the surfaces' meshes; see-through ones and curves don't count).

### The renderer

The renderer (`src/FunctionPlotRender3D.*`, `FplRenderer3D`) is Direct3D 11 at feature level 10.0 or above, WARP when there's no hardware device (`IsSoftwareRendering`), a flip-model swap chain, 4x MSAA resolved into the back buffer, shaders compiled at run time with `D3DCompile` (two-sided lighting with a light near the eye, viridis by height for `FPLGF_HEATMAP`), and a Direct2D device context on the back buffer for the overlay (the ticks and the same `PaintPanel` as 2D, through `FplState::rt`).

The swap chain is on a child window over the control (`WC_FunctionPlot3DView`, `HTTRANSPARENT` so the mouse reaches the control, painting by sending the control `WM_FPL_PAINT3D`; the control's `Invalidate` hides `CWindow`'s to redraw it too): a flip-model swap chain on the control's own window kept 2D's HWND target from showing after going back.

In 3D a drag orbits (Shift+drag moves the box's x and y under the mouse), the wheel and +/- zoom the box's ranges, the arrows orbit, Home resets (`ResetView3D`), `FPLN_VIEW3DCHANGED`; hovering traces (`Pick3D`, the point and "(x, y, z)" drawn on the overlay; let go when the values change); `ExportPng` reads the frame back before presenting it (grayscale text), `ExportSvg` is 2D only.

A lost device is reset and everything uploaded again on the next frame.

### See-through graphs and lines

3D: `FPLGM_OPACITY` under 1 makes a graph see-through, by weighted blended order-independent transparency (McGuire and Bavoil; `FplRenderer3D::BeginTranslucent`, `PSOit`, `Composite`): solid meshes first, then the see-through ones reading depth into a 16-bit float accumulation target and a revealage target with their own blending per target, resolved and put over the back buffer in `BeginOverlay`. Those targets are large, so they're only made when something is see-through (`EnsureTranslucent`) and released with the size; below feature level 10.1 it's plain alpha blending in the order drawn.

`FPLGF_MESHLINES` and `FPLGF_LEVELLINES` are drawn in the pixel shader (`Shade`, `Line`: a pixel and a bit wide by `fwidth`, fading out where they'd crowd), so they cost no geometry and fit any mesh: `FplMakeLineGrid` gives each axis a scale and offset putting lines on the box's ticks (`z = F`: x and y; implicit: x, y and z; levels: z, closer), and a parametric surface's run along its grid (`FplVertex3::u`, `v`, every 4 cells). The shader is compiled at run time, and a compile error just leaves the plot in 2D with a message only in the debug output (`line`, `point` and `triangle` are reserved in HLSL), which the `Shaders` test catches.

The shaders are in `src/FunctionPlotShaders.*`, apart from the renderer, so `FunctionPlotTests`' `Shaders` compiles every entry and binds the vertex layout and each shader on a WARP device without a window: a shader that doesn't compile leaves the plot in 2D with the reason only in the debug output.

## Demo and tests

`FunctionPlotDemo` (an editor whose lines are the graphs, the caret's line as mathematics under it (`CFunctionPlotMathCtrl` in a small left pane, which passes on the edit's messages), with lines like `a: -8..8`, `b: 0..4, 0.5`, `c: -pi..pi` or `d: 0..5, 0.5, 8s` (8 seconds to play across) setting a slider's range (`ParseRangeLine`; demo-only, an empty graph keeping the line's number, with its text as the label so the saved state keeps it; a name, a colon and two dots that don't read get their own message and mark, `RangeKind::Bad`, and anything else is a graph), the word `theta` turned into its letter as it's typed or opened (`ConvertTheta`, not inside a longer name), wrapped (`CaretLine` counts line breaks, as the edit's own lines aren't the graphs'), errors marked over the edit's painting by `PaintErrorMarks` (a dot in the margin, a wavy line under `errorPos`, asked to paint again after what makes the edit draw directly), beside the plot in a splitter: `CCustomSplitterWindow` on x64 and WTL's `CSplitterWindow` elsewhere, as only x64 links WTLHelper; the panes are its children, and it forwards their notifications to the main window, examples including the user's functions, a surface, a vector field, an animation, integrals, data with a regression, lists, draggable points and some in 3D, File > Open/Save Plot (`.fplot`, UTF-8), `/open file.fplot`, View > Tangent Line, View > 3D (Ctrl+3), automatic sliders for unknown names, heat maps on or off, the traced point and the view in the status bar, Analysis > Features, File > Export PNG/SVG, File > Copy Line as LaTeX/MathML, and `/example N /export file.png|.svg|.bmp|.fplot|.tex|.mml` to write one and exit (.bmp through `PrintTo`, a page with no printer; .fplot as File > Save does), View > Log X/Y Axis, File > Copy Plot and Print, which links WTLHelper on x64) is for checking by hand.

Covered by `FunctionPlotTests`; Release samples a function across 1600 DIPs in about 0.15 ms and 163 rings of sin(x^2 + y^2) = 0.5 in about 35 ms.
