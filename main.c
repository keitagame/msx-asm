/*
 * main.c - MSX用 Z80アセンブラ メインドライバ
 *
 * 使い方:
 *   msxasm input.asm -o output.bin           バイナリ生成(.orgのアドレスから連続イメージ)
 *   msxasm input.asm -o output.bin --rom     MSX ROMヘッダ(AB 42 ...)付きで出力
 *   msxasm input.asm -o output.com --com     MSX-DOS .COM形式(オフセット0x0100基準)で出力
 *   msxasm input.asm -l listing.lst          リストファイルも出力
 *
 * アセンブル方式: 2パス
 *   パス1: ラベルのアドレスを仮確定させる(命令長を計算しながらPCを進める)
 *   パス2: 全ラベルが確定した状態で実際のバイナリを生成する
 */
#include "asm.h"

SourceLine lines[MAX_SOURCE_LINES];
int line_count = 0;

uint8_t binimg[MAX_BIN];
int     binimg_used[MAX_BIN];

static int min_addr = -1, max_addr = -1;

/* ============ ソースファイル読み込み(.INCLUDE対応) ============ */
static void load_source(const char *path, int depth) {
    if (depth > 16) {
        fprintf(stderr, "エラー: .INCLUDEのネストが深すぎます (%s)\n", path);
        error_count++;
        return;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "エラー: ファイルを開けません: %s\n", path);
        error_count++;
        return;
    }
    char buf[MAX_LINE];
    int lineno = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        lineno++;
        /* .INCLUDE "file" のチェック(簡易: 行頭空白除去後に判定) */
        char trimmed[MAX_LINE];
        strncpy(trimmed, buf, sizeof(trimmed)-1); trimmed[sizeof(trimmed)-1]=0;
        trim(trimmed);
        char upper[MAX_LINE];
        strncpy(upper, trimmed, sizeof(upper)-1); upper[sizeof(upper)-1]=0;
        to_upper(upper);
        if (strncmp(upper, ".INCLUDE", 8) == 0 || strncmp(upper, "INCLUDE", 7) == 0) {
            char *q1 = strchr(trimmed, '"');
            if (q1) {
                char *q2 = strchr(q1 + 1, '"');
                if (q2) {
                    char incpath[512];
                    int len = (int)(q2 - q1 - 1);
                    if (len >= (int)sizeof(incpath)) len = sizeof(incpath) - 1;
                    strncpy(incpath, q1 + 1, len);
                    incpath[len] = '\0';
                    load_source(incpath, depth + 1);
                    continue;
                }
            }
        }

        if (line_count >= MAX_SOURCE_LINES) {
            fprintf(stderr, "エラー: ソース行数が上限を超えました\n");
            error_count++;
            break;
        }
        parse_line(buf, &lines[line_count]);
        lines[line_count].line_no = lineno;
        line_count++;
    }
    fclose(fp);
}

/* ============ .EQU の事前登録(パス1の前に処理して前方参照可能にする) ============ */
static void register_equ_pass(void) {
    for (int i = 0; i < line_count; i++) {
        SourceLine *sl = &lines[i];
        current_line_no = sl->line_no;
        if (strcmp(sl->mnemonic, ".EQU") == 0 || strcmp(sl->mnemonic, "EQU") == 0) {
            if (!sl->has_label) {
                asm_warning(".EQUにはラベルが必要です: %s", sl->raw);
                continue;
            }
            int val, ok;
            eval_expr(sl->operand1, &val, &ok);
            add_or_update_label(sl->label, val, 1);
        }
    }
}

/* ============ パス1: アドレス確定 ============ */
static void run_pass1(void) {
    pass = 1;
    pc = 0;
    org_base = 0;
    for (int i = 0; i < line_count; i++) {
        SourceLine *sl = &lines[i];
        current_line_no = sl->line_no;

        int is_equ = (strcmp(sl->mnemonic, ".EQU") == 0 || strcmp(sl->mnemonic, "EQU") == 0);

        if (sl->has_label && !is_equ) {
            add_or_update_label(sl->label, pc, 1);
        }

        sl->address = pc;

        if (sl->mnemonic[0] == '\0') continue;
        if (is_equ) continue;

        if (strcmp(sl->mnemonic, ".ORG") == 0 || strcmp(sl->mnemonic, "ORG") == 0) {
            int val, ok;
            eval_expr(sl->operand1, &val, &ok);
            pc = val;
            org_base = val;
            sl->address = pc; /* .ORG自身は現アドレス変更のみ */
            continue;
        }

        static uint8_t tmp[MAX_BIN];
        int len = 0;
        encode_instruction(sl, tmp, &len);
        pc += len;
    }
}

/* ============ パス2: バイナリ生成 ============ */
static void run_pass2(void) {
    pass = 2;
    pc = 0;
    for (int i = 0; i < line_count; i++) {
        SourceLine *sl = &lines[i];
        current_line_no = sl->line_no;

        int is_equ = (strcmp(sl->mnemonic, ".EQU") == 0 || strcmp(sl->mnemonic, "EQU") == 0);

        if (strcmp(sl->mnemonic, ".ORG") == 0 || strcmp(sl->mnemonic, "ORG") == 0) {
            int val, ok;
            eval_expr(sl->operand1, &val, &ok);
            pc = val;
            org_base = val;
            sl->address = pc;
            continue;
        }

        pc = sl->address;
        if (is_equ || sl->mnemonic[0] == '\0') continue;

        static uint8_t tmp[MAX_BIN];
        int len = 0;
        int ok = encode_instruction(sl, tmp, &len);
        (void)ok;

        for (int j = 0; j < len; j++) {
            int addr = pc + j;
            if (addr < 0 || addr >= MAX_BIN) {
                asm_error("アドレスが範囲外です: %04X", addr);
                continue;
            }
            binimg[addr] = tmp[j];
            binimg_used[addr] = 1;
            if (min_addr < 0 || addr < min_addr) min_addr = addr;
            if (max_addr < 0 || addr > max_addr) max_addr = addr;
        }
        pc += len;
    }
}

