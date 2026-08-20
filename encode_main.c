/*
 * encode_main.c - メインのエンコードロジック
 * encode.c から #include される
 */

/* 出力バッファへバイトを追加するヘルパー */
typedef struct {
    uint8_t *buf;
    int len;
} OutBuf;

static void ob_add(OutBuf *ob, uint8_t b) {
    if (ob->len < MAX_BIN) {
        ob->buf[ob->len++] = b;
    } else {
        asm_error("1命令の出力バイト数が上限を超えました(.DSのサイズ指定を確認してください)");
    }
}

/* ============ LD命令 ============ */
static int encode_LD(SourceLine *sl, OutBuf *ob) {
    const char *d = sl->operand1;
    const char *s = sl->operand2;
    if (!sl->has_op1 || !sl->has_op2) { asm_error("LD命令にはオペランドが2つ必要です"); return 0; }

    char inner[MAX_OPERAND], base[8], disp[MAX_OPERAND];

    int rd = reg8_code(d);
    int rs = reg8_code(s);

    /* LD r, r' (r,r'共に基本8bitレジスタ、(HL)含む。ただし(HL),(HL)は不可) */
    if (rd >= 0 && rs >= 0) {
        if (rd == 6 && rs == 6) { asm_error("LD (HL),(HL) は不正です(HALTを使ってください)"); return 0; }
        ob_add(ob, 0x40 | (rd << 3) | rs);
        return 1;
    }

    /* LD r, n (即値) : rd有効かつ rs が数値/式 */
    if (rd >= 0 && rs < 0 && !is_paren(s, inner, sizeof(inner))) {
        int val;
        if (!eval_soft(s, &val)) val = 0;
        ob_add(ob, 0x06 | (rd << 3));
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }

    /* LD A,(BC) / LD A,(DE) */
    if (is_reg_a(d) && is_paren(s, inner, sizeof(inner))) {
        char t[16]; strncpy(t, inner, 15); t[15]=0; to_upper(t); trim(t);
        if (strcmp(t, "BC") == 0) { ob_add(ob, 0x0A); return 1; }
        if (strcmp(t, "DE") == 0) { ob_add(ob, 0x12 == 0 ? 0x1A : 0x1A); return 1; } /* 0x1A */
    }
    /* LD (BC),A / LD (DE),A */
    if (is_paren(d, inner, sizeof(inner)) && is_reg_a(s)) {
        char t[16]; strncpy(t, inner, 15); t[15]=0; to_upper(t); trim(t);
        if (strcmp(t, "BC") == 0) { ob_add(ob, 0x02); return 1; }
        if (strcmp(t, "DE") == 0) { ob_add(ob, 0x12); return 1; }
    }

    /* LD (IX+d), r / LD (IY+d), r */
    if (is_paren_indexed(d, base, disp) && rs >= 0 && rs != 6) {
        int val;
        if (!eval_soft(disp, &val)) val = 0;
        ob_add(ob, strcmp(base,"IX")==0 ? 0xDD : 0xFD);
        ob_add(ob, 0x70 | rs);
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }
    /* LD r,(IX+d) / LD r,(IY+d) */
    if (rd >= 0 && rd != 6 && is_paren_indexed(s, base, disp)) {
        int val;
        if (!eval_soft(disp, &val)) val = 0;
        ob_add(ob, strcmp(base,"IX")==0 ? 0xDD : 0xFD);
        ob_add(ob, 0x46 | (rd << 3));
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }
    /* LD (IX+d), n */
    if (is_paren_indexed(d, base, disp) && !is_paren(s, inner, sizeof(inner)) && reg8_code(s) < 0) {
        int val, dv;
        if (!eval_soft(disp, &dv)) dv = 0;
        if (!eval_soft(s, &val)) val = 0;
        ob_add(ob, strcmp(base,"IX")==0 ? 0xDD : 0xFD);
        ob_add(ob, 0x36);
        ob_add(ob, (uint8_t)(dv & 0xFF));
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }

    /* LD IXH/IXL/IYH/IYL, n または ,r (undocumented) */
    {
        int ud = reg8_code_undoc(d);
        int us = reg8_code_undoc(s);
        int is_ix_d = (strstr(d, "IX") || strstr(d,"ix"));
        int is_iy_d = (strstr(d, "IY") || strstr(d,"iy"));
        if (ud >= 0) {
            uint8_t prefix = is_ix_d ? 0xDD : 0xFD;
            if (us >= 0) {
                ob_add(ob, prefix);
                ob_add(ob, 0x40 | (ud << 3) | us);
                return 1;
            }
            if (rs >= 0 && rs != 6) {
                ob_add(ob, prefix);
                ob_add(ob, 0x40 | (ud << 3) | rs);
                return 1;
            }
            /* 即値 */
            int val;
            if (!eval_soft(s, &val)) val = 0;
            ob_add(ob, prefix);
            ob_add(ob, 0x06 | (ud << 3));
            ob_add(ob, (uint8_t)(val & 0xFF));
            return 1;
        }
    }

    /* LD A,I / LD A,R */
    if (is_reg_a(d) && is_reg_i(s)) { ob_add(ob, 0xED); ob_add(ob, 0x57); return 1; }
    if (is_reg_a(d) && is_reg_r(s)) { ob_add(ob, 0xED); ob_add(ob, 0x5F); return 1; }
    if (is_reg_i(d) && is_reg_a(s)) { ob_add(ob, 0xED); ob_add(ob, 0x47); return 1; }
    if (is_reg_r(d) && is_reg_a(s)) { ob_add(ob, 0xED); ob_add(ob, 0x4F); return 1; }

    /* LD SP,HL */
    if (is_reg_sp(d) && is_reg_hl(s)) { ob_add(ob, 0xF9); return 1; }
    /* LD SP,IX / LD SP,IY */
    if (is_reg_sp(d) && is_reg_ix(s)) { ob_add(ob, 0xDD); ob_add(ob, 0xF9); return 1; }
    if (is_reg_sp(d) && is_reg_iy(s)) { ob_add(ob, 0xFD); ob_add(ob, 0xF9); return 1; }

    /* LD dd, nn (BC/DE/HL/SP <- 即値) */
    {
        int rr = reg16_code_sp(d);
        if (rr >= 0 && !is_paren(s, inner, sizeof(inner))) {
            int val;
            if (!eval_soft(s, &val)) val = 0;
            ob_add(ob, 0x01 | (rr << 4));
            ob_add(ob, (uint8_t)(val & 0xFF));
            ob_add(ob, (uint8_t)((val >> 8) & 0xFF));
            return 1;
        }
    }
    /* LD IX,nn / LD IY,nn */
    if (is_reg_ix(d) && !is_paren(s, inner, sizeof(inner))) {
        int val; if (!eval_soft(s, &val)) val = 0;
        ob_add(ob, 0xDD); ob_add(ob, 0x21);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    if (is_reg_iy(d) && !is_paren(s, inner, sizeof(inner))) {
        int val; if (!eval_soft(s, &val)) val = 0;
        ob_add(ob, 0xFD); ob_add(ob, 0x21);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }

    /* LD HL,(nn) */
    if (is_reg_hl(d) && is_paren(s, inner, sizeof(inner)) && reg16_code_sp(inner) < 0) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0x2A);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    /* LD (nn),HL */
    if (is_paren(d, inner, sizeof(inner)) && is_reg_hl(s) && reg16_code_sp(inner) < 0) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0x22);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    /* LD dd,(nn) : BC/DE/SP <- (nn)  [ED prefix] */
    {
        int rr = reg16_code_sp(d);
        char inner2[MAX_OPERAND];
        if (rr >= 0 && rr != 2 /*HL handled above*/ && is_paren(s, inner2, sizeof(inner2))) {
            int val; if (!eval_soft(inner2, &val)) val = 0;
            ob_add(ob, 0xED);
            ob_add(ob, 0x4B | (rr << 4));
            ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
            return 1;
        }
        char inner3[MAX_OPERAND];
        int rr2 = reg16_code_sp(s);
        if (rr2 >= 0 && rr2 != 2 && is_paren(d, inner3, sizeof(inner3))) {
            int val; if (!eval_soft(inner3, &val)) val = 0;
            ob_add(ob, 0xED);
            ob_add(ob, 0x43 | (rr2 << 4));
            ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
            return 1;
        }
    }
    /* LD IX,(nn) / LD (nn),IX / IY同様 */
    if (is_reg_ix(d) && is_paren(s, inner, sizeof(inner))) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0xDD); ob_add(ob, 0x2A);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    if (is_paren(d, inner, sizeof(inner)) && is_reg_ix(s)) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0xDD); ob_add(ob, 0x22);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    if (is_reg_iy(d) && is_paren(s, inner, sizeof(inner))) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0xFD); ob_add(ob, 0x2A);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    if (is_paren(d, inner, sizeof(inner)) && is_reg_iy(s)) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0xFD); ob_add(ob, 0x22);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }

    /* LD A,(nn) / LD (nn),A */
    if (is_reg_a(d) && is_paren(s, inner, sizeof(inner))) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0x3A);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }
    if (is_paren(d, inner, sizeof(inner)) && is_reg_a(s)) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0x32);
        ob_add(ob, (uint8_t)(val & 0xFF)); ob_add(ob, (uint8_t)((val>>8)&0xFF));
        return 1;
    }

    /* LD (HL),n */
    if (is_paren(d, inner, sizeof(inner)) && strcmp(inner,"HL")==0) {
        int val; if (!eval_soft(s, &val)) val = 0;
        ob_add(ob, 0x36);
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }

    asm_error("LD命令のオペランドを解釈できません: LD %s,%s", d, s);
    return 0;
}

