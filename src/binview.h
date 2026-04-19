#ifndef CHEAPBIN_BINVIEW_H
#define CHEAPBIN_BINVIEW_H

/*
 * binview — thin abstraction over file/binary inspection.
 *
 *   Tries to spawn radare2 (-z) for real disassembly, hex bytes from
 *   the entrypoint exec section, and ESIL-stepped register snapshots.
 *   Falls back to magic-byte format detection plus the bundled fake
 *   opcode/register tables when r2 is not in PATH.
 *
 * The rest of cheapbin should not know whether r2 is active.
 */

#include <stddef.h>
#include <stdint.h>

#define BV_MAX_REGS  16
#define BV_REG_NAME  8
#define BV_LINE_LEN  64
#define BV_MAX_LINES 32

typedef struct {
    char     name[BV_REG_NAME];
    uint32_t value;     /* truncated to 32 bits — visual only */
} BvReg;

typedef struct BinView BinView;

BinView    *binview_open(const char *path, const uint8_t *data, size_t size,
                         int use_r2);
void        binview_close(BinView *bv);

int         binview_has_r2(const BinView *bv);
const char *binview_format(const BinView *bv);
uint64_t    binview_entry(const BinView *bv);
uint64_t    binview_text_addr(const BinView *bv);
size_t      binview_text_size(const BinView *bv);
uint64_t    binview_pc(const BinView *bv);

/* Read up to n bytes at addr. With r2: virtual address. Without r2: file
 * offset wrapped into the buffer. Returns bytes actually written. */
size_t      binview_read(BinView *bv, uint64_t addr, uint8_t *out, size_t n);

/* Read up to n bytes from file offset (raw data, never via r2 virtual address).
 * Safe to call for header/magic bytes regardless of r2 mode. */
size_t      binview_file_bytes(const BinView *bv, size_t offset, uint8_t *out, size_t n);

/* Step one ESIL instruction and refresh the cached register snapshot.
 * pc_addr anchors the program counter before the step so emulation tracks
 * a chosen address instead of drifting into unreachable code; pass 0 to
 * keep whatever PC ESIL had.
 * Without r2: advances synthetic counters used by binview_regs/disasm. */
void        binview_step(BinView *bv, uint64_t pc_addr);

/* Copy up to max register snapshots into out; returns count actually filled. */
int         binview_regs(BinView *bv, BvReg *out, int max);

/* Disassemble up to count instructions starting at addr. Each line is a
 * NUL-terminated string of at most BV_LINE_LEN-1 chars. addrs_out (optional)
 * receives the start address of each line. Returns lines filled. */
int         binview_disasm(BinView *bv, uint64_t addr, int count,
                           uint64_t *addrs_out,
                           char lines[][BV_LINE_LEN]);

#endif
