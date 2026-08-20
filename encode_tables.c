/*
 * encode_tables.c - オペランド無し(固定バイト列)命令のテーブル
 * encode.c から #include される
 */

typedef struct {
    const char *mnem;
    uint8_t bytes[4];
    int len;
} SimpleOp;

static const SimpleOp simple_ops[] = {
    { "NOP",   {0x00}, 1 },
    { "HALT",  {0x76}, 1 },
    { "DI",    {0xF3}, 1 },
    { "EI",    {0xFB}, 1 },
    { "EXX",   {0xD9}, 1 },
    { "RLCA",  {0x07}, 1 },
    { "RRCA",  {0x0F}, 1 },
    { "RLA",   {0x17}, 1 },
    { "RRA",   {0x1F}, 1 },
    { "DAA",   {0x27}, 1 },
    { "CPL",   {0x2F}, 1 },
    { "SCF",   {0x37}, 1 },
    { "CCF",   {0x3F}, 1 },
    { "RETI",  {0xED,0x4D}, 2 },
    { "RETN",  {0xED,0x45}, 2 },
    { "NEG",   {0xED,0x44}, 2 },
    { "RLD",   {0xED,0x6F}, 2 },
    { "RRD",   {0xED,0x67}, 2 },
    { "LDI",   {0xED,0xA0}, 2 },
    { "LDIR",  {0xED,0xB0}, 2 },
    { "LDD",   {0xED,0xA8}, 2 },
    { "LDDR",  {0xED,0xB8}, 2 },
    { "CPI",   {0xED,0xA1}, 2 },
    { "CPIR",  {0xED,0xB1}, 2 },
    { "CPD",   {0xED,0xA9}, 2 },
    { "CPDR",  {0xED,0xB9}, 2 },
    { "INI",   {0xED,0xA2}, 2 },
    { "INIR",  {0xED,0xB2}, 2 },
    { "IND",   {0xED,0xAA}, 2 },
    { "INDR",  {0xED,0xBA}, 2 },
    { "OUTI",  {0xED,0xA3}, 2 },
    { "OTIR",  {0xED,0xB3}, 2 },
    { "OUTD",  {0xED,0xAB}, 2 },
    { "OTDR",  {0xED,0xBB}, 2 },
    { NULL, {0}, 0 }
};

/* ALU演算(A, r) の共通ベースオペコード: ADD/ADC/SUB/SBC/AND/XOR/OR/CP */
typedef struct {
    const char *mnem;
    uint8_t base_r;    /* r形式(レジスタ直接) 例 ADD A,r -> 0x80 + r */
    uint8_t base_n;    /* n形式(即値) 例 ADD A,n -> 0xC6 */
    int need_a;         /* 第1オペランドにAが必要か(ADD/ADCのみ2オペランド必須) */
} AluOp;

static const AluOp alu_ops[] = {
    { "ADD", 0x80, 0xC6, 1 },
    { "ADC", 0x88, 0xCE, 1 },
    { "SUB", 0x90, 0xD6, 0 },
    { "SBC", 0x98, 0xDE, 1 },
    { "AND", 0xA0, 0xE6, 0 },
    { "XOR", 0xA8, 0xEE, 0 },
    { "OR",  0xB0, 0xF6, 0 },
    { "CP",  0xB8, 0xFE, 0 },
    { NULL, 0,0,0 }
};

/* ローテート/シフト系 CB prefix: RLC,RRC,RL,RR,SLA,SRA,SLL,SRL */
typedef struct {
    const char *mnem;
    uint8_t base; /* base + r */
} RotOp;

static const RotOp rot_ops[] = {
    { "RLC", 0x00 },
    { "RRC", 0x08 },
    { "RL",  0x10 },
    { "RR",  0x18 },
    { "SLA", 0x20 },
    { "SRA", 0x28 },
    { "SLL", 0x30 }, /* undocumented */
    { "SRL", 0x38 },
    { NULL, 0 }
};