/* ============ ALU命令 (ADD/ADC/SUB/SBC/AND/XOR/OR/CP) ============ */
static int encode_ALU(SourceLine *sl, OutBuf *ob) {
    const char *mnem = sl->mnemonic;
    const AluOp *op = NULL;
    for (int i = 0; alu_ops[i].mnem; i++) {
        if (strcmp(alu_ops[i].mnem, mnem) == 0) { op = &alu_ops[i]; break; }
    }
    if (!op) return 0;

    const char *a1, *a2;
    if (op->need_a) {
        /* ADD/ADC/SBC は "A,r" または "HL,ss" / "IX,pp" 形式 */
        if (sl->has_op2) { a1 = sl->operand1; a2 = sl->operand2; }
        else { asm_error("%s には2つのオペランドが必要です", mnem); return 0; }

        if (is_reg_hl(a1)) {
            int rr = reg16_code_sp(a2);
            if (rr < 0) { asm_error("%s HL,?? のオペランドが不正です", mnem); return 0; }
            if (strcmp(mnem, "ADD") == 0) { ob_add(ob, 0x09 | (rr<<4)); return 1; }
            if (strcmp(mnem, "ADC") == 0) { ob_add(ob, 0xED); ob_add(ob, 0x4A | (rr<<4)); return 1; }
            if (strcmp(mnem, "SBC") == 0) { ob_add(ob, 0xED); ob_add(ob, 0x42 | (rr<<4)); return 1; }
        }
        if (is_reg_ix(a1) && strcmp(mnem,"ADD")==0) {
            int rr = reg16_code_sp(a2);
            /* ADD IX,IX は rr=HLコードを使うが実際はIX自身 -> Z80ではADD IX,IXも可(rp=HL code=2扱い) */
            char t[8]; strncpy(t,a2,7); t[7]=0; to_upper(t); trim(t);
            if (strcmp(t,"IX")==0) rr = 2;
            if (rr < 0) { asm_error("ADD IX,?? のオペランドが不正です"); return 0; }
            ob_add(ob, 0xDD); ob_add(ob, 0x09 | (rr<<4));
            return 1;
        }
        if (is_reg_iy(a1) && strcmp(mnem,"ADD")==0) {
            int rr = reg16_code_sp(a2);
            char t[8]; strncpy(t,a2,7); t[7]=0; to_upper(t); trim(t);
            if (strcmp(t,"IY")==0) rr = 2;
            if (rr < 0) { asm_error("ADD IY,?? のオペランドが不正です"); return 0; }
            ob_add(ob, 0xFD); ob_add(ob, 0x09 | (rr<<4));
            return 1;
        }
        if (!is_reg_a(a1)) { asm_error("%s の第1オペランドはAである必要があります", mnem); return 0; }
        /* A, r/n/(HL)/(IX+d) の形へフォールスルー */
        a2 = sl->operand2;
    } else {
        /* SUB/AND/XOR/OR/CP は "r" 単独、または "A,r" どちらも許容 */
        if (sl->has_op2) {
            if (!is_reg_a(sl->operand1)) { asm_error("%s の第1オペランドはAである必要があります", mnem); return 0; }
            a2 = sl->operand2;
        } else {
            a2 = sl->operand1;
        }
    }

    int r = reg8_code(a2);
    if (r >= 0) {
        ob_add(ob, op->base_r | r);
        return 1;
    }
    char base[8], disp[MAX_OPERAND];
    if (is_paren_indexed(a2, base, disp)) {
        int dv; if (!eval_soft(disp, &dv)) dv = 0;
        ob_add(ob, strcmp(base,"IX")==0 ? 0xDD : 0xFD);
        ob_add(ob, op->base_r | 6);
        ob_add(ob, (uint8_t)(dv & 0xFF));
        return 1;
    }
    int ud = reg8_code_undoc(a2);
    if (ud >= 0) {
        int is_ix = (strstr(a2,"IX")||strstr(a2,"ix"));
        ob_add(ob, is_ix ? 0xDD : 0xFD);
        ob_add(ob, op->base_r | ud);
        return 1;
    }
    /* 即値 n */
    int val;
    if (!eval_soft(a2, &val)) val = 0;
    ob_add(ob, op->base_n);
    ob_add(ob, (uint8_t)(val & 0xFF));
    return 1;
}

