/*
 * parser.c - 1行のソースをラベル/ニーモニック/オペランドに分解する
 *
 * 対応フォーマット例:
 *   LABEL:  LD A,10        ; コメント
 *   LABEL   LD A,10
 *           LD A,(HL)
 *           .ORG $8000
 *           .DB 1,2,3
 *   ; コメント行
 */
#include "asm.h"

/* 文字列リテラル("..."または'...')の中でなければコメント開始とみなす位置を探す */
static void strip_comment(char *s) {
    int in_squote = 0, in_dquote = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        if (c == '\'' && !in_dquote) in_squote = !in_squote;
        else if (c == '"' && !in_squote) in_dquote = !in_dquote;
        else if (c == ';' && !in_squote && !in_dquote) {
            s[i] = '\0';
            return;
        }
    }
}

/* ラベルの終端記号 ':' を除去しつつ判定 */
void parse_line(const char *src, SourceLine *out) {
    memset(out, 0, sizeof(SourceLine));

    char buf[MAX_LINE];
    strncpy(buf, src, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* 改行削除 */
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) { buf[--len] = '\0'; }

    strip_comment(buf);

    /* 元の行を保持(トリム後) */
    char kept[MAX_LINE];
    strncpy(kept, buf, sizeof(kept)-1);
    kept[sizeof(kept)-1] = '\0';
    trim(kept);
    strncpy(out->raw, kept, MAX_LINE - 1);

    if (kept[0] == '\0') {
        return; /* 空行 */
    }

    char *p = kept;

    /* ラベル判定: 行頭が空白でない識別子で始まり ':' があるか、
       あるいは行頭が空白なしで始まる場合(コロン無しラベル記法)を許容する。
       ここでは「行頭が空白文字でなければラベル領域」というシンプルなルールにする。
       ただし '.' で始まるディレクティブは通常インデントされる想定なので、
       行頭が非空白 = ラベルありと判定する。 */

    int starts_with_space = isspace((unsigned char)src[0]) || src[0] == '\t';

    if (!starts_with_space) {
        /* ラベルを読み取る */
        char label[MAX_LABEL];
        int i = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ':') {
            if (i < MAX_LABEL - 1) label[i++] = *p;
            p++;
        }
        label[i] = '\0';
        if (*p == ':') p++; /* コロンをスキップ */
        if (label[0] != '\0') {
            strncpy(out->label, label, MAX_LABEL - 1);
            out->has_label = 1;
        }
    }

    p = skip_spaces(p);
    if (*p == '\0') return; /* ラベルのみの行 */

    /* ニーモニック(またはディレクティブ)を読む */
    {
        char mnem[32];
        int i = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',') {
            if (i < 31) mnem[i++] = *p;
            p++;
        }
        mnem[i] = '\0';
        to_upper(mnem);
        strncpy(out->mnemonic, mnem, sizeof(out->mnemonic) - 1);
    }

    p = skip_spaces(p);
    if (*p == '\0') return; /* オペランド無し */

    /* オペランドをカンマで分割(括弧・クォート内のカンマは無視) */
    {
        char op1[MAX_OPERAND] = {0};
        char op2[MAX_OPERAND] = {0};
        int depth = 0;
        int in_squote = 0, in_dquote = 0;
        int idx = 0;
        char *dst = op1;
        int max = MAX_OPERAND - 1;
        int comma_found = 0;

        while (*p) {
            char c = *p;
            if (c == '\'' && !in_dquote) in_squote = !in_squote;
            else if (c == '"' && !in_squote) in_dquote = !in_dquote;
            else if (!in_squote && !in_dquote) {
                if (c == '(') depth++;
                else if (c == ')') depth--;
                else if (c == ',' && depth == 0 && !comma_found) {
                    comma_found = 1;
                    dst = op2;
                    idx = 0;
                    p++;
                    continue;
                }
            }
            if (idx < max) dst[idx++] = c;
            p++;
        }
        /* dstが指す配列を確実に終端する(op1のみの場合はop2は空のまま) */
        if (dst == op1) {
            op1[idx < max ? idx : max] = '\0';
        } else {
            op2[idx < max ? idx : max] = '\0';
        }

        trim(op1);
        trim(op2);

        if (op1[0]) { strncpy(out->operand1, op1, MAX_OPERAND - 1); out->has_op1 = 1; }
        if (op2[0]) { strncpy(out->operand2, op2, MAX_OPERAND - 1); out->has_op2 = 1; }
    }
}