// GCSE-friendly arithmetic for Bumble.
//
// Understands spoken numbers ("one hundred and five", "three point five"),
// precedence, parentheses, powers, roots, factorials, percentages, standard
// form, degrees trigonometry, logs and a few named helpers (mean, median,
// mode, range, pythagoras).  Everything runs through libm.
#include "math_eval.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOK 160

typedef enum { TK_WORD, TK_NUM, TK_SYM } tk_type;

typedef struct {
    tk_type t;
    char w[32];
    double v;
    char sym;
} token_t;

/* --------------------------------------------------------------- tokens */

static int tokenize(const char *s, token_t *t, int max)
{
    int n = 0;
    for (size_t i = 0; s[i] && n < max;) {
        unsigned char c = (unsigned char)s[i];
        if (isspace(c) || c == ',' || c == '?' || c == '\'' ||
            c == '"' || c == ':' || c == ';') {
            i++;
            continue;
        }
        if (isalpha(c)) {
            size_t j = 0;
            char *w = t[n].w;
            while (s[i] && isalpha((unsigned char)s[i]) && j + 1 < sizeof t[n].w) {
                w[j++] = (char)tolower((unsigned char)s[i++]);
            }
            w[j] = 0;
            t[n].t = TK_WORD;
            n++;
            continue;
        }
        if (isdigit(c) || (c == '.' && isdigit((unsigned char)s[i + 1]))) {
            char *end = NULL;
            t[n].t = TK_NUM;
            t[n].v = strtod(s + i, &end);
            i = (size_t)(end - s);
            n++;
            continue;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' ||
            c == '(' || c == ')' || c == '!' || c == '%' || c == '.') {
            t[n++] = (token_t){.t = TK_SYM, .sym = (char)c};
            i++;
            continue;
        }
        i++;  // anything else is noise
    }
    return n;
}

static bool is_number_word(const char *w, double *val, int *kind)
{
    static const struct { const char *w; double v; int k; } W[] = {
        {"zero", 0, 0}, {"one", 1, 0}, {"two", 2, 0}, {"three", 3, 0},
        {"four", 4, 0}, {"five", 5, 0}, {"six", 6, 0}, {"seven", 7, 0},
        {"eight", 8, 0}, {"nine", 9, 0}, {"ten", 10, 0}, {"eleven", 11, 0},
        {"twelve", 12, 0}, {"thirteen", 13, 0}, {"fourteen", 14, 0},
        {"fifteen", 15, 0}, {"sixteen", 16, 0}, {"seventeen", 17, 0},
        {"eighteen", 18, 0}, {"nineteen", 19, 0}, {"twenty", 20, 0},
        {"thirty", 30, 0}, {"forty", 40, 0}, {"fifty", 50, 0}, {"sixty", 60, 0},
        {"seventy", 70, 0}, {"eighty", 80, 0}, {"ninety", 90, 0},
        {"hundred", 100, 1}, {"thousand", 1000, 2}, {"million", 1000000, 2},
        {"billion", 1000000000, 2}, {"trillion", 1000000000000.0, 2},
        {"point", 0, 3},
    };
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        if (!strcmp(w, W[i].w)) {
            *val = W[i].v;
            *kind = W[i].k;
            return true;
        }
    }
    return false;
}

// Fold runs of number words into numbers: "twenty five", "one hundred and
// five", "three point five".
static int fold_numbers(token_t *in, int n, token_t *out)
{
    int m = 0;
    for (int i = 0; i < n;) {
        double v = 0;
        int kind = 0;
        if (in[i].t == TK_WORD && is_number_word(in[i].w, &v, &kind)) {
            double total = 0, cur = 0, frac = 0, place = 0.1;
            bool any = false, in_frac = false;
            while (i < n) {
                if (in[i].t == TK_WORD && !strcmp(in[i].w, "and")) {
                    if (i + 1 < n && in[i + 1].t == TK_WORD &&
                        is_number_word(in[i + 1].w, &v, &kind) && kind != 3) {
                        i++;
                        continue;
                    }
                    break;
                }
                if (in[i].t != TK_WORD || !is_number_word(in[i].w, &v, &kind)) break;
                if (kind == 3) { in_frac = true; i++; continue; }
                if (in_frac) {
                    frac += v * place;
                    place /= 10;
                } else if (kind == 1) {
                    cur = (cur == 0 ? 1 : cur) * 100;
                } else if (kind == 2) {
                    total += (cur == 0 ? 1 : cur) * v;
                    cur = 0;
                } else {
                    cur += v;
                }
                any = true;
                i++;
            }
            if (any) {
                out[m++] = (token_t){.t = TK_NUM, .v = total + cur + frac};
            }
            continue;
        }
        out[m++] = in[i++];
    }
    return m;
}

// "3 . 5" (speech-to-text split a decimal) -> 3.5
static int merge_decimals(token_t *t, int n)
{
    for (int i = 0; i + 2 < n; i++) {
        if (t[i].t == TK_NUM && t[i + 1].t == TK_SYM && t[i + 1].sym == '.' &&
            t[i + 2].t == TK_NUM) {
            double frac = t[i + 2].v;
            double scale = 1;
            while (scale <= frac) scale *= 10;
            t[i].v += frac / scale;
            memmove(&t[i + 1], &t[i + 3], (size_t)(n - i - 3) * sizeof t[0]);
            n -= 2;
        }
    }
    return n;
}

/* -------------------------------------------------------------- phrases */

typedef struct { int n; const char *w[4]; const char *out; } phrase_t;

static const phrase_t PHRASES[] = {
    {4, {"raised", "to", "the", "power"}, "^"},
    {3, {"to", "the", "power"}, "^"},
    {3, {"square", "root", "of"}, "sqrt"},
    {2, {"square", "root"}, "sqrt"},
    {3, {"cube", "root", "of"}, "cbrt"},
    {2, {"cube", "root"}, "cbrt"},
    {2, {"multiplied", "by"}, "*"},
    {2, {"divided", "by"}, "/"},
    {2, {"take", "away"}, "-"},
    {2, {"open", "bracket"}, "("},
    {2, {"open", "brackets"}, "("},
    {2, {"close", "bracket"}, ")"},
    {2, {"close", "brackets"}, ")"},
    {2, {"open", "parenthesis"}, "("},
    {2, {"close", "parenthesis"}, ")"},
    {2, {"sine", "of"}, "sin"},
    {2, {"cosine", "of"}, "cos"},
    {2, {"tangent", "of"}, "tan"},
    {2, {"arc", "sine"}, "asin"},
    {2, {"arc", "cosine"}, "acos"},
    {2, {"arc", "tangent"}, "atan"},
    {2, {"inverse", "sine"}, "asin"},
    {2, {"inverse", "cosine"}, "acos"},
    {2, {"inverse", "tangent"}, "atan"},
    {2, {"natural", "log"}, "log"},
    {2, {"log", "of"}, "log10"},
    {2, {"percent", "of"}, "% *"},
    {2, {"per", "cent"}, "%"},
    {2, {"to", "the"}, "^"},
};