/* ============ INC/DEC ============ */
static int encode_INC_DEC(SourceLine *sl, OutBuf *ob) {
    int is_inc = (strcmp(sl->mnemonic, "INC") == 0);
    const char *a = sl->operand1;
    int r = reg8_code(a);
    if (r >= 0) {
        ob_add(ob, (is_inc ? 0x04 : 0x05) | (r << 3));
        return 1;
    }
    int rr = reg16_code_sp(a);
    if (rr >= 0) {
        ob_add(ob, (is_inc ? 0x03 : 0x0B) | (rr << 4));
        return 1;
    }
    if (is_reg_ix(a)) { ob_add(ob, 0xDD); ob_add(ob, is_inc?0x23:0x2B); return 1; }
    if (is_reg_iy(a)) { ob_add(ob, 0xFD); ob_add(ob, is_inc?0x23:0x2B); return 1; }
    char base[8], disp[MAX_OPERAND];
    if (is_paren_indexed(a, base, disp)) {
        int dv; if (!eval_soft(disp, &dv)) dv = 0;
        ob_add(ob, strcmp(base,"IX")==0?0xDD:0xFD);
        ob_add(ob, (is_inc?0x34:0x35));
        ob_add(ob, (uint8_t)(dv & 0xFF));
        return 1;
    }
    int ud = reg8_code_undoc(a);
    if (ud >= 0) {
        int is_ix = (strstr(a,"IX")||strstr(a,"ix"));
        ob_add(ob, is_ix?0xDD:0xFD);
        ob_add(ob, (is_inc?0x04:0x05) | (ud<<3));
        return 1;
    }
    asm_error("%s のオペランドが不正です: %s", sl->mnemonic, a);
    return 0;
}

