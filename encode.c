/*
 * encode.c - Z80命令のエンコード(バイナリ生成)
 *
 * 各ニーモニックについて、オペランドの形からオペコードバイト列を決定する。
 * 数値・アドレスが必要な箇所は式評価器(eval_expr)を用いる。
 * パス1では未確定ラベルがあってもエラーにせず、命令長だけ確定できればよい。
 * パス2では全ラベルが確定しているのでエラーチェックを厳格に行う。
 */
#include "asm.h"

/* ============ レジスタ判定 ============ */

typedef enum {
    R_NONE = -1,
    R_B = 0, R_C, R_D, R_E, R_H, R_L, R_HLIND, R_A, /* r表(0-7) の並び順(HL)=6 */
} Reg8;

static int reg8_code(const char *s) {
    char t[16];
    strncpy(t, s, 15); t[15] = 0;
    trim(t);
    to_upper(t);
    if (strcmp(t, "B") == 0) return 0;
    if (strcmp(t, "C") == 0) return 1;
    if (strcmp(t, "D") == 0) return 2;
    if (strcmp(t, "E") == 0) return 3;
    if (strcmp(t, "H") == 0) return 4;
    if (strcmp(t, "L") == 0) return 5;
    if (strcmp(t, "(HL)") == 0) return 6;
    if (strcmp(t, "A") == 0) return 7;
    return -1;
}

/* IX/IY相対 8bitレジスタ的代替は別途処理(IXH等はZ80非公式命令のため簡易対応) */
static int reg8_code_undoc(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    if (strcmp(t,"IXH")==0) return 4;
    if (strcmp(t,"IXL")==0) return 5;
    if (strcmp(t,"IYH")==0) return 4;
    if (strcmp(t,"IYL")==0) return 5;
    return -1;
}

static int reg16_code_sp(const char *s) { /* BC,DE,HL,SP */
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    if (strcmp(t, "BC") == 0) return 0;
    if (strcmp(t, "DE") == 0) return 1;
    if (strcmp(t, "HL") == 0) return 2;
    if (strcmp(t, "SP") == 0) return 3;
    return -1;
}

static int reg16_code_af(const char *s) { /* BC,DE,HL,AF (push/pop用) */
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    if (strcmp(t, "BC") == 0) return 0;
    if (strcmp(t, "DE") == 0) return 1;
    if (strcmp(t, "HL") == 0) return 2;
    if (strcmp(t, "AF") == 0) return 3;
    return -1;
}

static int is_reg_ix(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "IX") == 0;
}
static int is_reg_iy(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "IY") == 0;
}
static int is_reg_a(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "A") == 0;
}
static int is_reg_hl(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "HL") == 0;
}
static int is_reg_sp(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "SP") == 0;
}
static int is_reg_de(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "DE") == 0;
}
static int is_reg_i(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "I") == 0;
}
static int is_reg_r(const char *s) {
    char t[16]; strncpy(t, s, 15); t[15]=0; trim(t); to_upper(t);
    return strcmp(t, "R") == 0;
}

/* "(HL)" のように括弧で囲まれているか */
static int is_paren(const char *s, char *inner, int innersize) {
    char t[MAX_OPERAND];
    strncpy(t, s, sizeof(t)-1); t[sizeof(t)-1]=0;
    trim(t);
    int len = (int)strlen(t);
    if (len >= 2 && t[0] == '(' && t[len-1] == ')') {
        int inlen = len - 2;
        if (inlen >= innersize) inlen = innersize - 1;
        strncpy(inner, t + 1, inlen);
        inner[inlen] = '\0';
        trim(inner);
        return 1;
    }
    return 0;
}

/* "(IX+d)" "(IY-d)" の判定。base に "IX"/"IY"、dispに式文字列を返す */
static int is_paren_indexed(const char *s, char *base, char *disp) {
    char inner[MAX_OPERAND];
    if (!is_paren(s, inner, sizeof(inner))) return 0;
    char t[MAX_OPERAND];
    strncpy(t, inner, sizeof(t)-1); t[sizeof(t)-1]=0;
    to_upper(t);
    /* IX / IY で始まるか */
    const char *rest = NULL;
    if (strncmp(t, "IX", 2) == 0) { strcpy(base, "IX"); rest = inner + 2; }
    else if (strncmp(t, "IY", 2) == 0) { strcpy(base, "IY"); rest = inner + 2; }
    else return 0;

    while (*rest && isspace((unsigned char)*rest)) rest++;
    if (*rest == '\0') {
        strcpy(disp, "0");
        return 1;
    }
    if (*rest == '+' || *rest == '-') {
        strcpy(disp, rest);
        return 1;
    }
    return 0;
}

/* 条件コード (Z,NZ,C,NC,PO,PE,P,M) を判定し、cc値(0-7)を返す。該当なしは-1 */
static int cond_code(const char *s) {
    char t[8]; strncpy(t, s, 7); t[7]=0; trim(t); to_upper(t);
    if (strcmp(t, "NZ") == 0) return 0;
    if (strcmp(t, "Z") == 0)  return 1;
    if (strcmp(t, "NC") == 0) return 2;
    if (strcmp(t, "C") == 0)  return 3;
    if (strcmp(t, "PO") == 0) return 4;
    if (strcmp(t, "PE") == 0) return 5;
    if (strcmp(t, "P") == 0)  return 6;
    if (strcmp(t, "M") == 0)  return 7;
    return -1;
}

/* 式を評価し、パス1でも失敗しない緩いバージョン(値が確定しなければ0を仮置き) */
static int eval_soft(const char *expr, int *value) {
    int ok = 0;
    eval_expr(expr, value, &ok);
    if (pass == 2 && !ok) {
        asm_error("式を評価できません: '%s'", expr);
        return 0;
    }
    return 1;
}

#include "encode_tables.c"
#include "encode_main.c"