static const struct { const char *w; const char *out; } WORDS[] = {
    {"plus", "+"}, {"add", "+"}, {"minus", "-"}, {"subtract", "-"},
    {"negative", "-"}, {"and", "+"}, {"half", "0.5"}, {"quarter", "0.25"},
    {"times", "*"}, {"multiply", "*"}, {"x", "*"}, {"over", "/"},
    {"divide", "/"}, {"power", "^"}, {"squared", "^ 2"}, {"cubed", "^ 3"},
    {"square", "sqrt"}, {"factorial", "!"}, {"percent", "%"}, {"per", "%"},
    {"sine", "sin"}, {"sin", "sin"}, {"cosine", "cos"}, {"cos", "cos"},
    {"tangent", "tan"}, {"tan", "tan"}, {"asin", "asin"}, {"acos", "acos"},
    {"atan", "atan"}, {"log", "log10"}, {"log10", "log10"}, {"ln", "log"},
    {"exp", "exp"}, {"abs", "abs"}, {"floor", "floor"}, {"ceil", "ceil"},
    {"ceiling", "ceil"}, {"round", "round"}, {"sqrt", "sqrt"}, {"cbrt", "cbrt"},
    {"pi", "pi"}, {"e", "e"}, {"equals", ""}, {"equal", ""},
    {"open", "("}, {"close", ")"}, {"bracket", ""}, {"brackets", ""},
};

static void append(char *out, size_t n, const char *s)
{
    size_t l = strlen(out);
    if (l + 1 >= n) return;
    snprintf(out + l, n - l, "%s", s);
}

