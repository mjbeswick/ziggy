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
                                        "pythagoras", "pi", NULL};
            for (int k = 0; ops[k]; k++) {
                if (!strcmp(tok[i].w, ops[k])) { has_op = true; break; }
            }
        }
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