/* ============ リスト出力 ============ */
static void write_listing(const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "警告: リストファイルを作成できません: %s\n", path);
        return;
    }
    pc = 0;
    for (int i = 0; i < line_count; i++) {
        SourceLine *sl = &lines[i];
        pc = sl->address;
        static uint8_t tmp[MAX_BIN];
        int len = 0;
        if (sl->mnemonic[0] != '\0' &&
            strcmp(sl->mnemonic,".ORG")!=0 && strcmp(sl->mnemonic,"ORG")!=0 &&
            strcmp(sl->mnemonic,".EQU")!=0 && strcmp(sl->mnemonic,"EQU")!=0) {
            encode_instruction(sl, tmp, &len);
        }
        if (len > 0) {
            fprintf(fp, "%04X  ", sl->address);
            for (int j = 0; j < 6; j++) {
                if (j < len) fprintf(fp, "%02X ", tmp[j]);
                else fprintf(fp, "   ");
            }
        } else {
            fprintf(fp, "%04X                  ", sl->address);
        }
        fprintf(fp, " %4d  %s\n", sl->line_no, sl->raw);
    }
    fclose(fp);
    printf("リストファイルを出力しました: %s\n", path);
}

/* ============ バイナリ出力 ============ */
static void write_binary(const char *path, int as_rom, int as_com) {
    if (min_addr < 0) {
        fprintf(stderr, "警告: 出力するコードがありません\n");
        min_addr = org_base;
        max_addr = org_base;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "エラー: 出力ファイルを作成できません: %s\n", path);
        error_count++;
        return;
    }

    if (as_rom) {
        /* MSX ROMカートリッジヘッダ: "AB" + エントリポイント等 */
        uint8_t header[16] = {0};
        header[0] = 'A'; header[1] = 'B';
        int entry = min_addr;
        header[2] = (uint8_t)(entry & 0xFF);
        header[3] = (uint8_t)((entry >> 8) & 0xFF);
        /* 残りのベクタは0で初期化(未使用) */
        fwrite(header, 1, 16, fp);
    }

    if (as_com) {
        /* MSX-DOS .COM: 通常0x0100開始想定。ここでは単純にmin_addrからのイメージを書く */
        if (min_addr != 0x0100) {
            fprintf(stderr, "警告: .COM形式は通常 ORG 0100h が必要です(現在の開始アドレス: %04Xh)\n", min_addr);
        }
    }

    for (int a = min_addr; a <= max_addr; a++) {
        uint8_t b = binimg_used[a] ? binimg[a] : 0x00;
        fwrite(&b, 1, 1, fp);
    }
    fclose(fp);

    printf("バイナリを出力しました: %s (アドレス %04Xh - %04Xh, %d バイト)\n",
           path, min_addr, max_addr, max_addr - min_addr + 1);
}

/* ============ シンボルテーブル出力 ============ */
static void print_symbols(void) {
    printf("\n=== シンボルテーブル ===\n");
    for (int i = 0; i < label_count; i++) {
        printf("%-32s = %04Xh (%d)\n", labels[i].name, labels[i].value & 0xFFFF, labels[i].value);
    }
}

/* ============ CLI ============ */
static void print_usage(const char *prog) {
    printf("MSX用 Z80アセンブラ (msxasm)\n");
    printf("使い方: %s <入力ファイル.asm> [オプション]\n", prog);
    printf("オプション:\n");
    printf("  -o <file>     出力バイナリファイル名 (デフォルト: out.bin)\n");
    printf("  -l <file>     リストファイルを出力\n");
    printf("  --rom         MSX ROMカートリッジヘッダを付加\n");
    printf("  --com         MSX-DOS .COM形式として出力(ORG 0100h想定)\n");
    printf("  --sym         シンボルテーブルを表示\n");
    printf("  -h, --help    このヘルプを表示\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_path = NULL;
    const char *output_path = "out.bin";
    const char *listing_path = NULL;
    int as_rom = 0, as_com = 0, show_sym = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            listing_path = argv[++i];
        } else if (strcmp(argv[i], "--rom") == 0) {
            as_rom = 1;
        } else if (strcmp(argv[i], "--com") == 0) {
            as_com = 1;
        } else if (strcmp(argv[i], "--sym") == 0) {
            show_sym = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        fprintf(stderr, "エラー: 入力ファイルが指定されていません\n");
        print_usage(argv[0]);
        return 1;
    }

    memset(binimg, 0, sizeof(binimg));
    memset(binimg_used, 0, sizeof(binimg_used));

    load_source(input_path, 0);
    if (error_count > 0) {
        fprintf(stderr, "読み込み中にエラーが発生したため中断します。\n");
        return 1;
    }

    register_equ_pass();
    run_pass1();
    if (error_count > 0) {
        fprintf(stderr, "パス1でエラーが %d 件発生しました。\n", error_count);
        return 1;
    }

    error_count = 0;
    run_pass2();
    if (error_count > 0) {
        fprintf(stderr, "パス2でエラーが %d 件発生しました。アセンブルを中止します。\n", error_count);
        return 1;
    }

    write_binary(output_path, as_rom, as_com);
    if (listing_path) write_listing(listing_path);
    if (show_sym) print_symbols();

    printf("アセンブル完了。ラベル数: %d, 出力行数: %d\n", label_count, line_count);
    return 0;
}