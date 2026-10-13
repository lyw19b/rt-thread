#ifndef __ASM_INST_H__
#define __ASM_INST_H__

enum reg0i15_op {
    break_op  = 0x54,
};

enum reg0i26_op {
    b_op   = 0x14,
    bl_op  = 0x15,
};

enum reg1i20_op {
    lu12iw_op     = 0x0a,
    lu32id_op     = 0x0b,
    pcaddi_op     = 0x0c,
    pcalau12i_op  = 0x0d,
    pcaddu12i_op  = 0x0e,
    pcaddu18i_op  = 0x0f,
};

enum reg1i21_op {
    beqz_op   = 0x10,
    bnez_op   = 0x11,
    bceqz_op  = 0x12, /* bits[9:8] = 0x00 */
    bcnez_op  = 0x12, /* bits[9:8] = 0x01 */
};

enum reg2_op {
    revb2h_op    = 0x0c,
    revb4h_op    = 0x0d,
    revb2w_op    = 0x0e,
    revbd_op     = 0x0f,
    revh2w_op    = 0x10,
    revhd_op     = 0x11,
    extwh_op     = 0x16,
    extwb_op     = 0x17,
    cpucfg_op    = 0x1b,
    iocsrrdb_op  = 0x19200,
    iocsrrdh_op  = 0x19201,
    iocsrrdw_op  = 0x19202,
    iocsrrdd_op  = 0x19203,
    iocsrwrb_op  = 0x19204,
    iocsrwrh_op  = 0x19205,
    iocsrwrw_op  = 0x19206,
    iocsrwrd_op  = 0x19207,
};

enum reg2i5_op {
    slliw_op  = 0x81,
    srliw_op  = 0x89,
    sraiw_op  = 0x91,
};

enum reg2i6_op {
    sllid_op  = 0x41,
    srlid_op  = 0x45,
    sraid_op  = 0x49,
};

enum reg2i12_op {
    addiw_op   = 0x0a,
    addid_op   = 0x0b,
    lu52id_op  = 0x0c,
    andi_op    = 0x0d,
    ori_op     = 0x0e,
    xori_op    = 0x0f,
    ldb_op     = 0xa0,
    ldh_op     = 0xa1,
    ldw_op     = 0xa2,
    ldd_op     = 0xa3,
    stb_op     = 0xa4,
    sth_op     = 0xa5,
    stw_op     = 0xa6,
    std_op     = 0xa7,
    ldbu_op    = 0xa8,
    ldhu_op    = 0xa9,
    ldwu_op    = 0xaa,
    flds_op     = 0xac,
    fsts_op    = 0xad,
    fldd_op     = 0xae,
    fstd_op    = 0xaf,
};

enum reg2i14_op {
    llw_op     = 0x20,
    scw_op     = 0x21,
    lld_op     = 0x22,
    scd_op     = 0x23,
    ldptrw_op  = 0x24,
    stptrw_op  = 0x25,
    ldptrd_op  = 0x26,
    stptrd_op  = 0x27,
};

enum reg2i16_op {
    jirl_op  = 0x13,
    beq_op   = 0x16,
    bne_op   = 0x17,
    blt_op   = 0x18,
    bge_op   = 0x19,
    bltu_op  = 0x1a,
    bgeu_op  = 0x1b,
};

enum reg2bstrd_op {
    bstrinsd_op   = 0x2,
    bstrpickd_op  = 0x3,
};

