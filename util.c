/*
 * util.c - ユーティリティ関数、ラベル管理、エラー処理
 */
#include "asm.h"
#include <stdarg.h>

Label labels[MAX_LABELS];
int   label_count = 0;

int pc = 0;
int org_base = 0;
int pass = 1;
int error_count = 0;
int current_line_no = 0;

/* ============ 文字列ユーティリティ ============ */

void trim(char *s) {
    /* 先頭の空白を削る */
    int start = 0;
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start > 0) memmove(s, s + start, strlen(s + start) + 1);

    /* 末尾の空白・改行を削る */
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

void to_upper(char *s) {
    for (int i = 0; s[i]; i++) s[i] = (char)toupper((unsigned char)s[i]);
}

char *skip_spaces(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* 数値トークンかどうか判定 ($hex, 0xhex, %bin, 数字...のいずれか) */
int is_number_token(const char *s) {
    if (!s || !*s) return 0;
    if (s[0] == '$' && s[1]) return 1;
    if (s[0] == '%' && s[1]) return 1;
    if (isdigit((unsigned char)s[0])) return 1;
    if (s[0] == '\'' ) return 1; /* 文字リテラル 'A' */
    return 0;
}

/* 数値パース: $FF / 0xFF / 0FFh / %101 / 123 / 'A' に対応 */
int parse_number(const char *s) {
    if (!s || !*s) return 0;
    char buf[MAX_TOKEN];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    trim(buf);

    int len = (int)strlen(buf);
    if (len == 0) return 0;

    /* 文字リテラル 'A' */
    if (buf[0] == '\'' && len >= 3 && buf[len - 1] == '\'') {
        return (int)(unsigned char)buf[1];
    }

    /* $HEX */
    if (buf[0] == '$') {
        return (int)strtol(buf + 1, NULL, 16);
    }
    /* %BIN */
    if (buf[0] == '%') {
        return (int)strtol(buf + 1, NULL, 2);
    }
    /* 0x HEX */
    if (len > 1 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
        return (int)strtol(buf + 2, NULL, 16);
    }
    /* 末尾 H -> 16進 (例: 0FFH, 1AH) */
    if (len > 1 && (buf[len - 1] == 'h' || buf[len - 1] == 'H')) {
        char tmp[MAX_TOKEN];
        strncpy(tmp, buf, len - 1);
        tmp[len - 1] = '\0';
        return (int)strtol(tmp, NULL, 16);
    }
    /* 末尾 B -> 2進 */
    if (len > 1 && (buf[len - 1] == 'b' || buf[len - 1] == 'B')) {
        int is_bin = 1;
        for (int i = 0; i < len - 1; i++) {
            if (buf[i] != '0' && buf[i] != '1') { is_bin = 0; break; }
        }
        if (is_bin) {
            char tmp[MAX_TOKEN];
            strncpy(tmp, buf, len - 1);
            tmp[len - 1] = '\0';
            return (int)strtol(tmp, NULL, 2);
        }
    }
    /* 末尾 O/Q -> 8進 */
    if (len > 1 && (buf[len - 1] == 'o' || buf[len - 1] == 'O' ||
                     buf[len - 1] == 'q' || buf[len - 1] == 'Q')) {
        char tmp[MAX_TOKEN];
        strncpy(tmp, buf, len - 1);
        tmp[len - 1] = '\0';
        return (int)strtol(tmp, NULL, 8);
    }

    /* 通常の10進数 */
    return (int)strtol(buf, NULL, 10);
}

/* ============ ラベル管理 ============ */

int find_label(const char *name) {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0) return i;
    }
    return -1;
}

int add_or_update_label(const char *name, int value, int allow_redefine) {
    int idx = find_label(name);
    if (idx >= 0) {
        if (!allow_redefine && labels[idx].defined && labels[idx].value != value) {
            /* パス1では未確定な前方参照もあるため、パス2で確定値が変わるのは異常 */
            if (pass == 2) {
                asm_error("ラベル '%s' が別の値で再定義されています (旧:%d 新:%d)",
                           name, labels[idx].value, value);
            }
        }
        labels[idx].value = value;
        labels[idx].defined = 1;
        return idx;
    }
    if (label_count >= MAX_LABELS) {
        asm_error("ラベルテーブルが満杯です (上限 %d)", MAX_LABELS);
        return -1;
    }
    strncpy(labels[label_count].name, name, MAX_LABEL - 1);
    labels[label_count].name[MAX_LABEL - 1] = '\0';
    labels[label_count].value = value;
    labels[label_count].defined = 1;
    label_count++;
    return label_count - 1;
}

int get_label_value(const char *name, int *out_value) {
    int idx = find_label(name);
    if (idx < 0) return 0;
    if (!labels[idx].defined) return 0;
    *out_value = labels[idx].value;
    return 1;
}

/* ============ エラー / 警告 ============ */

void asm_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "エラー(行 %d): ", current_line_no);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    error_count++;
}

void asm_warning(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "警告(行 %d): ", current_line_no);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}