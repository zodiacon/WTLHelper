# FunctionPlot

A math function plotter control for WTL, in 2D (Direct2D) and 3D (Direct3D 11). You give it text such as `y = sin x`, `x^2 + y^2 < 4` or `z = sin x cos y`, and it draws it. `FunctionPlotDemo` shows most of what it does, and its Examples menu is a good place to start.

This page lists what can be written in a graph's text. `include/FunctionPlot.h` has the control's API.

The legend shows each graph's text as mathematics, with real roots, fractions and limits, unless the control has `FPLS_PLAINLEGEND`. `CFunctionPlotMathCtrl` shows one text the same way in a window of its own. A text that has an error, or doesn't fit, stays as it was typed. `GetGraphLatex` and `GetGraphMathML` give the same expression to paste elsewhere. They all use the usual symbols: a name like `alpha` is α, `gamma(x)` is Γ(x), `besselj(0, x)` is J₀(x), `nCr(n, r)` a binomial coefficient, and so on.

## Functions

An angle is in radians unless the control is in degrees (`SetDegrees`). In degrees, the functions marked ° take degrees, and the inverse ones return them.

A function with one argument doesn't need parentheses: `sin 2x` is `sin(2x)`, and `sin x/2` is `sin(x)/2`. The exceptions are in [Names that give way](#names-that-give-way).

### Trigonometric

| Function | Meaning |
|---|---|
| `sin(x)`, `cos(x)`, `tan(x)` | ° |
| `sec(x)`, `csc(x)`, `cot(x)` | ° Reciprocals. Also `cosec`, `tg` for `tan`, `ctg` for `cot` |
| `asin(x)`, `acos(x)`, `atan(x)` | ° Inverses. Also `arcsin`, `arccos`, `arctan`, `arctg`, and `sin^-1 x` and so on |
| `asec(x)`, `acsc(x)`, `acot(x)` | ° Inverses of the reciprocals. Also `arcsec`, `arccsc`, `arccot`, `arcctg`. `acot` runs from 0 to π without a jump |
| `atan2(y, x)` | ° The angle of the point (x, y) |

### Hyperbolic

| Function | Meaning |
|---|---|
| `sinh(x)`, `cosh(x)`, `tanh(x)` | |
| `sech(x)`, `csch(x)`, `coth(x)` | Reciprocals |
| `asinh(x)`, `acosh(x)`, `atanh(x)` | Inverses. Also `arsinh`, `arcosh`, `artanh`, `arcsinh`, `arccosh`, `arctanh`, and `sinh^-1 x` and so on |
| `asech(x)`, `acsch(x)`, `acoth(x)` | Inverses of the reciprocals. Also `arsech`, `arcsch`, `arcoth`, `arcsech`, `arccsch`, `arccoth` |

### Exponentials, logarithms and roots

| Function | Meaning |
|---|---|
| `exp(x)` | e to the x |
| `ln(x)` | Natural logarithm |
| `log(x)` | Base 10. Also `log10`, `lg` |
| `log(x, b)` | Base b |
| `log2(x)` | Base 2 |
| `sqrt(x)` | Square root. Also `√x` |
| `cbrt(x)` | Cube root, negative for a negative x |
| `root(n, x)` | The nth root |

### Rounding and parts of numbers

| Function | Meaning |
|---|---|
| `abs(x)` | Absolute value. Also `\|x\|` |
| `sign(x)` | -1, 0 or 1. Also `sgn` |
| `floor(x)`, `ceil(x)` | Down, up |
| `round(x)` | To the nearest whole number, halves away from 0 |
| `trunc(x)` | Toward 0 |
| `frac(x)` | `x - floor(x)`, so from 0 up to 1 |
| `mod(a, b)` | The remainder, with the sign of b. Undefined when b is 0 |
| `min(a, b, ...)`, `max(a, b, ...)` | Of any number of values |
| `clamp(x, a, b)` | x kept between a and b |
| `hypot(a, b)` | The square root of a² + b² |

### Special functions

| Function | Meaning |
|---|---|
| `gamma(x)` | Γ(x), with `x!` as Γ(x + 1) |
| `lgamma(x)` | The logarithm of \|Γ(x)\|, which goes on where `gamma` overflows |
| `digamma(x)` | The derivative of `lgamma`. It has no derivative of its own here |
| `erf(x)`, `erfc(x)` | The error function and 1 - erf(x), which stays accurate for large x |
| `sinc(x)` | sin(x)/x, and 1 at 0. Not the sin(πx)/(πx) of signal processing |

| `beta(a, b)` | Γ(a) Γ(b) / Γ(a + b) |
| `besselj(n, x)`, `bessely(n, x)` | Bessel functions of the first and second kinds, of any real order n. Negative x needs a whole order, and `bessely` positive x |
| `besseli(n, x)`, `besselk(n, x)` | The modified ones. The same limits, with `besselk` as `bessely` |
| `airyai(x)`, `airybi(x)` | Airy functions, the solutions of y'' = x y |
| `zeta(s)` | The Riemann zeta function, undefined at 1. It has no derivative here |
| `lambertw(x)`, `lambertwm1(x)` | The Lambert W function: the w with w e^w = x, on the branch from -1 up, and on the one from -1 down, for x from -1/e to 0 |
| `ellipk(m)`, `ellipe(m)` | Complete elliptic integrals of the first and second kinds, of the parameter m = k², as in MATLAB and SciPy, not the modulus k |
| `ellipf(φ, m)`, `ellipe(φ, m)` | The incomplete ones, with φ always in radians and m from 0 to 1. A derivative only by φ |

### Combinatorics