/* ============ ローテート/シフト (CB prefix) ============ */
static int encode_ROT(SourceLine *sl, OutBuf *ob) {
    const RotOp *op = NULL;
    for (int i = 0; rot_ops[i].mnem; i++) {
        if (strcmp(rot_ops[i].mnem, sl->mnemonic) == 0) { op = &rot_ops[i]; break; }
    }
    if (!op) return 0;
    const char *a = sl->operand1;
    int r = reg8_code(a);
    if (r >= 0) {
        ob_add(ob, 0xCB);
        ob_add(ob, op->base | r);
        return 1;
    }
    char base[8], disp[MAX_OPERAND];
    if (is_paren_indexed(a, base, disp)) {
        int dv; if (!eval_soft(disp, &dv)) dv = 0;
        ob_add(ob, strcmp(base,"IX")==0?0xDD:0xFD);
        ob_add(ob, 0xCB);
        ob_add(ob, (uint8_t)(dv & 0xFF));
        ob_add(ob, op->base | 6);
        return 1;
    }
    asm_error("%s のオペランドが不正です: %s", sl->mnemonic, a);
    return 0;
}

/* ============ BIT/SET/RES ============ */
static int encode_BIT(SourceLine *sl, OutBuf *ob) {
    uint8_t base;
    if (strcmp(sl->mnemonic,"BIT")==0) base = 0x40;
    else if (strcmp(sl->mnemonic,"RES")==0) base = 0x80;
    else if (strcmp(sl->mnemonic,"SET")==0) base = 0xC0;
    else return 0;

    if (!sl->has_op2) { asm_error("%s にはビット番号とオペランドが必要です", sl->mnemonic); return 0; }
    int bitnum;
    if (!eval_soft(sl->operand1, &bitnum)) bitnum = 0;
    if (bitnum < 0 || bitnum > 7) { asm_warning("ビット番号は0-7である必要があります: %d", bitnum); }

    const char *a = sl->operand2;
    int r = reg8_code(a);
    if (r >= 0) {
        ob_add(ob, 0xCB);
        ob_add(ob, base | (bitnum << 3) | r);
        return 1;
    }
    char base_idx[8], disp[MAX_OPERAND];
    if (is_paren_indexed(a, base_idx, disp)) {
        int dv; if (!eval_soft(disp, &dv)) dv = 0;
        ob_add(ob, strcmp(base_idx,"IX")==0?0xDD:0xFD);
        ob_add(ob, 0xCB);
        ob_add(ob, (uint8_t)(dv & 0xFF));
        ob_add(ob, base | (bitnum << 3) | 6);
        return 1;
    }
    asm_error("%s のオペランドが不正です: %s", sl->mnemonic, a);
    return 0;
}

