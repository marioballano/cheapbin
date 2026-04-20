#include "binview.h"
#define USE_REGISTERS
#define USE_FAKE_DISASM
#include "re_data.h"

#define R2P_ENABLE_DLOPEN 0
#define R2P_ENABLE_SPAWN  1
#include "r2pipe.inc.c"

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct BinView {
    R2Pipe        *r2;

    const uint8_t *data;
    size_t         size;
    char           format[24];

    uint64_t       entry;
    uint64_t       text_addr;
    size_t         text_size;
    uint64_t       pc;

    /* register set selected for current arch */
    char           reg_names[BV_MAX_REGS][BV_REG_NAME];
    uint32_t       reg_cache[BV_MAX_REGS];
    int            num_regs;
    int            pc_idx;

    /* fallback animation state */
    int            fake_disasm_idx;
    uint64_t       fake_pc;
    uint32_t       fake_step;
};

/* ── Format detection from magic bytes (lifted from display.c) ─────── */

static const char *detect_format(const uint8_t *m, size_t sz)
{
    if (sz < 4) return "";
    if (m[0] == 0xCF && m[1] == 0xFA && m[2] == 0xED && m[3] == 0xFE) return "Mach-O 64";
    if (m[0] == 0xCE && m[1] == 0xFA && m[2] == 0xED && m[3] == 0xFE) return "Mach-O 32";
    if (m[0] == 0xCA && m[1] == 0xFE && m[2] == 0xBA && m[3] == 0xBE) return "Fat Mach-O";
    if (m[0] == 0xFE && m[1] == 0xED && m[2] == 0xFA && m[3] == 0xCF) return "Mach-O 64 BE";
    if (m[0] == 0x7F && m[1] == 'E'  && m[2] == 'L'  && m[3] == 'F')  return "ELF";
    if (m[0] == 'M'  && m[1] == 'Z')                                  return "PE/MZ";
    if (m[0] == 0x89 && m[1] == 'P'  && m[2] == 'N'  && m[3] == 'G')  return "PNG";
    if (m[0] == 0xFF && m[1] == 0xD8 && m[2] == 0xFF)                 return "JPEG";
    if (m[0] == 'G'  && m[1] == 'I'  && m[2] == 'F')                  return "GIF";
    if (m[0] == 'P'  && m[1] == 'K'  && m[2] == 0x03 && m[3] == 0x04) return "ZIP";
    if (m[0] == 0x1F && m[1] == 0x8B)                                 return "gzip";
    if (m[0] == 'B'  && m[1] == 'Z'  && m[2] == 'h')                  return "bzip2";
    if (m[0] == 0xFD && m[1] == '7'  && m[2] == 'z'  && m[3] == 'X')  return "XZ";
    if (m[0] == 0x25 && m[1] == 0x50 && m[2] == 0x44 && m[3] == 0x46) return "PDF";
    if (m[0] == 'd'  && m[1] == 'e'  && m[2] == 'x'  && m[3] == 0x0A) return "DEX";
    if (m[0] == 0x4D && m[1] == 0x53 && m[2] == 0x43 && m[3] == 0x46) return "CAB";
    if (m[0] == 0x52 && m[1] == 0x61 && m[2] == 0x72 && m[3] == 0x21) return "RAR";
    return "";
}

/* ── r2 helpers ─────────────────────────────────────────────────────── */

static char *r2_str(R2Pipe *r2, const char *cmd)
{
    char *out = r2p_cmd(r2, cmd);
    if (!out) return NULL;
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
        out[--n] = '\0';
    }
    return out;
}

static uint64_t r2_hex(R2Pipe *r2, const char *cmd)
{
    char *out = r2p_cmd(r2, cmd);
    if (!out) return 0;
    uint64_t v = strtoull(out, NULL, 0);
    free(out);
    return v;
}