enum reg3_op {
    asrtle_op     = 0x02,
    asrtgt_op     = 0x03,
    addw_op       = 0x20,
    addd_op       = 0x21,
    subw_op       = 0x22,
    subd_op       = 0x23,
    nor_op        = 0x28,
    and_op        = 0x29,
    or_op         = 0x2a,
    xor_op        = 0x2b,
    orn_op        = 0x2c,
    andn_op       = 0x2d,
    sllw_op       = 0x2e,
    srlw_op       = 0x2f,
    sraw_op       = 0x30,
    slld_op       = 0x31,
    srld_op       = 0x32,
    srad_op       = 0x33,
    mulw_op       = 0x38,
    mulhw_op      = 0x39,
    mulhwu_op     = 0x3a,
    muld_op       = 0x3b,
    mulhd_op      = 0x3c,
    mulhdu_op     = 0x3d,
    divw_op       = 0x40,
    modw_op       = 0x41,
    divwu_op      = 0x42,
    modwu_op      = 0x43,
    divd_op       = 0x44,
    modd_op       = 0x45,
    divdu_op      = 0x46,
    moddu_op      = 0x47,
    ldxb_op       = 0x7000,
    ldxh_op       = 0x7008,
    ldxw_op       = 0x7010,
    ldxd_op       = 0x7018,
    stxb_op       = 0x7020,
    stxh_op       = 0x7028,
    stxw_op       = 0x7030,
    stxd_op       = 0x7038,
    ldxbu_op      = 0x7040,
    ldxhu_op      = 0x7048,
    ldxwu_op      = 0x7050,
    fldxs_op       = 0x7060,
    fldxd_op       = 0x7068,
    fstxs_op      = 0x7070,
    fstxd_op      = 0x7078,
    amswapw_op    = 0x70c0,
    amswapd_op    = 0x70c1,
    amaddw_op     = 0x70c2,
    amaddd_op     = 0x70c3,
    amandw_op     = 0x70c4,
    amandd_op     = 0x70c5,
    amorw_op      = 0x70c6,
    amord_op      = 0x70c7,
    amxorw_op     = 0x70c8,
    amxord_op     = 0x70c9,
    ammaxw_op     = 0x70ca,
    ammaxd_op     = 0x70cb,
    amminw_op     = 0x70cc,
    ammind_op     = 0x70cd,
    ammaxwu_op    = 0x70ce,
    ammaxdu_op    = 0x70cf,
    amminwu_op    = 0x70d0,
    ammindu_op    = 0x70d1,
    amswapdbw_op  = 0x70d2,
    amswapdbd_op  = 0x70d3,
    amadddbw_op   = 0x70d4,
    amadddbd_op   = 0x70d5,
    amanddbw_op   = 0x70d6,
    amanddbd_op   = 0x70d7,
    amordbw_op    = 0x70d8,
    amordbd_op    = 0x70d9,
    amxordbw_op   = 0x70da,
    amxordbd_op   = 0x70db,
    ammaxdbw_op   = 0x70dc,
    ammaxdbd_op   = 0x70dd,
    ammindbw_op   = 0x70de,
    ammindbd_op   = 0x70df,
    ammaxdbwu_op  = 0x70e0,
    ammaxdbdu_op  = 0x70e1,
    ammindbwu_op  = 0x70e2,
    ammindbdu_op  = 0x70e3,
    fldgts_op      = 0x70e8,
    fldgtd_op      = 0x70e9,
    fldles_op      = 0x70ea,
    fldled_op      = 0x70eb,
    fstgts_op     = 0x70ec,
    fstgtd_op     = 0x70ed,
    fstles_op     = 0x70ee,
    fstled_op     = 0x70ef,
    ldgtb_op      = 0x70f0,
    ldgth_op      = 0x70f1,
    ldgtw_op      = 0x70f2,
    ldgtd_op      = 0x70f3,
    ldleb_op      = 0x70f4,
    ldleh_op      = 0x70f5,
    ldlew_op      = 0x70f6,
    ldled_op      = 0x70f7,
    stgtb_op      = 0x70f8,
    stgth_op      = 0x70f9,
    stgtw_op      = 0x70fa,
    stgtd_op      = 0x70fb,
    stleb_op      = 0x70fc,
    stleh_op      = 0x70fd,
    stlew_op      = 0x70fe,
    stled_op      = 0x70ff,
};

enum reg3sa2_op {
    alslw_op    = 0x02,
    alslwu_op   = 0x03,
    alsld_op    = 0x16,
};

struct reg0i15_format {
    unsigned int immediate : 15;
    unsigned int opcode : 17;
};

struct reg0i26_format {
    unsigned int immediate_h : 10;
    unsigned int immediate_l : 16;
    unsigned int opcode : 6;
};

struct reg1i20_format {
    unsigned int rd : 5;
    unsigned int immediate : 20;
    unsigned int opcode : 7;
};

struct reg1i21_format {
    unsigned int immediate_h  : 5;
    unsigned int rj : 5;
    unsigned int immediate_l : 16;
    unsigned int opcode : 6;
};

struct reg2_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int opcode : 22;
};

struct reg2i5_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int immediate : 5;
    unsigned int opcode : 17;
};

struct reg2i6_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int immediate : 6;
    unsigned int opcode : 16;
};

struct reg2i12_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int immediate : 12;
    unsigned int opcode : 10;
};

struct reg2i14_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int immediate : 14;
    unsigned int opcode : 8;
};

struct reg2i16_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int immediate : 16;
    unsigned int opcode : 6;
};

struct reg2bstrd_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int lsbd : 6;
    unsigned int msbd : 6;
    unsigned int opcode : 10;
};

struct reg2csr_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int csr : 14;
    unsigned int opcode : 8;
};

struct reg3_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int rk : 5;
    unsigned int opcode : 17;
};

struct reg3sa2_format {
    unsigned int rd : 5;
    unsigned int rj : 5;
    unsigned int rk : 5;
    unsigned int immediate : 2;
    unsigned int opcode : 15;
};

union loongarch_instruction {
    unsigned int word;
    struct reg0i15_format   reg0i15_format;
    struct reg0i26_format   reg0i26_format;
    struct reg1i20_format   reg1i20_format;
    struct reg1i21_format   reg1i21_format;
    struct reg2_format      reg2_format;
    struct reg2i5_format    reg2i5_format;
    struct reg2i6_format    reg2i6_format;
    struct reg2i12_format   reg2i12_format;
    struct reg2i14_format   reg2i14_format;
    struct reg2i16_format   reg2i16_format;
    struct reg2bstrd_format reg2bstrd_format;
    struct reg2csr_format   reg2csr_format;
    struct reg3_format      reg3_format;
    struct reg3sa2_format   reg3sa2_format;
};

#endif /* __ASM_INST_H__ */