/* ============ JP/JR/CALL/RET/DJNZ ============ */
static int encode_JP(SourceLine *sl, OutBuf *ob) {
    /* JP nn / JP cc,nn / JP (HL) / JP (IX) / JP (IY) */
    if (!sl->has_op1) { asm_error("JPにはオペランドが必要です"); return 0; }
    char inner[MAX_OPERAND];
    if (sl->has_op2) {
        int cc = cond_code(sl->operand1);
        if (cc < 0) { asm_error("JPの条件コードが不正です: %s", sl->operand1); return 0; }
        int val; if (!eval_soft(sl->operand2, &val)) val = 0;
        ob_add(ob, 0xC2 | (cc << 3));
        ob_add(ob, (uint8_t)(val & 0xFF));
        ob_add(ob, (uint8_t)((val >> 8) & 0xFF));
        return 1;
    }
    if (is_paren(sl->operand1, inner, sizeof(inner))) {
        char t[8]; strncpy(t,inner,7); t[7]=0; to_upper(t); trim(t);
        if (strcmp(t,"HL")==0) { ob_add(ob, 0xE9); return 1; }
        if (strcmp(t,"IX")==0) { ob_add(ob, 0xDD); ob_add(ob, 0xE9); return 1; }
        if (strcmp(t,"IY")==0) { ob_add(ob, 0xFD); ob_add(ob, 0xE9); return 1; }
    }
    int val; if (!eval_soft(sl->operand1, &val)) val = 0;
    ob_add(ob, 0xC3);
    ob_add(ob, (uint8_t)(val & 0xFF));
    ob_add(ob, (uint8_t)((val >> 8) & 0xFF));
    return 1;
}

static int encode_JR(SourceLine *sl, OutBuf *ob) {
    const char *target;
    int cc = -1;
    if (sl->has_op2) {
        char t[8]; strncpy(t, sl->operand1, 7); t[7]=0; to_upper(t); trim(t);
        if (strcmp(t,"Z")==0) cc = 0;      /* JR Z  -> 0x28 */
        else if (strcmp(t,"NZ")==0) cc = 1; /* JR NZ -> 0x20 */
        else if (strcmp(t,"C")==0) cc = 2;  /* JR C  -> 0x38 */
        else if (strcmp(t,"NC")==0) cc = 3; /* JR NC -> 0x30 */
        else { asm_error("JRの条件コードが不正です(Z/NZ/C/NCのみ): %s", sl->operand1); return 0; }
        target = sl->operand2;
    } else {
        target = sl->operand1;
    }
    int addr;
    if (!eval_soft(target, &addr)) addr = pc + 2;
    int offset = addr - (pc + 2);
    if (pass == 2 && (offset < -128 || offset > 127)) {
        asm_error("JRの飛び先が範囲外です(相対 %d): %s", offset, target);
    }
    static const uint8_t opc[4] = {0x28, 0x20, 0x38, 0x30};
    if (cc < 0) {
        ob_add(ob, 0x18);
    } else {
        ob_add(ob, opc[cc]);
    }
    ob_add(ob, (uint8_t)(offset & 0xFF));
    return 1;
}

static int encode_DJNZ(SourceLine *sl, OutBuf *ob) {
    int addr;
    if (!eval_soft(sl->operand1, &addr)) addr = pc + 2;
    int offset = addr - (pc + 2);
    if (pass == 2 && (offset < -128 || offset > 127)) {
        asm_error("DJNZの飛び先が範囲外です(相対 %d)", offset);
    }
    ob_add(ob, 0x10);
    ob_add(ob, (uint8_t)(offset & 0xFF));
    return 1;
}

static int encode_CALL(SourceLine *sl, OutBuf *ob) {
    if (sl->has_op2) {
        int cc = cond_code(sl->operand1);
        if (cc < 0) { asm_error("CALLの条件コードが不正です: %s", sl->operand1); return 0; }
        int val; if (!eval_soft(sl->operand2, &val)) val = 0;
        ob_add(ob, 0xC4 | (cc << 3));
        ob_add(ob, (uint8_t)(val & 0xFF));
        ob_add(ob, (uint8_t)((val >> 8) & 0xFF));
        return 1;
    }
    int val; if (!eval_soft(sl->operand1, &val)) val = 0;
    ob_add(ob, 0xCD);
    ob_add(ob, (uint8_t)(val & 0xFF));
    ob_add(ob, (uint8_t)((val >> 8) & 0xFF));
    return 1;
}

static int encode_RET_CC(SourceLine *sl, OutBuf *ob) {
    if (sl->has_op1) {
        int cc = cond_code(sl->operand1);
        if (cc < 0) { asm_error("RETの条件コードが不正です: %s", sl->operand1); return 0; }
        ob_add(ob, 0xC0 | (cc << 3));
        return 1;
    }
    ob_add(ob, 0xC9);
    return 1;
}