static void r2_pick_regset(BinView *bv)
{
    /* Trust whatever r2 picked: query the actual arch/bits AND cross-check
     * against the PC register alias, since r2 5.x plugins go by names like
     * "a64" or "aarch64" that the simple substring test would miss. */
    char *arch = r2_str(bv->r2, "e asm.arch");
    char *bits = r2_str(bv->r2, "e asm.bits");
    char *pc   = r2_str(bv->r2, "arn pc");

    int is_arm = 0;
    if (arch && (strstr(arch, "arm")   ||
                 strstr(arch, "aarch") ||
                 strstr(arch, "a64")))   is_arm = 1;
    if (pc && (strcmp(pc, "pc") == 0))   is_arm = 1;  /* arm uses 'pc' */
    if (pc && (strcmp(pc, "rip") == 0 ||
               strcmp(pc, "eip") == 0))  is_arm = 0;  /* x86 uses rip/eip */

    int is_64 = bits && strcmp(bits, "64") == 0;
    if (pc && strcmp(pc, "rip") == 0) is_64 = 1;
    if (pc && strcmp(pc, "eip") == 0) is_64 = 0;

    free(arch);
    free(bits);
    free(pc);

    /* PC is always the last entry — caller relies on pc_idx = num_regs - 1. */
    static const char *X86_64[] = {
        "rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp",
        "r8","r9","r10","r11","r12","r13","r14","rip",
    };
    static const char *X86_32[] = {
        "eax","ebx","ecx","edx","esi","edi","ebp","esp",
        "eflags","cs","ds","es","fs","gs","ss","eip",
    };
    static const char *ARM_64[] = {
        "x0","x1","x2","x3","x4","x5","x6","x7",
        "x19","x20","x21","x22","x29","sp","lr","pc",
    };
    static const char *ARM_32[] = {
        "r0","r1","r2","r3","r4","r5","r6","r7",
        "r8","r9","r10","r11","r12","sp","lr","pc",
    };

    const char **set = is_arm ? (is_64 ? ARM_64 : ARM_32)
                              : (is_64 ? X86_64 : X86_32);
    bv->num_regs = 16;
    if (bv->num_regs > BV_MAX_REGS) bv->num_regs = BV_MAX_REGS;
    bv->pc_idx = bv->num_regs - 1;
    for (int i = 0; i < bv->num_regs; i++) {
        strncpy(bv->reg_names[i], set[i], BV_REG_NAME - 1);
        bv->reg_names[i][BV_REG_NAME - 1] = '\0';
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

BinView *binview_open(const char *path, const uint8_t *data, size_t size,
                      int use_r2)
{
    BinView *bv = (BinView *)calloc(1, sizeof(BinView));
    if (!bv) return NULL;

    bv->data    = data;
    bv->size    = size;
    bv->fake_pc = 0x401000;
    strncpy(bv->format, detect_format(data, size), sizeof(bv->format) - 1);

    if (!use_r2) {
        return bv;  /* forced fallback mode */
    }

    /* Don't die from broken pipes if r2 disappears mid-stream. */
    signal(SIGPIPE, SIG_IGN);

    /* -0: NUL framing for r2pipe protocol (required)
     * -z: skip string parsing for fast startup
     * -2: silence stderr noise from r2
     * Let r2 auto-detect arch/bits/slice from the binary itself. */
    bv->r2 = r2p_open(R2P_SPAWN, path,
        "-q0 -z -2 -e scr.color=0 -e scr.utf8=0 -e scr.interactive=false"
        " -e asm.addr=0 -e asm.bytes=0 -e asm.ucase=true");

    if (!bv->r2) {
        return bv;  /* fallback mode */
    }

    bv->entry = r2_hex(bv->r2, "?v entry0");
    if (bv->entry == 0) {
        /* file isn't recognised as an executable — drop r2 entirely */
        r2p_close(bv->r2);
        bv->r2 = NULL;
        return bv;
    }

    char *o = r2p_cmd(bv->r2, "s entry0");
    free(o);
    bv->text_addr = r2_hex(bv->r2, "?v $S");
    bv->text_size = (size_t)r2_hex(bv->r2, "?v $SS");
    if (bv->text_addr == 0) bv->text_addr = bv->entry;
    if (bv->text_size == 0) bv->text_size = 0x1000;

    o = r2p_cmd(bv->r2, "aei; aeim; aeip");
    free(o);
    bv->pc = bv->entry;

    r2_pick_regset(bv);
    return bv;
}

void binview_close(BinView *bv)
{
    if (!bv) return;
    if (bv->r2) r2p_close(bv->r2);
    free(bv);
}

int         binview_has_r2(const BinView *bv)   { return bv && bv->r2; }
const char *binview_format(const BinView *bv)   { return bv ? bv->format : ""; }
uint64_t    binview_entry(const BinView *bv)    { return bv ? bv->entry : 0; }
uint64_t    binview_text_addr(const BinView *bv){ return bv ? bv->text_addr : 0; }
size_t      binview_text_size(const BinView *bv){ return bv ? bv->text_size : 0; }
uint64_t    binview_pc(const BinView *bv)       { return bv ? bv->pc : 0; }

size_t binview_file_bytes(const BinView *bv, size_t offset, uint8_t *out, size_t n)
{
    if (!bv || !bv->data || !out || n == 0 || offset >= bv->size) return 0;
    size_t avail = bv->size - offset;
    if (n > avail) n = avail;
    memcpy(out, bv->data + offset, n);
    return n;
}

size_t binview_read(BinView *bv, uint64_t addr, uint8_t *out, size_t n)
{
    if (!bv || !out || n == 0) return 0;
    if (bv->r2) {
        char cmd[80];
        snprintf(cmd, sizeof(cmd), "p8 %zu @ 0x%llx", n, (unsigned long long)addr);
        char *resp = r2p_cmd(bv->r2, cmd);
        if (!resp) return 0;
        size_t got = 0;
        for (const char *p = resp; *p && got < n; ) {
            while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
            if (!p[0] || !p[1]) break;
            if (!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) break;
            unsigned int b;
            if (sscanf(p, "%2x", &b) != 1) break;
            out[got++] = (uint8_t)b;
            p += 2;
        }
        free(resp);
        return got;
    }
    if (bv->size == 0) return 0;
    size_t off = (size_t)(addr % bv->size);
    size_t avail = bv->size - off;
    if (n > avail) n = avail;
    memcpy(out, bv->data + off, n);
    return n;
}

void binview_step(BinView *bv, uint64_t pc_addr)
{
    if (!bv) return;
    if (!bv->r2) {
        bv->fake_step++;
        bv->fake_pc += (bv->fake_step & 7) + 1;
        bv->fake_disasm_idx = (bv->fake_disasm_idx + 1) % (int)NUM_FAKE_DISASM;
        return;
    }

    /* Anchor PC at the caller-supplied address before stepping — without
     * this, `aes` keeps walking from wherever ESIL ended up last frame and
     * usually wedges in invalid memory after a few hops, freezing the
     * regs/disasm. Fall back to entry for an empty/out-of-range hint. */
    if (pc_addr == 0) pc_addr = bv->pc ? bv->pc : bv->entry;
    if (bv->text_size > 0 &&
        (pc_addr < bv->text_addr || pc_addr >= bv->text_addr + bv->text_size)) {
        pc_addr = bv->entry;
    }

    /* Batch PC set + step + reg dump in a single round-trip. r2pipe writes
     * the whole command on one line, so subcommands MUST be ';'-chained:
     * embedded '\n' would terminate the protocol mid-batch and only the
     * first response would come back. `aer <name>` prints just the value. */
    char cmd[1024];
    int force = (bv->fake_step % 32) == 0;
    int off = force? snprintf(cmd, sizeof(cmd), "aepc 0x%llx;aes",
                        (unsigned long long)pc_addr): snprintf (cmd, sizeof (cmd), "aes");
    for (int i = 0; i < bv->num_regs; i++) {
        off += snprintf(cmd + off, sizeof(cmd) - off,
                        ";ar?%s", bv->reg_names[i]);
    }
    char *resp = r2p_cmd(bv->r2, cmd);
    if (!resp) return;

    char *p = resp;
    uint32_t lv = (uint32_t)(size_t)p;
    for (int i = 0; i < bv->num_regs && *p; i++) {
        uint64_t v = strtoull(p, NULL, 0);
        if (i == bv->pc_idx) bv->pc = v;
	uint32_t rv = (uint32_t)(v & 0xFFFFFFFFu);
	if ((rv & 0xffff) < 0xffff) {
		lv = rv;
	} else {
		rv = lv;
	}
	if (!rv) {
		rv = (lv + i) ^ (pc_addr << i /2);
	}
        bv->reg_cache[i] = rv;
        char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    free(resp);
}

int binview_regs(BinView *bv, BvReg *out, int max)
{
    if (!bv || !out || max <= 0) return 0;
    if (bv->r2) {
        int n = bv->num_regs < max ? bv->num_regs : max;
        for (int i = 0; i < n; i++) {
            strncpy(out[i].name, bv->reg_names[i], BV_REG_NAME - 1);
            out[i].name[BV_REG_NAME - 1] = '\0';
            out[i].value = bv->reg_cache[i];
        }
        return n;
    }

    /* Fallback: spin through the bundled register table; values driven by
     * file bytes so they vary per-file but stay deterministic per-step. */
    int n = 8 < max ? 8 : max;
    for (int i = 0; i < n; i++) {
        const char *nm = REGISTERS[(bv->fake_step / 16 + (uint32_t)i * 3)
                                   % NUM_REGISTERS];
        strncpy(out[i].name, nm, BV_REG_NAME - 1);
        out[i].name[BV_REG_NAME - 1] = '\0';
        uint32_t v = bv->fake_step * 0x9E3779B1u + (uint32_t)i * 0x12345u;
        if (bv->data && bv->size > 0) {
            v ^= bv->data[(bv->fake_step + (uint32_t)i * 7) % bv->size];
            v ^= ((uint32_t)bv->data[(bv->fake_step * 3 + (uint32_t)i)
                                     % bv->size]) << 8;
        }
        out[i].value = v;
    }
    return n;
}

int binview_disasm(BinView *bv, uint64_t addr, int count,
                   uint64_t *addrs_out, char lines[][BV_LINE_LEN])
{
    if (!bv || !lines || count <= 0) return 0;
    if (count > BV_MAX_LINES) count = BV_MAX_LINES;

    if (bv->r2) {
        char cmd[80];
        snprintf(cmd, sizeof(cmd), "pi %d @ 0x%llx",
                 count, (unsigned long long)addr);
        char *resp = r2p_cmd(bv->r2, cmd);
        if (!resp) return 0;

        int n = 0;
        char *p = resp;
        uint64_t cur = addr;
        while (n < count && *p) {
            char *eol = strchr(p, '\n');
            if (eol) *eol = '\0';
            char *s = p;
            while (*s == ' ' || *s == '\t') s++;
            if (*s) {
                strncpy(lines[n], s, BV_LINE_LEN - 1);
                lines[n][BV_LINE_LEN - 1] = '\0';
                for (char *c = lines[n]; *c; c++)
                    *c = (char)toupper((unsigned char)*c);
                if (addrs_out) addrs_out[n] = cur;
                /* approximate: addresses just monotonically count up by 4
                 * — visual ticker only, real PC is shown separately */
                cur += 4;
                n++;
            }
            if (!eol) break;
            p = eol + 1;
        }
        free(resp);
        return n;
    }

    /* Fallback: cycle through bundled fake disasm. */
    int n = 0;
    for (int i = 0; i < count; i++) {
        const char *fake = FAKE_DISASM[(bv->fake_disasm_idx + i)
                                       % NUM_FAKE_DISASM];
        strncpy(lines[i], fake, BV_LINE_LEN - 1);
        lines[i][BV_LINE_LEN - 1] = '\0';
        if (addrs_out) addrs_out[i] = bv->fake_pc + (uint64_t)i * 4;
        n++;
    }
    return n;
}