| Function | Meaning |
|---|---|
| `nCr(n, r)` | Combinations. Also `ncr`. Exact for whole numbers, and smooth between them by the gamma function |
| `nPr(n, r)` | Permutations. Also `npr`. The same |
| `gcd(a, b)`, `lcm(a, b)` | Of the arguments rounded to whole numbers, so they draw as steps |

### Waves and steps

| Function | Meaning |
|---|---|
| `square(x)` | ° Period 2π, as `sin`: 1 for the first half, -1 for the second |
| `triangle(x)` | ° Period 2π: 0 at 0, rising to 1 at π/2 |
| `sawtooth(x)` | ° Period 2π: 0 at 0, from -1 up to 1 |
| `step(x)` | 0 below 0, 1 from 0 on. Also `heaviside` |

### Normal distribution

| Function | Meaning |
|---|---|
| `normpdf(x)`, `normpdf(x, m)`, `normpdf(x, m, s)` | The density, with mean m (0 if not given) and standard deviation s (1 if not given) |
| `normcdf(x)`, `normcdf(x, m)`, `normcdf(x, m, s)` | The cumulative distribution, accurate far out in the tails |

A standard deviation of zero or less is undefined. Pick other names than `mean` for parameters, as it's taken by [lists](#lists).

### Conversions

| Function | Meaning |
|---|---|
| `deg(x)` | Radians to degrees |
| `rad(x)` | Degrees to radians |

### Calculus

| Written | Meaning |
|---|---|
| `d/dx sin x` | The derivative. Also `d/dy`, `d/dt`, `d/dθ` |
| `sin'(x)`, `sin''(x)`, `sin' 2x` | A function's own derivative at its argument, so `sin'(2x)` is `cos(2x)`, with no chain rule. Works for your own functions too, by their first parameter |
| `integral(f, a, b)` | The integral of f over x, from a to b |
| `integral(f, t, a, b)` | Over the variable named. Also `∫` |

### Sums and products

| Written | Meaning |
|---|---|
| `sum(f, k, a, b)` | f added up for k = a, a + 1, ... b. Also `∑` |
| `prod(f, k, a, b)` | Multiplied. Also `product`, `∏` |

The index can be any name except a variable's, and it means nothing outside its sum: `y = sum(x^k/k!, k, 0, 8)`. The limits are rounded to whole numbers and can be parameters. An empty sum is 0 and an empty product 1. More than 100,000 terms is undefined.

A curve with a long sum, or anything else that takes a while to work out, is drawn roughly at once and in full a moment later, and tracing finds its roots and extrema a moment later too. Its intersections with other curves aren't looked for if it's very slow.

A step-like function has a derivative of 0: `sign`, `floor`, `ceil`, `round`, `trunc`, `step`, `square`, `gcd` and `lcm`. A function of more than one argument can't take primes (use `d/dx`), and `sinc` has them up to the second.

### Lists

For a list such as `L = [1, 2, 3]`:

| Function | Meaning |
|---|---|
| `mean(L)`, `median(L)`, `stdev(L)` | |
| `sum(L)`, `count(L)` | |
| `min(L)`, `max(L)` | |

## What can be drawn

Besides functions, curves and equations, a tuple of three is drawn in 3D by its variables: `(x(t), y(t), z(t))` a curve, `(x(u, v), y(u, v), z(u, v))` a surface, `(a, b, c)` a point, and `(P, Q, R)` of x, y and z a vector field, as arrows through the box.

## Your own functions

`f(x) = x^2 - 1` defines a function that any other line can use, as `f(2x)`, `f'(x)` or `2fx`. It also draws, if its one parameter is `x`, `y`, `t` or `θ`.

`a = x - 1` is a substitution: `a` stands for all of that wherever it's used, so `2a` is `2(x - 1)`, and it draws nothing itself.

### Names that give way

Most of the newer names can still be your own. If a parameter or function has the name, or a line defines it (`step = 2`, `square(x) = x^2`), it's yours in every line. These are the names:

`square triangle sawtooth asec acsc acot sech csch coth asech acsch acoth trunc lgamma erfc nCr nPr ncr npr gcd lcm step heaviside sinc normpdf normcdf frac clamp deg rad log10 arcsec arccsc arccot arsech arcsch arcoth arcsech arccsch arccoth cosec tg ctg arctg arcctg prod product beta besselj bessely besseli besselk airyai airybi zeta lambertw lambertwm1 ellipk ellipe ellipf`

The shorter ones read as products of letters unless they're called with parentheses, so `(t, tg)` is still t times g: `tg ctg deg rad gcd lcm nCr nPr ncr npr beta zeta`. The other function names can't be redefined.

## Numbers and variables

| Written | Meaning |
|---|---|
| `pi` or `π`, `tau` or `τ`, `e` | Constants |
| `x`, `y`, `z`, `t`, `r`, `u`, `v` | Variables |
| `theta` or `θ` | The angle. Also `q`, unless `q` is your own name or its line has a variable other than `r`. A slider the app only added for another line (`FPLPF_AUTO`) leaves `q` as the angle in a line with `r` |

Any other name is a parameter, which the app defines. The demo adds a slider for each one. The name of a Greek letter, such as `alpha` or `mu`, is one name unless its letters are all known names, and shows as the letter.

## Operators

| Written | Meaning |
|---|---|
| `+ - * /` | The usual ones. Also `·` and `×` |
| `^` | Power, right associative and tighter than a leading minus, so `-x^2` is `-(x^2)`. Also `x²` |
| `2x`, `x(x + 1)`, `2 sin x cos x` | Products without a sign |
| `x!` | Factorial |
| `\|x\|` | Absolute value |
| `{x > 0: x, -x}` | Piecewise: the first true condition's value, or the last value otherwise |
| `x^2 {0 < x < 2}` | Restricted to where the condition holds |
| `= < > <= >=` | Equations and inequalities. Also `≤` and `≥` |