static int encode_RST(SourceLine *sl, OutBuf *ob) {
    int val; if (!eval_soft(sl->operand1, &val)) val = 0;
    /* 有効値: 00,08,10,18,20,28,30,38 */
    if (val != 0x00 && val != 0x08 && val != 0x10 && val != 0x18 &&
        val != 0x20 && val != 0x28 && val != 0x30 && val != 0x38) {
        asm_warning("RSTの値は00/08/10/18/20/28/30/38hのいずれかである必要があります: %02Xh", val);
    }
    ob_add(ob, 0xC7 | (val & 0x38));
    return 1;
}

/* ============ PUSH/POP ============ */
static int encode_PUSH_POP(SourceLine *sl, OutBuf *ob) {
    int is_push = (strcmp(sl->mnemonic, "PUSH") == 0);
    const char *a = sl->operand1;
    if (is_reg_ix(a)) { ob_add(ob, 0xDD); ob_add(ob, is_push?0xE5:0xE1); return 1; }
    if (is_reg_iy(a)) { ob_add(ob, 0xFD); ob_add(ob, is_push?0xE5:0xE1); return 1; }
    int rr = reg16_code_af(a);
    if (rr < 0) { asm_error("%s のオペランドが不正です: %s", sl->mnemonic, a); return 0; }
    ob_add(ob, (is_push?0xC5:0xC1) | (rr << 4));
    return 1;
}

/* ============ EX ============ */
static int encode_EX(SourceLine *sl, OutBuf *ob) {
    if (!sl->has_op2) { asm_error("EXには2つのオペランドが必要です"); return 0; }
    const char *a = sl->operand1, *b = sl->operand2;
    char t1[16], t2[16];
    strncpy(t1,a,15); t1[15]=0; to_upper(t1); trim(t1);
    strncpy(t2,b,15); t2[15]=0; to_upper(t2); trim(t2);
    if (strcmp(t1,"AF")==0 && strcmp(t2,"AF'")==0) { ob_add(ob, 0x08); return 1; }
    if (strcmp(t1,"DE")==0 && strcmp(t2,"HL")==0) { ob_add(ob, 0xEB); return 1; }
    char inner[MAX_OPERAND];
    if (is_paren(a, inner, sizeof(inner))) {
        char ti[8]; strncpy(ti,inner,7); ti[7]=0; to_upper(ti); trim(ti);
        if (strcmp(ti,"SP")==0) {
            if (strcmp(t2,"HL")==0) { ob_add(ob, 0xE3); return 1; }
            if (strcmp(t2,"IX")==0) { ob_add(ob, 0xDD); ob_add(ob, 0xE3); return 1; }
            if (strcmp(t2,"IY")==0) { ob_add(ob, 0xFD); ob_add(ob, 0xE3); return 1; }
        }
    }
    asm_error("EXのオペランドを解釈できません: EX %s,%s", a, b);
    return 0;
}

/* ============ IN/OUT ============ */
static int encode_IN(SourceLine *sl, OutBuf *ob) {
    if (!sl->has_op2) { asm_error("INには2つのオペランドが必要です"); return 0; }
    char inner[MAX_OPERAND];
    if (!is_paren(sl->operand2, inner, sizeof(inner))) { asm_error("IN r,(port) の形式が必要です"); return 0; }
    char t[16]; strncpy(t,inner,15); t[15]=0; to_upper(t); trim(t);
    if (strcmp(t,"C")==0) {
        int r = reg8_code(sl->operand1);
        if (r < 0) { asm_error("IN r,(C) のレジスタが不正です: %s", sl->operand1); return 0; }
        ob_add(ob, 0xED);
        ob_add(ob, 0x40 | (r << 3));
        return 1;
    }
    if (is_reg_a(sl->operand1)) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0xDB);
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }
    asm_error("INのオペランドを解釈できません");
    return 0;
}

static int encode_OUT(SourceLine *sl, OutBuf *ob) {
    if (!sl->has_op2) { asm_error("OUTには2つのオペランドが必要です"); return 0; }
    char inner[MAX_OPERAND];
    if (!is_paren(sl->operand1, inner, sizeof(inner))) { asm_error("OUT (port),r の形式が必要です"); return 0; }
    char t[16]; strncpy(t,inner,15); t[15]=0; to_upper(t); trim(t);
    if (strcmp(t,"C")==0) {
        int r = reg8_code(sl->operand2);
        if (r < 0) { asm_error("OUT (C),r のレジスタが不正です: %s", sl->operand2); return 0; }
        ob_add(ob, 0xED);
        ob_add(ob, 0x41 | (r << 3));
        return 1;
    }
    if (is_reg_a(sl->operand2)) {
        int val; if (!eval_soft(inner, &val)) val = 0;
        ob_add(ob, 0xD3);
        ob_add(ob, (uint8_t)(val & 0xFF));
        return 1;
    }
    asm_error("OUTのオペランドを解釈できません");
    return 0;
}