// Turn folded tokens into a parser-friendly expression.
static void build_expr(token_t *t, int n, char *out, size_t outn)
{
    out[0] = 0;
    for (int i = 0; i < n;) {
        if (t[i].t == TK_NUM) {
            char buf[40];
            if (t[i].v == floor(t[i].v) && fabs(t[i].v) < 1e15) {
                snprintf(buf, sizeof buf, "%lld ", (long long)t[i].v);
            } else {
                snprintf(buf, sizeof buf, "%.10g ", t[i].v);
            }
            append(out, outn, buf);
            i++;
            continue;
        }
        if (t[i].t == TK_SYM) {
            if (t[i].sym == '.') { i++; continue; }
            char buf[4] = {t[i].sym, ' ', 0};
            append(out, outn, buf);
            i++;
            continue;
        }
        // "log base ten" and "log ten"
        if (!strcmp(t[i].w, "log") && i + 2 < n && t[i + 1].t == TK_WORD &&
            !strcmp(t[i + 1].w, "base") && t[i + 2].t == TK_NUM &&
            (int)t[i + 2].v == 10) {
            append(out, outn, "log10 ");
            i += 3;
            continue;
        }
        if (!strcmp(t[i].w, "log") && i + 2 < n && t[i + 1].t == TK_NUM &&
            (int)t[i + 1].v == 10 && (t[i + 2].t == TK_NUM ||
            (t[i + 2].t == TK_SYM && t[i + 2].sym == '('))) {
            append(out, outn, "log10 ");
            i += 2;
            continue;
        }
        bool matched = false;
        for (size_t p = 0; p < sizeof PHRASES / sizeof PHRASES[0]; p++) {
            if (i + PHRASES[p].n > n) continue;
            bool ok = true;
            for (int k = 0; k < PHRASES[p].n; k++) {
                if (t[i + k].t != TK_WORD || strcmp(t[i + k].w, PHRASES[p].w[k])) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                append(out, outn, PHRASES[p].out);
                append(out, outn, " ");
                i += PHRASES[p].n;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        bool known = false;
        for (size_t p = 0; p < sizeof WORDS / sizeof WORDS[0]; p++) {
            if (!strcmp(t[i].w, WORDS[p].w)) {
                append(out, outn, WORDS[p].out);
                append(out, outn, " ");
                known = true;
                break;
            }
        }
        i++;
        (void)known;  // unknown words are dropped: "what is ..." etc.
    }
}

/* --------------------------------------------------------------- parser */

typedef struct {
    const char *s;
    size_t i;
    bool radians;
    const char *err;
} parser_t;

static void skip_ws(parser_t *p)
{
    while (p->s[p->i] == ' ') p->i++;
}

static double parse_expr(parser_t *p);

static double parse_unary(parser_t *p);

static double parse_primary(parser_t *p)
{
    skip_ws(p);
    if (p->err) return 0;

    if (p->s[p->i] == '(') {
        p->i++;
        double v = parse_expr(p);
        skip_ws(p);
        if (p->s[p->i] == ')') p->i++;
        else p->err = "I lost a bracket somewhere.";
        return v;
    }

    if (isdigit((unsigned char)p->s[p->i]) || p->s[p->i] == '.') {
        char *end = NULL;
        double v = strtod(p->s + p->i, &end);
        p->i = (size_t)(end - p->s);
        return v;
    }

    if (isalpha((unsigned char)p->s[p->i])) {
        char name[16];
        size_t j = 0;
        while (isalnum((unsigned char)p->s[p->i]) && j + 1 < sizeof name) {
            name[j++] = p->s[p->i++];
        }
        name[j] = 0;

        if (!strcmp(name, "pi")) return M_PI;
        if (!strcmp(name, "e")) return M_E;

        // Functions consume one unary argument: "sin 30", "sqrt(2)".
        skip_ws(p);
        double arg;
        if (p->s[p->i] == '(') {
            p->i++;
            arg = parse_expr(p);
            skip_ws(p);
            if (p->s[p->i] == ')') p->i++;
            else p->err = "I lost a bracket somewhere.";
        } else {
            arg = parse_unary(p);
        }
        if (p->err) return 0;

        double to_rad = p->radians ? 1.0 : M_PI / 180.0;
        double from_rad = p->radians ? 1.0 : 180.0 / M_PI;
        if (!strcmp(name, "sqrt")) {
            if (arg < 0) { p->err = "That would be imaginary. I only do real numbers."; return 0; }
            return sqrt(arg);
        }
        if (!strcmp(name, "cbrt")) return cbrt(arg);
        if (!strcmp(name, "sin")) return sin(arg * to_rad);
        if (!strcmp(name, "cos")) return cos(arg * to_rad);
        if (!strcmp(name, "tan")) {
            double deg = p->radians ? arg * 180.0 / M_PI : arg;
            double m = fmod(deg, 180.0);
            if (m < 0) m += 180.0;
            if (fabs(m - 90.0) < 1e-9) {
                p->err = "The tangent of ninety degrees is undefined.";
                return 0;
            }
            return tan(arg * to_rad);
        }
        if (!strcmp(name, "asin")) {
            if (arg < -1 || arg > 1) { p->err = "That angle is outside the range I can invert."; return 0; }
            return asin(arg) * from_rad;
        }
        if (!strcmp(name, "acos")) {
            if (arg < -1 || arg > 1) { p->err = "That angle is outside the range I can invert."; return 0; }
            return acos(arg) * from_rad;
        }
        if (!strcmp(name, "atan")) return atan(arg) * from_rad;
        if (!strcmp(name, "log")) {
            if (arg <= 0) { p->err = "Logarithms only work on positive numbers."; return 0; }
            return log(arg);
        }
        if (!strcmp(name, "log10")) {
            if (arg <= 0) { p->err = "Logarithms only work on positive numbers."; return 0; }
            return log10(arg);
        }
        if (!strcmp(name, "exp")) return exp(arg);
        if (!strcmp(name, "abs")) return fabs(arg);
        if (!strcmp(name, "floor")) return floor(arg);
        if (!strcmp(name, "ceil")) return ceil(arg);
        if (!strcmp(name, "round")) return round(arg);
        p->err = "I don't know that function.";
        return 0;
    }

    p->err = "I couldn't find a number there.";
    return 0;
}

static double parse_postfix(parser_t *p)
{
    double v = parse_primary(p);
    for (;;) {
        skip_ws(p);
        if (p->s[p->i] == '!') {
            p->i++;
            if (v < 0 || v > 170 || floor(v) != v) {
                p->err = "Factorial wants a whole number from zero to a hundred and seventy.";
                return 0;
            }
            v = tgamma(v + 1.0);
        } else if (p->s[p->i] == '%') {
            p->i++;
            v /= 100.0;
        } else {
            break;
        }
    }
    return v;
}

static double parse_power(parser_t *p)
{
    double base = parse_postfix(p);
    skip_ws(p);
    if (p->s[p->i] == '^') {
        p->i++;
        double ex = parse_unary(p);
        return pow(base, ex);
    }
    return base;
}

static double parse_unary(parser_t *p)
{
    skip_ws(p);
    if (p->s[p->i] == '-') {
        p->i++;
        return -parse_unary(p);
    }
    if (p->s[p->i] == '+') {
        p->i++;
        return parse_unary(p);
    }
    return parse_power(p);
}

static double parse_term(parser_t *p)
{
    double v = parse_unary(p);
    for (;;) {
        skip_ws(p);
        char c = p->s[p->i];
        if (c == '*' || c == '/') {
            p->i++;
            double rhs = parse_unary(p);
            if (p->err) return 0;
            if (c == '/' && rhs == 0) {
                p->err = "Dividing by zero is not allowed.";
                return 0;
            }
            v = (c == '*') ? v * rhs : v / rhs;
            continue;
        }
        // implicit multiplication: 2 pi, 3(4+5), 2 sin 30
        if (isdigit((unsigned char)c) || c == '(' || c == '.' ||
            isalpha((unsigned char)c)) {
            double rhs = parse_power(p);
            if (p->err) return 0;
            v *= rhs;
            continue;
        }
        break;
    }
    return v;
}

static double parse_expr(parser_t *p)
{
    double v = parse_term(p);
    for (;;) {
        skip_ws(p);
        char c = p->s[p->i];
        if (c == '+' || c == '-') {
            p->i++;
            double rhs = parse_term(p);
            if (p->err) return 0;
            v = (c == '+') ? v + rhs : v - rhs;
            continue;
        }
        break;
    }
    return v;
}

/* ---------------------------------------------------------------- extras */

static int collect_numbers(token_t *t, int n, double *vals, int max)
{
    int m = 0;
    for (int i = 0; i < n && m < max; i++) {
        if (t[i].t == TK_NUM) vals[m++] = t[i].v;
    }
    return m;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static bool stats_case(token_t *t, int n, double *out)
{
    if (n < 3) return false;
    const char *op = NULL;
    int words = 0;
    for (int i = 0; i < n && words < 4; i++) {
        if (t[i].t != TK_WORD) continue;
        words++;
        if (!strcmp(t[i].w, "mean") || !strcmp(t[i].w, "average") ||
            !strcmp(t[i].w, "median") || !strcmp(t[i].w, "mode") ||
            !strcmp(t[i].w, "range") || !strcmp(t[i].w, "sum") ||
            !strcmp(t[i].w, "total")) {
            op = t[i].w;
            break;
        }
    }
    if (!op) return false;
    double vals[32];
    int m = collect_numbers(t, n, vals, 32);
    if (m < 1) return false;

    if (!strcmp(op, "sum") || !strcmp(op, "total")) {
        double s = 0;
        for (int i = 0; i < m; i++) s += vals[i];
        *out = s;
        return true;
    }
    if (!strcmp(op, "mean") || !strcmp(op, "average")) {
        double s = 0;
        for (int i = 0; i < m; i++) s += vals[i];
        *out = s / m;
        return true;
    }
    if (!strcmp(op, "range")) {
        double lo = vals[0], hi = vals[0];
        for (int i = 1; i < m; i++) {
            if (vals[i] < lo) lo = vals[i];
            if (vals[i] > hi) hi = vals[i];
        }
        *out = hi - lo;
        return true;
    }
    if (!strcmp(op, "median")) {
        qsort(vals, (size_t)m, sizeof vals[0], cmp_double);
        *out = (m & 1) ? vals[m / 2] : (vals[m / 2 - 1] + vals[m / 2]) / 2;
        return true;
    }
    if (!strcmp(op, "mode")) {
        double best = vals[0];
        int best_n = 0;
        for (int i = 0; i < m; i++) {
            int c = 0;
            for (int j = 0; j < m; j++) if (vals[j] == vals[i]) c++;
            if (c > best_n) { best_n = c; best = vals[i]; }
        }
        *out = best;
        return true;
    }
    return false;
}

static bool percent_case(token_t *t, int n, double *out)
{
    if (n < 4 || t[0].t != TK_WORD) return false;
    bool up = !strcmp(t[0].w, "increase");
    bool down = !strcmp(t[0].w, "decrease");
    if (!up && !down) return false;
    bool has_pct = false;
    for (int i = 0; i < n; i++) {
        if ((t[i].t == TK_WORD && !strcmp(t[i].w, "percent")) ||
            (t[i].t == TK_SYM && t[i].sym == '%')) has_pct = true;
    }
    if (!has_pct) return false;
    double vals[4];
    int m = collect_numbers(t, n, vals, 4);
    if (m < 2) return false;
    double base = vals[0], pct = vals[1];
    *out = base * (1.0 + (up ? 1.0 : -1.0) * pct / 100.0);
    return true;
}

static bool pythag_case(token_t *t, int n, double *out)
{
    bool named = false;
    for (int i = 0; i < n; i++) {
        if (t[i].t == TK_WORD &&
            (!strcmp(t[i].w, "hypotenuse") || !strcmp(t[i].w, "pythagoras"))) {
            named = true;
        }
    }
    if (!named) return false;
    double vals[4];
    int m = collect_numbers(t, n, vals, 4);
    if (m < 2) return false;
    *out = hypot(vals[0], vals[1]);
    return true;
}


/* --------------------------------------------------------- GCSE extras */

static bool has_word(const token_t *t, int n, const char *w)
{
    for (int i = 0; i < n; i++) {
        if (t[i].t == TK_WORD && !strcmp(t[i].w, w)) return true;
    }
    return false;
}

static void frac_str(double v, char *out, size_t on)
{
    out[0] = 0;
    double x = fabs(v);
    if (x < 1e-9 || x > 1e6) return;
    long h1 = 1, h0 = 0, k1 = 0, k0 = 1;
    double b = x;
    for (int i = 0; i < 24; i++) {
        long a = (long)floor(b);
        long h2 = a * h1 + h0, k2 = a * k1 + k0;
        if (k2 > 1000) break;
        h0 = h1; h1 = h2; k0 = k1; k1 = k2;
        if (fabs(x - (double)h1 / (double)k1) <= 1e-9 * (x > 1 ? x : 1)) break;
        double f = b - (double)a;
        if (f < 1e-12) break;
        b = 1.0 / f;
    }
    if (k1 < 2 || k1 > 100) return;
    if (fabs(x - (double)h1 / (double)k1) > 1e-9 * (x > 1 ? x : 1)) return;
    long n = (v < 0) ? -h1 : h1;
    long d = k1;
    long whole = n / d;
    long rem = labs(n % d);
    if (whole) snprintf(out, on, "%ld and %ld over %ld", whole, rem, d);
    else snprintf(out, on, "%ld over %ld", n, d);
}

static void say_result(double v, char *out, size_t on)
{
    char num[96], fr[64];
    math_format(v, num, sizeof num);
    frac_str(v, fr, sizeof fr);
    if (fr[0] && fabs(v) < 1000) snprintf(out, on, "That is %s, or %s.", num, fr);
    else snprintf(out, on, "That is %s.", num);
}

static void say_plain(double v, char *out, size_t on)
{
    char num[96];
    math_format(v, num, sizeof num);
    snprintf(out, on, "That is %s.", num);
}

static int collect_signed(const token_t *t, int n, double *vals, int max)
{
    int m = 0, sign = 1;
    for (int i = 0; i < n && m < max; i++) {
        if (t[i].t == TK_SYM && (t[i].sym == '-' || t[i].sym == '+')) {
            sign = t[i].sym == '-' ? -1 : 1;
            continue;
        }
        if (t[i].t == TK_WORD && (!strcmp(t[i].w, "minus") || !strcmp(t[i].w, "negative"))) {
            sign = -1;
            continue;
        }
        if (t[i].t == TK_NUM) {
            vals[m++] = sign * t[i].v;
            sign = 1;
        } else {
            sign = 1;
        }
    }
    return m;
}

static double median_sorted(const double *v, int n)
{
    if (n <= 0) return 0;
    return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

static bool iqr_case(token_t *t, int n, char *out, size_t on)
{
    bool wants = has_word(t, n, "interquartile") || has_word(t, n, "iqr") ||
                 has_word(t, n, "quartile") || has_word(t, n, "quartiles");
    if (!wants) return false;
    double v[32];
    int m = collect_numbers(t, n, v, 32);
    if (m < 4) return false;
    qsort(v, (size_t)m, sizeof v[0], cmp_double);
    int half = m / 2;
    int lo = (m + 1) / 2;
    double q1 = median_sorted(v, half);
    double q2 = median_sorted(v, m);
    double q3 = median_sorted(v + lo, m - lo);
    char a[32], b[32], c[32], d[32];
    math_format(q1, a, sizeof a);
    math_format(q2, b, sizeof b);
    math_format(q3, c, sizeof c);
    math_format(q3 - q1, d, sizeof d);
    if (has_word(t, n, "iqr") || has_word(t, n, "interquartile")) {
        snprintf(out, on, "The interquartile range is %s.", d);
    } else {
        snprintf(out, on, "The lower quartile is %s, the median is %s, and the upper quartile is %s.",
                 a, b, c);
    }
    return true;
}

static double binom(int n, int k)
{
    if (k < 0 || k > n) return 0;
    double r = 1;
    for (int i = 1; i <= k; i++) r = r * (n - k + i) / i;
    return r;
}

static bool probability_case(token_t *t, int n, char *out, size_t on)
{
    if (!has_word(t, n, "probability")) return false;
    double v[8];
    int m = collect_numbers(t, n, v, 8);
    if (has_word(t, n, "head") || has_word(t, n, "heads") ||
        has_word(t, n, "tail") || has_word(t, n, "tails")) {
        if (m < 2) return false;
        int want = (int)v[0], flips = (int)v[1];
        if (want < 0 || flips <= 0 || want > flips) return false;
        say_result(binom(flips, want) / pow(2.0, flips), out, on);
        return true;
    }
    if (has_word(t, n, "dice") || has_word(t, n, "die") ||
        has_word(t, n, "roll") || has_word(t, n, "rolling")) {
        say_result(has_word(t, n, "not") ? 5.0 / 6.0 : 1.0 / 6.0, out, on);
        return true;
    }
    if (has_word(t, n, "coin") || has_word(t, n, "heads") || has_word(t, n, "tails")) {
        say_result(0.5, out, on);
        return true;
    }
    return false;
}

static bool round_case(token_t *t, int n, char *out, size_t on)
{
    if (!has_word(t, n, "round") && !has_word(t, n, "rounded")) return false;
    double v[8];
    int m = collect_numbers(t, n, v, 8);
    if (m < 1) return false;
    double x = v[0];
    if (has_word(t, n, "decimal")) {
        int dp = m >= 2 ? (int)v[1] : 0;
        double f = pow(10.0, dp);
        x = round(x * f) / f;
    } else if (has_word(t, n, "significant") || has_word(t, n, "sig")) {
        int sf = m >= 2 ? (int)v[1] : 1;
        if (sf < 1) sf = 1;
        if (x != 0) {
            int digits = (int)floor(log10(fabs(x))) + 1;
            double f = pow(10.0, digits - sf);
            x = round(x / f) * f;
        }
    } else if (has_word(t, n, "nearest")) {
        double unit = m >= 2 ? v[1] : 1;
        if (unit == 0) unit = 1;
        x = round(x / unit) * unit;
    } else {
        return false;
    }
    say_plain(x, out, on);
    return true;
}

static bool bounds_case(token_t *t, int n, char *out, size_t on)
{
    if (!has_word(t, n, "bound") && !has_word(t, n, "bounds") &&
        !has_word(t, n, "interval")) return false;
    double v[8];
    int m = collect_numbers(t, n, v, 8);
    if (m < 1) return false;
    double x = v[0], unit = 1;
    if (has_word(t, n, "decimal")) {
        int dp = m >= 2 ? (int)v[1] : 0;
        unit = pow(10.0, -dp);
    } else if (has_word(t, n, "significant") || has_word(t, n, "sig")) {
        int sf = m >= 2 ? (int)v[1] : 1;
        int digits = (int)floor(log10(fabs(x))) + 1;
        unit = pow(10.0, digits - sf);
    } else if (has_word(t, n, "nearest")) {
        unit = m >= 2 ? v[1] : 1;
    }
    double half = unit / 2;
    char a[32], b[32];
    math_format(x - half, a, sizeof a);
    math_format(x + half, b, sizeof b);
    if (has_word(t, n, "upper")) snprintf(out, on, "The upper bound is %s.", b);
    else if (has_word(t, n, "lower")) snprintf(out, on, "The lower bound is %s.", a);
    else snprintf(out, on, "%s to %s.", a, b);
    return true;
}

static long gcd_long(long a, long b)
{
    a = labs(a); b = labs(b);
    while (b) { long r = a % b; a = b; b = r; }
    return a ? a : 1;
}

static bool ratio_case(token_t *t, int n, char *out, size_t on)
{
    if (!has_word(t, n, "ratio")) return false;
    double v[8];
    int m = collect_numbers(t, n, v, 8);
    if (m < 2) return false;

    if (has_word(t, n, "simplify")) {
        long g = (long)v[0];
        for (int i = 1; i < m; i++) g = gcd_long(g, (long)v[i]);
        out[0] = 0;
        for (int i = 0; i < m; i++) {
            char b[24];
            snprintf(b, sizeof b, "%ld%s", (long)v[i] / g, i + 1 < m ? " to " : ".");
            strncat(out, b, on - strlen(out) - 1);
        }
        return true;
    }

    int first_part = 1;
    long total_parts = 0;
    if (has_word(t, n, "share")) {
        for (int i = 1; i < m; i++) total_parts += (long)v[i];
        if (total_parts <= 0) return false;
        double total = v[0];
        out[0] = 0;
        for (int i = 1; i < m; i++) {
            char b[64];
            char num[48];
            math_format(total * v[i] / total_parts, num, sizeof num);
            snprintf(b, sizeof b, "%s%s", num, i + 1 < m ? " and " : ".");
            strncat(out, b, on - strlen(out) - 1);
        }
        return true;
    }
    (void)first_part;
    return false;
}

static bool prime_check(long n)
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (long i = 3; i * i <= n; i += 2) if (n % i == 0) return false;
    return true;
}

static bool number_theory_case(token_t *t, int n, char *out, size_t on)
{
    bool hcf = has_word(t, n, "hcf") || has_word(t, n, "gcd") ||
               (has_word(t, n, "highest") && has_word(t, n, "common"));
    bool lcm = has_word(t, n, "lcm") ||
               (has_word(t, n, "lowest") && has_word(t, n, "common"));
    bool pf = has_word(t, n, "prime") &&
              (has_word(t, n, "factor") || has_word(t, n, "factors") ||
               has_word(t, n, "factorise") || has_word(t, n, "factorize"));
    bool pr = has_word(t, n, "prime") && !pf;

    if (!hcf && !lcm && !pf && !pr) return false;
    double v[8];
    int m = collect_numbers(t, n, v, 8);
    if (m < 1) return false;

    if (hcf || lcm) {
        long a = (long)v[0];
        for (int i = 1; i < m; i++) {
            if (hcf) a = gcd_long(a, (long)v[i]);
            else a = labs(a * (long)v[i]) / gcd_long(a, (long)v[i]);
        }
        snprintf(out, on, "The %s is %ld.", hcf ? "highest common factor" : "lowest common multiple", a);
        return true;
    }
    if (pf) {
        long x = (long)v[0];
        if (x < 2) { snprintf(out, on, "That has no prime factors."); return true; }
        out[0] = 0;
        for (long p = 2; p * p <= x; p++) {
            int e = 0;
            while (x % p == 0) { x /= p; e++; }
            if (!e) continue;
            char b[40];
            if (e == 1) snprintf(b, sizeof b, "%ld", p);
            else snprintf(b, sizeof b, "%ld squared", p);
            if (out[0]) strncat(out, " times ", on - strlen(out) - 1);
            strncat(out, b, on - strlen(out) - 1);
        }
        if (x > 1) {
            if (out[0]) strncat(out, " times ", on - strlen(out) - 1);
            char b[24];
            snprintf(b, sizeof b, "%ld", x);
            strncat(out, b, on - strlen(out) - 1);
        }
        strncat(out, ".", on - strlen(out) - 1);
        char head[80];
        snprintf(head, sizeof head, "The prime factors of %ld are ", (long)v[0]);
        char full[256];
        snprintf(full, sizeof full, "%s%s", head, out);
        snprintf(out, on, "%s", full);
        return true;
    }
    if (pr) {
        long x = (long)v[0];
        if (prime_check(x)) snprintf(out, on, "Yes, %ld is prime.", x);
        else snprintf(out, on, "No, %ld is not prime.", x);
        return true;
    }
    return false;
}

enum { U_NONE = 0, U_LEN, U_MASS, U_VOL, U_TIME, U_TEMP, U_SPEED };

typedef struct { const char *w; int cat; double f; const char *name; } unit_t;

static const unit_t UNITS[] = {
    {"mm", U_LEN, 0.001, "millimetres"}, {"millimetre", U_LEN, 0.001, "millimetres"},
    {"millimetres", U_LEN, 0.001, "millimetres"}, {"cm", U_LEN, 0.01, "centimetres"},
    {"centimetre", U_LEN, 0.01, "centimetres"}, {"centimetres", U_LEN, 0.01, "centimetres"},
    {"m", U_LEN, 1, "metres"}, {"metre", U_LEN, 1, "metres"}, {"metres", U_LEN, 1, "metres"},
    {"km", U_LEN, 1000, "kilometres"}, {"kilometre", U_LEN, 1000, "kilometres"},
    {"kilometres", U_LEN, 1000, "kilometres"}, {"inch", U_LEN, 0.0254, "inches"},
    {"inches", U_LEN, 0.0254, "inches"}, {"foot", U_LEN, 0.3048, "feet"},
    {"feet", U_LEN, 0.3048, "feet"}, {"yard", U_LEN, 0.9144, "yards"},
    {"yards", U_LEN, 0.9144, "yards"}, {"mile", U_LEN, 1609.344, "miles"},
    {"miles", U_LEN, 1609.344, "miles"},
    {"mg", U_MASS, 0.001, "milligrams"}, {"g", U_MASS, 1, "grams"},
    {"gram", U_MASS, 1, "grams"}, {"grams", U_MASS, 1, "grams"},
    {"kg", U_MASS, 1000, "kilograms"}, {"kilogram", U_MASS, 1000, "kilograms"},
    {"kilograms", U_MASS, 1000, "kilograms"}, {"pound", U_MASS, 453.592, "pounds"},
    {"pounds", U_MASS, 453.592, "pounds"}, {"lb", U_MASS, 453.592, "pounds"},
    {"lbs", U_MASS, 453.592, "pounds"}, {"ounce", U_MASS, 28.3495, "ounces"},
    {"ounces", U_MASS, 28.3495, "ounces"}, {"oz", U_MASS, 28.3495, "ounces"},
    {"stone", U_MASS, 6350.29, "stone"}, {"stones", U_MASS, 6350.29, "stone"},
    {"ml", U_VOL, 0.001, "millilitres"}, {"millilitre", U_VOL, 0.001, "millilitres"},
    {"l", U_VOL, 1, "litres"}, {"litre", U_VOL, 1, "litres"}, {"litres", U_VOL, 1, "litres"},
    {"pint", U_VOL, 0.568261, "pints"}, {"pints", U_VOL, 0.568261, "pints"},
    {"second", U_TIME, 1, "seconds"}, {"seconds", U_TIME, 1, "seconds"},
    {"sec", U_TIME, 1, "seconds"}, {"minute", U_TIME, 60, "minutes"},
    {"minutes", U_TIME, 60, "minutes"}, {"min", U_TIME, 60, "minutes"},
    {"hour", U_TIME, 3600, "hours"}, {"hours", U_TIME, 3600, "hours"},
    {"day", U_TIME, 86400, "days"}, {"days", U_TIME, 86400, "days"},
    {"celsius", U_TEMP, 0, "degrees Celsius"}, {"centigrade", U_TEMP, 0, "degrees Celsius"},
    {"c", U_TEMP, 0, "degrees Celsius"}, {"fahrenheit", U_TEMP, 0, "degrees Fahrenheit"},
    {"f", U_TEMP, 0, "degrees Fahrenheit"},
    {"mph", U_SPEED, 0.44704, "miles per hour"},
    {"kmh", U_SPEED, 0.2777778, "kilometres per hour"},
    {"mps", U_SPEED, 1, "metres per second"},
};

static const unit_t *unit_lookup(const char *w)
{
    for (size_t i = 0; i < sizeof UNITS / sizeof UNITS[0]; i++) {
        if (!strcmp(w, UNITS[i].w)) return &UNITS[i];
    }
    return NULL;
}

static bool conversion_case(token_t *t, int n, char *out, size_t on)
{
    int ni = -1;
    for (int i = 0; i < n; i++) if (t[i].t == TK_NUM) { ni = i; break; }
    if (ni < 0) return false;
    int si = -1;
    for (int i = ni + 1; i < n; i++) if (t[i].t == TK_WORD && unit_lookup(t[i].w)) { si = i; break; }
    if (si < 0) return false;
    int sep = -1;
    for (int i = si + 1; i < n; i++) {
        if (t[i].t == TK_WORD && (!strcmp(t[i].w, "in") || !strcmp(t[i].w, "to") ||
                                  !strcmp(t[i].w, "into") || !strcmp(t[i].w, "as"))) { sep = i; break; }
        if (t[i].t == TK_WORD && unit_lookup(t[i].w)) { sep = i - 1; break; }
    }
    if (sep < 0) return false;
    int ti = -1;
    for (int i = sep + 1; i < n; i++) if (t[i].t == TK_WORD && unit_lookup(t[i].w)) { ti = i; break; }
    if (ti < 0) return false;

    const unit_t *a = unit_lookup(t[si].w), *b = unit_lookup(t[ti].w);
    if (!a || !b || a->cat != b->cat) return false;
    double x = t[ni].v, r;
    if (a->cat == U_TEMP) {
        bool from_c = a->name[0] == 'd' && strstr(a->name, "Celsius");
        bool to_c = b->name[0] == 'd' && strstr(b->name, "Celsius");
        if (from_c && !to_c) r = x * 9.0 / 5.0 + 32.0;
        else if (!from_c && to_c) r = (x - 32.0) * 5.0 / 9.0;
        else r = x;
    } else {
        r = x * a->f / b->f;
    }
    char num[64];
    math_format(r, num, sizeof num);
    snprintf(out, on, "That is %s %s.", num, b->name);
    return true;
}

static bool quadratic_case(token_t *t, int n, char *out, size_t on)
{
    bool named = has_word(t, n, "quadratic") ||
                 (has_word(t, n, "roots") && has_word(t, n, "solve"));
    if (!named) return false;
    double v[4];
    int m = collect_signed(t, n, v, 4);
    if (m < 3) return false;
    double a = v[0], b = v[1], c = v[2];
    if (a == 0) { snprintf(out, on, "That is not a quadratic."); return true; }
    double disc = b * b - 4 * a * c;
    if (disc < 0) { snprintf(out, on, "There are no real roots."); return true; }
    if (disc == 0) {
        char x[32];
        math_format(-b / (2 * a), x, sizeof x);
        snprintf(out, on, "There is one repeated root, x equals %s.", x);
        return true;
    }
    char x1[32], x2[32];
    math_format((-b + sqrt(disc)) / (2 * a), x1, sizeof x1);
    math_format((-b - sqrt(disc)) / (2 * a), x2, sizeof x2);
    snprintf(out, on, "x equals %s or x equals %s.", x1, x2);
    return true;
}

static bool simultaneous_case(token_t *t, int n, char *out, size_t on)
{
    if (!has_word(t, n, "simultaneous")) return false;
    double v[8];
    int m = collect_signed(t, n, v, 8);
    if (m < 6) return false;
    double a1 = v[0], b1 = v[1], c1 = v[2];
    double a2 = v[3], b2 = v[4], c2 = v[5];
    double det = a1 * b2 - a2 * b1;
    if (det == 0) { snprintf(out, on, "There is no unique solution."); return true; }
    char x[32], y[32];
    math_format((c1 * b2 - c2 * b1) / det, x, sizeof x);
    math_format((a1 * c2 - a2 * c1) / det, y, sizeof y);
    snprintf(out, on, "x equals %s and y equals %s.", x, y);
    return true;
}

static bool sequence_case(token_t *t, int n, char *out, size_t on)
{
    bool nth = has_word(t, n, "nth") && has_word(t, n, "term");
    bool next = has_word(t, n, "next");
    bool seq = has_word(t, n, "sequence");
    if (!nth && !next && !seq) return false;
    double v[16];
    int m = collect_signed(t, n, v, 16);
    if (m < 2) return false;
    if (nth && m < 3) return false;

    bool arith = true, geo = true;
    double d = v[1] - v[0];
    double r = v[0] != 0 ? v[1] / v[0] : 0;
    for (int i = 2; i < m; i++) {
        if (fabs((v[i] - v[i - 1]) - d) > 1e-9) arith = false;
        if (v[i - 1] == 0 || fabs(v[i] / v[i - 1] - r) > 1e-9) geo = false;
    }

    if (!arith && !geo) { snprintf(out, on, "That is not a simple sequence."); return true; }
    if (next) {
        char x[32];
        math_format(arith ? v[m - 1] + d : v[m - 1] * r, x, sizeof x);
        snprintf(out, on, "The next term is %s.", x);
        return true;
    }
    if (arith) {
        char expr[128] = {0};
        char dstr[32];
        math_format(d, dstr, sizeof dstr);
        if (d == 1) {
            strncat(expr, "n", sizeof expr - 1);
        } else if (d == -1) {
            strncat(expr, "minus n", sizeof expr - 1);
        } else {
            strncat(expr, dstr, sizeof expr - strlen(expr) - 1);
            strncat(expr, " n", sizeof expr - strlen(expr) - 1);
        }
        double c = v[0] - d;
        if (c > 0) { char b[32]; math_format(c, b, sizeof b); strncat(expr, " plus ", sizeof expr - strlen(expr) - 1); strncat(expr, b, sizeof expr - strlen(expr) - 1); }
        else if (c < 0) { char b[32]; math_format(-c, b, sizeof b); strncat(expr, " minus ", sizeof expr - strlen(expr) - 1); strncat(expr, b, sizeof expr - strlen(expr) - 1); }
        snprintf(out, on, "The nth term is %s.", expr);
        return true;
    }
    char a[32], rr[32];
    math_format(v[0], a, sizeof a);
    math_format(r, rr, sizeof rr);
    snprintf(out, on, "The nth term is %s times %s to the power of n minus 1.", a, rr);
    return true;
}

static bool shape_case(token_t *t, int n, char *out, size_t on)
{
    bool area = has_word(t, n, "area");
    bool circ = has_word(t, n, "circumference");
    bool peri = has_word(t, n, "perimeter");
    bool vol = has_word(t, n, "volume");
    if (!area && !circ && !peri && !vol) return false;
    double v[4];
    int m = collect_numbers(t, n, v, 4);
    if (m < 1) return false;

    double r = 0;
    if (has_word(t, n, "circle")) {
        r = v[0];
        if (has_word(t, n, "diameter")) r /= 2;
        if (area) { char x[32]; math_format(M_PI * r * r, x, sizeof x); snprintf(out, on, "The area is %s.", x); return true; }
        if (circ) { char x[32]; math_format(2 * M_PI * r, x, sizeof x); snprintf(out, on, "The circumference is %s.", x); return true; }
    }
    if (has_word(t, n, "triangle") && area && m >= 2) {
        char x[32]; math_format(0.5 * v[0] * v[1], x, sizeof x);
        snprintf(out, on, "The area is %s.", x); return true;
    }
    if ((has_word(t, n, "rectangle") || has_word(t, n, "square")) && m >= 2) {
        char x[32];
        if (area) math_format(v[0] * v[1], x, sizeof x);
        else if (peri) math_format(2 * (v[0] + v[1]), x, sizeof x);
        else return false;
        snprintf(out, on, "The %s is %s.", area ? "area" : "perimeter", x);
        return true;
    }
    if (has_word(t, n, "cylinder") && vol && m >= 2) {
        char x[32]; math_format(M_PI * v[0] * v[0] * v[1], x, sizeof x);
        snprintf(out, on, "The volume is %s.", x); return true;
    }
    if (has_word(t, n, "sphere") && vol) {
        char x[32]; math_format(4.0 / 3.0 * M_PI * v[0] * v[0] * v[0], x, sizeof x);
        snprintf(out, on, "The volume is %s.", x); return true;
    }
    if (has_word(t, n, "cone") && vol && m >= 2) {
        char x[32]; math_format(M_PI * v[0] * v[0] * v[1] / 3.0, x, sizeof x);
        snprintf(out, on, "The volume is %s.", x); return true;
    }
    return false;
}

static bool rule_case(token_t *t, int n, char *out, size_t on)
{
    bool sine = has_word(t, n, "sine") && has_word(t, n, "rule");
    bool cosine = has_word(t, n, "cosine") && has_word(t, n, "rule");
    if (!sine && !cosine) return false;
    double v[4];
    int m = collect_numbers(t, n, v, 4);
    if (m < 3) return false;
    double r;
    if (sine) {
        double a = v[0], A = v[1] * M_PI / 180.0, B = v[2] * M_PI / 180.0;
        if (sin(A) == 0) return false;
        r = a * sin(B) / sin(A);
    } else {
        double a = v[0], b = v[1], C = v[2] * M_PI / 180.0;
        r = sqrt(a * a + b * b - 2 * a * b * cos(C));
    }
    say_plain(r, out, on);
    return true;
}

static bool surd_case(token_t *t, int n, char *out, size_t on)
{
    bool surd = has_word(t, n, "surd") ||
                (has_word(t, n, "simplify") && has_word(t, n, "root"));
    if (!surd) return false;
    double v[4];
    int m = collect_numbers(t, n, v, 4);
    if (m < 1) return false;
    long x = (long)v[0];
    if (x < 2) return false;
    long k = 1;
    for (long i = (long)sqrt((double)x); i >= 2; i--) {
        if (x % (i * i) == 0) { k = i; break; }
    }
    if (k == 1) { snprintf(out, on, "That does not simplify."); return true; }
    if (x / (k * k) == 1) snprintf(out, on, "That is %ld.", k);
    else snprintf(out, on, "That is %ld root %ld.", k, x / (k * k));
    return true;
}

static bool standard_form_case(token_t *t, int n, char *out, size_t on)
{
    if (!has_word(t, n, "standard") || !has_word(t, n, "form")) return false;
    char expr[768];
    build_expr(t, n, expr, sizeof expr);
    parser_t p = {.s = expr, .i = 0, .radians = false};
    double v = parse_expr(&p);
    if (p.err || !isfinite(v) || v == 0) return false;
    int exp = (int)floor(log10(fabs(v)));
    double mant = v / pow(10.0, exp);
    char m[32];
    math_format(mant, m, sizeof m);
    if (exp == 0) snprintf(out, on, "That is %s.", m);
    else if (exp > 0) snprintf(out, on, "That is %s times ten to the power of %d.", m, exp);
    else snprintf(out, on, "That is %s times ten to the power of minus %d.", m, -exp);
    return true;
}

static bool eval_tokens(token_t *t, int n, double *out, const char **err)
{
    char expr[768];
    build_expr(t, n, expr, sizeof expr);
    parser_t p = {.s = expr, .i = 0, .radians = false};
    for (int i = 0; i < n; i++) {
        if (t[i].t == TK_WORD &&
            (!strcmp(t[i].w, "radian") || !strcmp(t[i].w, "radians"))) p.radians = true;
    }
    double v = parse_expr(&p);
    skip_ws(&p);
    if (!p.err && p.s[p.i] != 0) p.err = "I got lost in the numbers.";
    if (p.err) { *err = p.err; return false; }
    if (!isfinite(v)) { *err = "That number is too large for me."; return false; }
    *out = v;
    return true;
}

/* ------------------------------------------------------------------ API */

void math_format(double v, char *out, size_t outn)
{
    if (!isfinite(v)) {
        snprintf(out, outn, "a number too large for me");
        return;
    }
    if (fabs(v) < 1e-12) v = 0;

    if (v == floor(v) && fabs(v) < 1e10) {
        snprintf(out, outn, "%lld", (long long)v);
        return;
    }

    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.10g", v);
    char *e = strchr(tmp, 'e');
    if (e) {
        *e = 0;
        int exp = atoi(e + 1);
        if (exp == 0) {
            snprintf(out, outn, "%s", tmp);
        } else if (exp > 0) {
            snprintf(out, outn, "%s times ten to the power of %d", tmp, exp);
        } else {
            snprintf(out, outn, "%s times ten to the power of minus %d", tmp, -exp);
        }
        return;
    }
    snprintf(out, outn, "%s", tmp);
}

bool math_looks_like(const char *text)
{
    token_t raw[MAX_TOK], tok[MAX_TOK];
    int n = tokenize(text, raw, MAX_TOK);
    n = fold_numbers(raw, n, tok);
    n = merge_decimals(tok, n);

    bool has_num = false, has_op = false;
    for (int i = 0; i < n; i++) {
        if (tok[i].t == TK_NUM) has_num = true;
        if (tok[i].t == TK_SYM &&
            strchr("+-*/^!%", tok[i].sym)) has_op = true;
        if (tok[i].t == TK_WORD && (!strcmp(tok[i].w, "pi") || !strcmp(tok[i].w, "e"))) {
            has_num = true;
        }
        if (tok[i].t == TK_WORD) {
            static const char *ops[] = {"plus", "minus", "times", "add", "subtract",
                                        "multiply", "divided", "multiplied", "over",
                                        "square", "cube", "root", "power", "squared",
                                        "cubed", "sine", "cosine", "tangent", "sin",
                                        "cos", "tan", "log", "ln", "factorial",
                                        "percent", "mean", "median", "mode", "range",
                                        "sum", "increase", "decrease", "hypotenuse",
                                        "pythagoras", "pi", "round", "rounded", "ratio",
                                        "share", "hcf", "gcd", "lcm", "highest", "lowest",
                                        "prime", "convert", "quadratic", "simultaneous",
                                        "nth", "sequence", "next", "area", "circumference",
                                        "perimeter", "volume", "surd", "standard",
                                        "probability", "interquartile", "iqr", "quartile",
                                        "bound", "bounds", "interval", "upper", "lower",
                                        "sine", "cosine", "rule", NULL};
            for (int k = 0; ops[k]; k++) {
                if (!strcmp(tok[i].w, ops[k])) { has_op = true; break; }
            }
        }
    }
    if (!has_op) {
        int units = 0;
        for (int i = 0; i < n; i++) {
            if (tok[i].t == TK_WORD && unit_lookup(tok[i].w)) units++;
        }
        if (units >= 2) has_op = true;
    }
    return has_op && has_num;
}

bool math_eval(const char *text, double *out, char *msg, size_t msg_len)
{
    token_t raw[MAX_TOK], tok[MAX_TOK];
    int n = tokenize(text, raw, MAX_TOK);
    n = fold_numbers(raw, n, tok);
    n = merge_decimals(tok, n);

    if (n == 0) {
        snprintf(msg, msg_len, "I didn't catch a sum there.");
        return false;
    }

    if (stats_case(tok, n, out) || percent_case(tok, n, out) || pythag_case(tok, n, out)) {
        return true;
    }

    char expr[768];
    build_expr(tok, n, expr, sizeof expr);

    parser_t p = {.s = expr, .i = 0, .radians = false};
    for (int i = 0; i < n; i++) {
        if (tok[i].t == TK_WORD &&
            (!strcmp(tok[i].w, "radian") || !strcmp(tok[i].w, "radians"))) {
            p.radians = true;
        }
    }

    double v = parse_expr(&p);
    skip_ws(&p);
    if (!p.err && p.s[p.i] != 0) p.err = "I got lost in the numbers.";
    if (p.err) {
        snprintf(msg, msg_len, "%s", p.err);
        return false;
    }
    if (!isfinite(v)) {
        snprintf(msg, msg_len, "That number is too large for me.");
        return false;
    }
    *out = v;
    return true;
}


bool math_say(const char *text, char *out, size_t on)
{
    token_t raw[MAX_TOK], tok[MAX_TOK];
    int n = tokenize(text, raw, MAX_TOK);
    n = fold_numbers(raw, n, tok);
    n = merge_decimals(tok, n);
    if (n == 0) return false;

    if (iqr_case(tok, n, out, on)) return true;
    if (probability_case(tok, n, out, on)) return true;
    if (round_case(tok, n, out, on)) return true;
    if (bounds_case(tok, n, out, on)) return true;
    if (ratio_case(tok, n, out, on)) return true;
    if (number_theory_case(tok, n, out, on)) return true;
    if (conversion_case(tok, n, out, on)) return true;
    if (quadratic_case(tok, n, out, on)) return true;
    if (simultaneous_case(tok, n, out, on)) return true;
    if (sequence_case(tok, n, out, on)) return true;
    if (shape_case(tok, n, out, on)) return true;
    if (rule_case(tok, n, out, on)) return true;
    if (surd_case(tok, n, out, on)) return true;
    if (standard_form_case(tok, n, out, on)) return true;

    double v = 0;
    if (stats_case(tok, n, &v) || percent_case(tok, n, &v) || pythag_case(tok, n, &v)) {
        say_result(v, out, on);
        return true;
    }

    const char *err = NULL;
    if (!eval_tokens(tok, n, &v, &err)) {
        snprintf(out, on, "%s", err ? err : "I got lost in the numbers.");
        return true;
    }
    say_result(v, out, on);
    return true;
}
