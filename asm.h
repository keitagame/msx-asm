/*
 * asm.h - MSX(Z80)用アセンブラ 共通定義ヘッダ
 */
#ifndef ASM_H
#define ASM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

/* ---------- 基本定数 ---------- */
#define MAX_LINE      512
#define MAX_LABEL     64
#define MAX_LABELS    4096
#define MAX_TOKEN     128
#define MAX_OPERAND   256
#define MAX_BIN       65536   /* Z80のアドレス空間 = 64KB */
#define MAX_SOURCE_LINES 65536

/* ---------- ラベル(シンボル)テーブル ---------- */
typedef struct {
    char name[MAX_LABEL];
    int  value;
    int  defined;   /* 1=定義済み */
} Label;

/* ---------- アセンブル対象の1行 ---------- */
typedef struct {
    char raw[MAX_LINE];   /* 元の行(コメント除去後) */
    char label[MAX_LABEL];
    char mnemonic[32];
    char operand1[MAX_OPERAND];
    char operand2[MAX_OPERAND];
    int  has_label;
    int  has_op1;
    int  has_op2;
    int  address;         /* この行のプログラムカウンタ */
    int  line_no;         /* 元のソース行番号 */
} SourceLine;

/* ---------- グローバル状態 ---------- */
extern Label labels[MAX_LABELS];
extern int   label_count;

extern SourceLine lines[MAX_SOURCE_LINES];
extern int   line_count;

extern uint8_t binimg[MAX_BIN];
extern int     binimg_used[MAX_BIN]; /* 出力有無フラグ(重複書き込み検出用) */

extern int  pc;             /* 現在のプログラムカウンタ */
extern int  org_base;       /* .org で設定された開始アドレス */
extern int  pass;           /* 現在のパス(1 or 2) */
extern int  error_count;
extern int  current_line_no;

/* ---------- ラベル管理 ---------- */
int  find_label(const char *name);
int  add_or_update_label(const char *name, int value, int allow_redefine);
int  get_label_value(const char *name, int *out_value);

/* ---------- エラー処理 ---------- */
void asm_error(const char *fmt, ...);
void asm_warning(const char *fmt, ...);

/* ---------- 式評価 ---------- */
int  eval_expr(const char *expr, int *out_value, int *ok);

/* ---------- 行パース ---------- */
void parse_line(const char *src, SourceLine *out);

/* ---------- コード生成(命令ごとのバイト数計算 & バイナリ生成) ---------- */
int  encode_instruction(SourceLine *sl, uint8_t *out, int *out_len);

/* ---------- ユーティリティ ---------- */
void trim(char *s);
void to_upper(char *s);
int  is_number_token(const char *s);
int  parse_number(const char *s);
char *skip_spaces(char *s);

#endif /* ASM_H */