/* ============ IM ============ */
static int encode_IM(SourceLine *sl, OutBuf *ob) {
    int val; if (!eval_soft(sl->operand1, &val)) val = 0;
    ob_add(ob, 0xED);
    if (val == 0) ob_add(ob, 0x46);
    else if (val == 1) ob_add(ob, 0x56);
    else if (val == 2) ob_add(ob, 0x5E);
    else { asm_error("IMには0,1,2のいずれかを指定してください"); return 0; }
    return 1;
}

/* ============ ディレクティブ ============ */
static int encode_directive(SourceLine *sl, OutBuf *ob, int *is_directive) {
    *is_directive = 1;
    const char *m = sl->mnemonic;

    if (strcmp(m, ".ORG") == 0 || strcmp(m, "ORG") == 0) {
        int val; if (!eval_soft(sl->operand1, &val)) val = 0;
        org_base = val;
        pc = val;
        return 1;
    }
    if (strcmp(m, ".DB") == 0 || strcmp(m, "DB") == 0 || strcmp(m, ".BYTE") == 0) {
        /* カンマ区切りの複数値・文字列に対応するため、raw行から再パースする */
        char work[MAX_LINE];
        /* operand1,operand2 だけでは3つ目以降が失われるため raw から抽出 */
        const char *p = sl->raw;
        /* mnemonic部分をスキップ */
        while (*p && isspace((unsigned char)*p)) p++;
        /* ラベルがあれば飛ばす(すでにhas_labelで判定済みなので、mnemonicの位置を探す) */
        /* 簡易実装: mnemonic文字列を検索してその後ろから読む */
        const char *mp = strstr(sl->raw, sl->mnemonic);
        if (!mp) mp = sl->raw;
        p = mp + strlen(sl->mnemonic);
        strncpy(work, p, sizeof(work)-1); work[sizeof(work)-1]=0;
        trim(work);

        char *tok = work;
        int in_squote=0, in_dquote=0, depth=0;
        char item[MAX_OPERAND]; int ilen=0;
        for (int i = 0; ; i++) {
            char c = tok[i];
            int end = (c == '\0');
            if (!end) {
                if (c=='\'' && !in_dquote) in_squote=!in_squote;
                else if (c=='"' && !in_squote) in_dquote=!in_dquote;
                else if (!in_squote && !in_dquote) {
                    if (c=='(') depth++;
                    else if (c==')') depth--;
                }
            }
            if ((c==',' && depth==0 && !in_squote && !in_dquote) || end) {
                item[ilen]='\0';
                trim(item);
                if (item[0] != '\0') {
                    /* 文字列リテラル "..." */
                    if (item[0]=='"') {
                        int L = (int)strlen(item);
                        for (int k=1; k<L-1; k++) {
                            ob_add(ob, (uint8_t)item[k]);
                        }
                    } else {
                        int val; if (!eval_soft(item, &val)) val = 0;
                        ob_add(ob, (uint8_t)(val & 0xFF));
                    }
                }
                ilen = 0;
                if (end) break;
                continue;
            }
            item[ilen++] = c;
        }
        return 1;
    }
    if (strcmp(m, ".DW") == 0 || strcmp(m, "DW") == 0 || strcmp(m, ".WORD") == 0) {
        const char *mp = strstr(sl->raw, sl->mnemonic);
        char work[MAX_LINE];
        const char *p = mp ? mp + strlen(sl->mnemonic) : sl->raw;
        strncpy(work, p, sizeof(work)-1); work[sizeof(work)-1]=0;
        trim(work);
        char *tok = work;
        char item[MAX_OPERAND]; int ilen=0;
        int depth=0;
        for (int i=0;;i++) {
            char c = tok[i];
            int end = (c=='\0');
            if (!end) { if (c=='(') depth++; else if (c==')') depth--; }
            if ((c==',' && depth==0) || end) {
                item[ilen]='\0'; trim(item);
                if (item[0]) {
                    int val; if (!eval_soft(item, &val)) val = 0;
                    ob_add(ob, (uint8_t)(val & 0xFF));
                    ob_add(ob, (uint8_t)((val>>8) & 0xFF));
                }
                ilen=0;
                if (end) break;
                continue;
            }
            item[ilen++]=c;
        }
        return 1;
    }
    if (strcmp(m, ".DS") == 0 || strcmp(m, "DS") == 0 || strcmp(m, ".DEFS") == 0) {
        int count; if (!eval_soft(sl->operand1, &count)) count = 0;
        int fill = 0;
        if (sl->has_op2) { if (!eval_soft(sl->operand2, &fill)) fill = 0; }
        if (count < 0) { asm_error(".DSのサイズが負の値です: %d", count); count = 0; }
        if (count > MAX_BIN) { asm_error(".DSのサイズが大きすぎます: %d", count); count = MAX_BIN; }
        for (int i = 0; i < count; i++) ob_add(ob, (uint8_t)(fill & 0xFF));
        return 1;
    }
    if (strcmp(m, ".EQU") == 0 || strcmp(m, "EQU") == 0) {
        /* .EQU はラベル定義行として別途処理される(pass1で処理済み) */
        return 1;
    }
    if (strcmp(m, ".END") == 0 || strcmp(m, "END") == 0) {
        return 1;
    }

    *is_directive = 0;
    return 0;
}

