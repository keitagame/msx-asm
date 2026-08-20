/*
 * expr.c - 簡易式評価器
 *
 * サポートする演算子: + - * / % & | ^ ~ << >> 単項マイナス 括弧()
 * オペランド: 数値リテラル ($FF, 0xFF, 12, 101B, 'A' など), ラベル名, 現在アドレス($ または *)
 *
 * 再帰下降パーサで実装。
 */
#include "asm.h"

typedef struct {
    const char *p;   /* 現在の読み取り位置 */
    int ok;           /* すべてのシンボルが解決できたか */
    int used_undefined; /* 未定義ラベルを参照したか(パス1で許容するため) */
} ExprState;

static void es_skip_ws(ExprState *es) {
    while (*es->p && isspace((unsigned char)*es->p)) es->p++;
}

static int parse_expr_or(ExprState *es);

/* トークン: 識別子(ラベル名) or 数値 or 括弧 or 現在アドレス */
static int parse_primary(ExprState *es) {
    es_skip_ws(es);
    if (*es->p == '(') {
        es->p++;
        int v = parse_expr_or(es);
        es_skip_ws(es);
        if (*es->p == ')') es->p++;
        else es->ok = 0;
        return v;
    }
    if (*es->p == '-') {
        es->p++;
        return -parse_primary(es);
    }
    if (*es->p == '+') {
        es->p++;
        return parse_primary(es);
    }
    if (*es->p == '~') {
        es->p++;
        return ~parse_primary(es);
    }
    /* 現在アドレス */
    if (*es->p == '$' && !(isxdigit((unsigned char)es->p[1]))) {
        es->p++;
        return pc;
    }
    if (*es->p == '*') {
        /* 単独の * は現在アドレスとして扱う(掛け算と紛らわしいため呼び出し元で判定) */
        es->p++;
        return pc;
    }

    /* 数値: $hex, 0x, 進数サフィックス, 通常10進, 文字リテラル */
    if (*es->p == '$' || isdigit((unsigned char)*es->p) || *es->p == '\'') {
        char buf[MAX_TOKEN];
        int i = 0;
        if (*es->p == '\'') {
            buf[i++] = *es->p++;
            if (*es->p) buf[i++] = *es->p++;
            if (*es->p == '\'') buf[i++] = *es->p++;
            buf[i] = '\0';
            return parse_number(buf);
        }
        if (*es->p == '$') buf[i++] = *es->p++;
        while (*es->p && (isalnum((unsigned char)*es->p))) {
            buf[i++] = *es->p++;
            if (i >= MAX_TOKEN - 1) break;
        }
        buf[i] = '\0';
        return parse_number(buf);
    }

    /* 識別子(ラベル) */
    if (isalpha((unsigned char)*es->p) || *es->p == '_' || *es->p == '.') {
        char buf[MAX_LABEL];
        int i = 0;
        while (*es->p && (isalnum((unsigned char)*es->p) || *es->p == '_' || *es->p == '.')) {
            if (i < MAX_LABEL - 1) buf[i++] = *es->p++;
            else es->p++;
        }
        buf[i] = '\0';
        int val = 0;
        if (get_label_value(buf, &val)) {
            return val;
        } else {
            es->used_undefined = 1;
            if (pass == 2) {
                es->ok = 0; /* パス2で未定義なら本当のエラー */
            }
            return 0;
        }
    }

    /* 解釈不能 */
    es->ok = 0;
    return 0;
}

static int parse_mul(ExprState *es) {
    int v = parse_primary(es);
    for (;;) {
        es_skip_ws(es);
        if (*es->p == '*' ) {
            /* 直後が識別子等の演算対象になりそうな場合のみ乗算とみなす。
               単独$/*処理は parse_primary 内で対応済みなのでここでは常に乗算扱い */
            es->p++;
            int r = parse_primary(es);
            v = v * r;
        } else if (*es->p == '/') {
            es->p++;
            int r = parse_primary(es);
            if (r == 0) { v = 0; }
            else v = v / r;
        } else if (*es->p == '%') {
            es->p++;
            int r = parse_primary(es);
            if (r == 0) { v = 0; }
            else v = v % r;
        } else {
            break;
        }
    }
    return v;
}

static int parse_shift(ExprState *es) {
    int v = parse_mul(es);
    for (;;) {
        es_skip_ws(es);
        if (es->p[0] == '<' && es->p[1] == '<') {
            es->p += 2;
            int r = parse_mul(es);
            v = v << r;
        } else if (es->p[0] == '>' && es->p[1] == '>') {
            es->p += 2;
            int r = parse_mul(es);
            v = v >> r;
        } else {
            break;
        }
    }
    return v;
}

static int parse_add(ExprState *es) {
    int v = parse_shift(es);
    for (;;) {
        es_skip_ws(es);
        if (*es->p == '+') {
            es->p++;
            int r = parse_shift(es);
            v = v + r;
        } else if (*es->p == '-') {
            es->p++;
            int r = parse_shift(es);
            v = v - r;
        } else {
            break;
        }
    }
    return v;
}

static int parse_and(ExprState *es) {
    int v = parse_add(es);
    for (;;) {
        es_skip_ws(es);
        if (*es->p == '&') {
            es->p++;
            int r = parse_add(es);
            v = v & r;
        } else break;
    }
    return v;
}

static int parse_xor(ExprState *es) {
    int v = parse_and(es);
    for (;;) {
        es_skip_ws(es);
        if (*es->p == '^') {
            es->p++;
            int r = parse_and(es);
            v = v ^ r;
        } else break;
    }
    return v;
}

static int parse_or(ExprState *es) {
    int v = parse_xor(es);
    for (;;) {
        es_skip_ws(es);
        if (*es->p == '|') {
            es->p++;
            int r = parse_xor(es);
            v = v | r;
        } else break;
    }
    return v;
}

static int parse_expr_or(ExprState *es) {
    return parse_or(es);
}

/*
 * eval_expr: 式文字列を評価する
 * 戻り値: 1=成功(パース可能かつパス2ならラベルも全解決), 0=失敗
 * out_value に結果を格納
 */
int eval_expr(const char *expr, int *out_value, int *ok) {
    ExprState es;
    es.p = expr;
    es.ok = 1;
    es.used_undefined = 0;

    int v = parse_expr_or(&es);
    es_skip_ws(&es);
    if (*es.p != '\0') {
        es.ok = 0; /* 余分な文字が残っている */
    }

    *out_value = v;
    *ok = es.ok;
    return es.ok;
}