/* ============ メインディスパッチ ============ */
int encode_instruction(SourceLine *sl, uint8_t *out, int *out_len) {
    OutBuf ob = { out, 0 };
    const char *m = sl->mnemonic;
    if (m[0] == '\0') { *out_len = 0; return 1; }

    /* ディレクティブ判定 */
    int is_dir = 0;
    int handled = 0;
    if (m[0] == '.' || strcmp(m,"ORG")==0 || strcmp(m,"DB")==0 || strcmp(m,"DW")==0 ||
        strcmp(m,"DS")==0 || strcmp(m,"EQU")==0 || strcmp(m,"END")==0 || strcmp(m,"BYTE")==0 || strcmp(m,"WORD")==0 || strcmp(m,"DEFS")==0) {
        handled = encode_directive(sl, &ob, &is_dir);
        if (is_dir) { *out_len = ob.len; return handled; }
    }

    /* 単純命令(オペランド無し固定バイト列) */
    for (int i = 0; simple_ops[i].mnem; i++) {
        if (strcmp(simple_ops[i].mnem, m) == 0) {
            for (int j = 0; j < simple_ops[i].len; j++) ob_add(&ob, simple_ops[i].bytes[j]);
            *out_len = ob.len;
            return 1;
        }
    }

    int ok = 1;
    if (strcmp(m, "LD") == 0) { ok = encode_LD(sl, &ob); }
    else if (strcmp(m,"ADD")==0||strcmp(m,"ADC")==0||strcmp(m,"SUB")==0||strcmp(m,"SBC")==0||
             strcmp(m,"AND")==0||strcmp(m,"XOR")==0||strcmp(m,"OR")==0||strcmp(m,"CP")==0) {
        ok = encode_ALU(sl, &ob);
    }
    else if (strcmp(m,"INC")==0 || strcmp(m,"DEC")==0) { ok = encode_INC_DEC(sl, &ob); }
    else if (strcmp(m,"RLC")==0||strcmp(m,"RRC")==0||strcmp(m,"RL")==0||strcmp(m,"RR")==0||
             strcmp(m,"SLA")==0||strcmp(m,"SRA")==0||strcmp(m,"SLL")==0||strcmp(m,"SRL")==0) {
        ok = encode_ROT(sl, &ob);
    }
    else if (strcmp(m,"BIT")==0||strcmp(m,"SET")==0||strcmp(m,"RES")==0) { ok = encode_BIT(sl, &ob); }
    else if (strcmp(m,"JP")==0) { ok = encode_JP(sl, &ob); }
    else if (strcmp(m,"JR")==0) { ok = encode_JR(sl, &ob); }
    else if (strcmp(m,"DJNZ")==0) { ok = encode_DJNZ(sl, &ob); }
    else if (strcmp(m,"CALL")==0) { ok = encode_CALL(sl, &ob); }
    else if (strcmp(m,"RET")==0) { ok = encode_RET_CC(sl, &ob); }
    else if (strcmp(m,"RST")==0) { ok = encode_RST(sl, &ob); }
    else if (strcmp(m,"PUSH")==0||strcmp(m,"POP")==0) { ok = encode_PUSH_POP(sl, &ob); }
    else if (strcmp(m,"EX")==0) { ok = encode_EX(sl, &ob); }
    else if (strcmp(m,"IN")==0) { ok = encode_IN(sl, &ob); }
    else if (strcmp(m,"OUT")==0) { ok = encode_OUT(sl, &ob); }
    else if (strcmp(m,"IM")==0) { ok = encode_IM(sl, &ob); }
    else {
        asm_error("未知の命令です: %s", m);
        ok = 0;
    }

    *out_len = ob.len;
    return ok;
}