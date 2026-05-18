#include "sdl_display.h"
#include "font8x8.h"
#include "sdl_include.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

/* ── Module state ──────────────────────────────────────────────────── */

static SDL_Window   *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture  *s_font_tex;
static SDL_GameController *s_controller;
static SDL_Joystick       *s_joystick;
static SDL_JoystickID      s_controller_id = -1;
static SDL_JoystickID      s_joystick_id = -1;
static bool                s_gamepad_ready;
static int                 s_ctrl_x_dir;
static int                 s_ctrl_y_dir;
static int                 s_joy_x_dir;
static int                 s_joy_y_dir;

static char     s_filename[256];
static size_t   s_filesize;
static BinView *s_bv;
static int      s_frame;
static uint32_t s_rng = 0x12345678;

#define KEY_QUEUE_SIZE 64
static int s_keys[KEY_QUEUE_SIZE];
static int s_kq_head, s_kq_tail;

static const char *NOTE_NAMES_S[12] = {
    "C ","C#","D ","D#","E ","F ","F#","G ","G#","A ","A#","B "
};

static const char *CH_NAMES_S[6] = {
    "LEAD ", "HARM ", "BASS ", "ARP  ", "PAD  ", "DRUM "
};

/* ── PRNG ──────────────────────────────────────────────────────────── */

static uint32_t rng_next(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ── Key queue ─────────────────────────────────────────────────────── */

static void key_push(int k)
{
    int next = (s_kq_tail + 1) % KEY_QUEUE_SIZE;
    if (next == s_kq_head) return;  /* full → drop */
    s_keys[s_kq_tail] = k;
    s_kq_tail = next;
}

static int key_pop(void)
{
    if (s_kq_head == s_kq_tail) return 0;
    int k = s_keys[s_kq_head];
    s_kq_head = (s_kq_head + 1) % KEY_QUEUE_SIZE;
    return k;
}

/* -- Gamepad input --------------------------------------------------- */

static int axis_dir(Sint16 value)
{
    const Sint16 deadzone = 16000;
    if (value < -deadzone) return -1;
    if (value >  deadzone) return  1;
    return 0;
}

static void push_axis_motion(Sint16 value, int *state, int neg_key, int pos_key)
{
    int dir = axis_dir(value);
    if (dir == *state) return;
    *state = dir;
    if (dir < 0) {
        key_push(neg_key);
    } else if (dir > 0) {
        key_push(pos_key);
    }
}

static void gamepad_close(void)
{
    if (s_controller) {
        SDL_GameControllerClose(s_controller);
        s_controller = NULL;
    }
    if (s_joystick) {
        SDL_JoystickClose(s_joystick);
        s_joystick = NULL;
    }
    s_controller_id = -1;
    s_joystick_id = -1;
    s_ctrl_x_dir = s_ctrl_y_dir = 0;
    s_joy_x_dir = s_joy_y_dir = 0;
}

static bool gamepad_open_device(int index)
{
    SDL_Joystick *joy;

    if (!s_gamepad_ready || s_controller || s_joystick || index < 0) {
        return false;
    }

    if (SDL_IsGameController(index)) {
        s_controller = SDL_GameControllerOpen(index);
        if (!s_controller) {
            return false;
        }
        joy = SDL_GameControllerGetJoystick(s_controller);
        s_controller_id = joy ? SDL_JoystickInstanceID(joy) : -1;
        return true;
    }

    s_joystick = SDL_JoystickOpen(index);
    if (!s_joystick) {
        return false;
    }
    s_joystick_id = SDL_JoystickInstanceID(s_joystick);
    return true;
}

static void gamepad_open_first(void)
{
    int n;

    if (!s_gamepad_ready || s_controller || s_joystick) {
        return;
    }

    n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (SDL_IsGameController(i) && gamepad_open_device(i)) {
            return;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!SDL_IsGameController(i) && gamepad_open_device(i)) {
            return;
        }
    }
}

static void gamepad_init(void)
{
    Uint32 flags = SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK;

    s_gamepad_ready = false;
    if (SDL_InitSubSystem(flags) != 0) {
        fprintf(stderr, "warning: SDL_InitSubSystem(GAMECONTROLLER): %s\n",
                SDL_GetError());
        return;
    }

    s_gamepad_ready = true;
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    gamepad_open_first();
}

static int translate_controller_button(Uint8 button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
    case SDL_CONTROLLER_BUTTON_START:
        return ' ';
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_BACK:
    case SDL_CONTROLLER_BUTTON_GUIDE:
        return 'q';
    case SDL_CONTROLLER_BUTTON_X:
        return 'c';
    case SDL_CONTROLLER_BUTTON_Y:
        return 's';
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return 'C';
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return 'S';
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return KEY_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return KEY_RIGHT;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return 'K';
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return 'k';
    default:
        return 0;
    }
}

static int translate_joystick_button(Uint8 button)
{
    switch (button) {
    case 0:  return ' ';       /* A */
    case 1:  return 'q';       /* B */
    case 2:  return 'c';       /* X */
    case 3:  return 's';       /* Y */
    case 4:  return 'C';       /* L */
    case 5:  return 'S';       /* R */
    case 6:  return 'q';       /* Select / Back */
    case 7:  return ' ';       /* Start */
    case 11: return 'K';       /* Common Linux D-pad up */
    case 12: return 'k';       /* Common Linux D-pad down */
    case 13: return KEY_LEFT;  /* Common Linux D-pad left */
    case 14: return KEY_RIGHT; /* Common Linux D-pad right */
    default: return 0;
    }
}

static void push_hat_motion(Uint8 value)
{
    if (value & SDL_HAT_LEFT)  key_push(KEY_LEFT);
    if (value & SDL_HAT_RIGHT) key_push(KEY_RIGHT);
    if (value & SDL_HAT_UP)    key_push('K');
    if (value & SDL_HAT_DOWN)  key_push('k');
}

/* ── Font texture ──────────────────────────────────────────────────── */

static SDL_Texture *build_font_texture(SDL_Renderer *r)
{
    const int W = 128 * 8;
    const int H = 8;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(
        0, W, H, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) return NULL;

    SDL_LockSurface(surf);
    uint32_t *px = (uint32_t *)surf->pixels;
    int pitch = surf->pitch / 4;
    for (int ch = 0; ch < 128; ch++) {
        for (int y = 0; y < 8; y++) {
            uint8_t row = FONT8X8[ch][y];
            for (int x = 0; x < 8; x++) {
                int bit = (row >> x) & 1;
                px[y * pitch + ch * 8 + x] = bit ? 0xFFFFFFFFu : 0x00000000u;
            }
        }
    }
    SDL_UnlockSurface(surf);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

/* ── Drawing primitives ────────────────────────────────────────────── */

static void set_color(int r, int g, int b)
{
    SDL_SetRenderDrawColor(s_renderer, (Uint8)r, (Uint8)g, (Uint8)b, 255);
}

static void fill_pixels(int x, int y, int w, int h, int r, int g, int b)
{
    SDL_Rect rect = { x, y, w, h };
    set_color(r, g, b);
    SDL_RenderFillRect(s_renderer, &rect);
}

static void draw_char(int col, int row, int ch, int r, int g, int b)
{
    if (ch < 0 || ch > 127) ch = '?';
    SDL_Rect src = { ch * 8, 0, 8, 8 };
    SDL_Rect dst = { col * SDL_CELL_W, row * SDL_CELL_H, SDL_CELL_W, SDL_CELL_H };
    SDL_SetTextureColorMod(s_font_tex, (Uint8)r, (Uint8)g, (Uint8)b);
    SDL_RenderCopy(s_renderer, s_font_tex, &src, &dst);
}

static int draw_text(int col, int row, const char *s, int r, int g, int b)
{
    int c = col;
    while (*s && c < SDL_COLS) {
        draw_char(c++, row, (unsigned char)*s++, r, g, b);
    }
    return c;
}

__attribute__((format(printf, 4, 5)))
static int draw_textf(int col, int row, int rgb, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int rr = (rgb >> 16) & 0xFF;
    int gg = (rgb >>  8) & 0xFF;
    int bb =  rgb        & 0xFF;
    return draw_text(col, row, buf, rr, gg, bb);
}

/* ── UI sections ───────────────────────────────────────────────────── */

static void draw_header(const SynthState *s)
{
    /* Top: thin neon-cyan strip */
    fill_pixels(0, 0, SDL_WIN_W, 2, 0, 200, 180);

    int row = 0;
    /* spinner / title */
    const char *spin = "|/-\\";
    int sp = spin[s_frame & 3];

    int c = 0;
    c = draw_textf(c, row, 0xFF0080, "%c ", sp);
    c = draw_text(c, row, "CHEAP", 0, 220, 220);
    c = draw_text(c, row, "BIN",   255, 100,   0);
    c++;
    c = draw_textf(c, row, 0xC8A0FF, "%s", chip_short_name(s->chip_type));
    c++;
    if (s->style_type != STYLE_NONE) {
        c = draw_textf(c, row, 0xFFC864, "%s", style_short_name(s->style_type));
        c++;
    }
    c = draw_textf(c, row, 0xB4F0C8, "%s", scale_short_name(s->scale_type));

    /* right-aligned: section + bpm */
    char info[24];
    snprintf(info, sizeof(info), "[%s] %3.0f", s->section_name, (double)s->bpm);
    int info_len = (int)strlen(info);
    int right = SDL_COLS - info_len - 1;
    if (right < c + 1) right = c + 1;
    draw_text(right, row, info, 255, 200, 0);

    /* bottom rule */
    fill_pixels(0, SDL_CELL_H * 2 - 2, SDL_WIN_W, 1, 0, 100, 90);
}

static void draw_file_info(const SynthState *s)
{
    int row = 2;
    draw_text(0, row,  "FILE", 100, 100, 120);
    draw_text(5, row,  s_filename, 200, 200, 255);

    row = 3;
    char sz[32];
    if (s_filesize >= 1048576)
        snprintf(sz, sizeof(sz), "%.1fM", (double)s_filesize / 1048576.0);
    else if (s_filesize >= 1024)
        snprintf(sz, sizeof(sz), "%.1fK", (double)s_filesize / 1024.0);
    else
        snprintf(sz, sizeof(sz), "%zuB", s_filesize);

    draw_text(0, row, "SIZE", 100, 100, 120);
    draw_text(5, row, sz, 200, 200, 255);

    int n = s->current_note;
    if (n > 0 && n < 128) {
        draw_text(13, row, "NOTE", 100, 100, 120);
        char nb[8];
        snprintf(nb, sizeof(nb), "%s%d", NOTE_NAMES_S[n % 12], n / 12 - 1);
        draw_text(18, row, nb, 0, 255, 255);
    }

    /* magic + format */
    uint8_t magic[4] = { 0 };
    size_t  mn = binview_file_bytes(s_bv, 0, magic, 4);
    if (mn >= 4) {
        char m[16];
        snprintf(m, sizeof(m), "%02X%02X%02X%02X",
                 magic[0], magic[1], magic[2], magic[3]);
        draw_text(24, row, m, 100, 140, 120);
        const char *fmt = binview_format(s_bv);
        if (fmt && *fmt) {
            char fb[12];
            snprintf(fb, sizeof(fb), "[%.4s]", fmt);
            draw_text(33, row, fb, 0, 200, 150);
        }
    }
    if (binview_has_r2(s_bv)) {
        draw_text(38, row, "r2", 255, 100, 0);
    }
}

static void draw_meters(const SynthState *s)
{
    static const int HI[6][3] = {
        { 0,255,255}, {100,100,255}, {  0,255,100},
        {255,  0,255}, {255,255,  0}, {255, 50, 50},
    };
    static const int LO[6][3] = {
        {  0, 80,120}, { 30, 30,120}, {  0,100,  0},
        {100,  0,100}, {120,100,  0}, {120,  0,  0},
    };

    int top_row = 5;
    int bar_left_col = 6;          /* after the label */
    int bar_width    = SDL_COLS - bar_left_col - 1;

    for (int ch = 0; ch < 6; ch++) {
        int row = top_row + ch;
        draw_text(0, row, CH_NAMES_S[ch], HI[ch][0], HI[ch][1], HI[ch][2]);

        float lv = s->ch_levels[ch];
        if (lv > 1.0f) lv = 1.0f;

        /* draw the bar as a real filled gradient rectangle */
        int px = bar_left_col * SDL_CELL_W;
        int py = row * SDL_CELL_H + 2;
        int ph = SDL_CELL_H - 4;
        int total_pw = bar_width * SDL_CELL_W;
        int filled_pw = (int)(lv * (float)total_pw);

        /* background trough */
        fill_pixels(px, py, total_pw, ph, 18, 18, 28);

        /* gradient fill */
        for (int x = 0; x < filled_pw; x++) {
            float t = (float)x / (float)total_pw;
            int r = LO[ch][0] + (int)(t * (float)(HI[ch][0] - LO[ch][0]));
            int g = LO[ch][1] + (int)(t * (float)(HI[ch][1] - LO[ch][1]));
            int b = LO[ch][2] + (int)(t * (float)(HI[ch][2] - LO[ch][2]));
            fill_pixels(px + x, py, 1, ph, r, g, b);
        }
    }
}

/* Polyline scope drawn as actual SDL line segments — that's the SDL
   port of the ASCII-art waveform from theme_default.c::draw_scope. */
static void draw_scope(const SynthState *s)
{
    int label_row  = 11;
    int top_row    = 12;
    int rows_tall  = 5;

    draw_text(0, label_row, "SCOPE", 100, 200, 200);
    char addr[12];
    snprintf(addr, sizeof(addr), "0x%04X", (s_frame * 0x40) & 0xFFFF);
    draw_text(35, label_row, addr, 50, 50, 70);

    int x0 = 0;
    int y0 = top_row * SDL_CELL_H;
    int W  = SDL_WIN_W;
    int H  = rows_tall * SDL_CELL_H;

    /* dim background grid */
    fill_pixels(x0, y0, W, H, 8, 12, 16);
    set_color(20, 30, 30);
    for (int g = 0; g < 8; g++) {
        int gx = x0 + (W * g) / 8;
        SDL_RenderDrawLine(s_renderer, gx, y0, gx, y0 + H - 1);
    }
    for (int g = 0; g < 4; g++) {
        int gy = y0 + (H * g) / 4;
        SDL_RenderDrawLine(s_renderer, x0, gy, x0 + W - 1, gy);
    }

    /* Main wave: same recipe as theme_default.c. */
    float ph = s->paused ? 0.0f : (float)s_frame * 0.08f;
    float f1 = 2.0f + s->ch_levels[0] * 8.0f;     /* CH_LEAD */
    float f2 = 1.0f + s->ch_levels[2] * 4.0f;     /* CH_BASS */
    float f3 = 3.0f + s->ch_levels[3] * 6.0f;     /* CH_ARP  */
    float dr = s->ch_levels[5];                   /* CH_DRUMS */

    set_color(0, 230, 170);
    int prev_x = x0, prev_y = y0 + H / 2;
    for (int x = 0; x < W; x++) {
        float t = (float)x / (float)W;
        float wave;
        if (s->paused) {
            wave = 0.5f;
        } else {
            wave = 0.5f
                + 0.22f * sinf(t * f1 * 6.2832f + ph)
                + 0.13f * sinf(t * f2 * 6.2832f + ph * 0.7f)
                + 0.08f * sinf(t * f3 * 6.2832f + ph * 1.3f)
                + dr    * 0.15f * sinf(t * 2.0f * 6.2832f + ph * 2.0f);
        }
        if (wave < 0.0f) wave = 0.0f;
        if (wave > 1.0f) wave = 1.0f;
        int yy = y0 + (int)((1.0f - wave) * (float)(H - 1));
        SDL_RenderDrawLine(s_renderer, prev_x, prev_y, x0 + x, yy);
        prev_x = x0 + x;
        prev_y = yy;
    }

    /* faint mid-line */
    set_color(40, 80, 60);
    SDL_RenderDrawLine(s_renderer, x0, y0 + H / 2, x0 + W - 1, y0 + H / 2);
}

static void draw_hex_and_disasm(const SynthState *s)
{
    int label_row  = 17;
    int data_row0  = 18;
    int rows_tall  = 6;
    const int dis_col = 22;

    draw_text(0, label_row,  "HEX",    140, 140, 180);
    draw_text(dis_col, label_row, "DISASM", 140, 140, 180);

    BinView *bv = s_bv;
    enum { BPR = 6, NROWS = 6, HEX_COL = 6, HEX_STRIDE = 2 };
    const int total = BPR * NROWS;

    uint64_t base;
    if (binview_has_r2(bv)) {
        uint64_t text = binview_text_addr(bv);
        size_t   tsz  = binview_text_size(bv);
        if (tsz < (size_t)total) tsz = (size_t)total;
        uint64_t span = (uint64_t)(tsz - (size_t)total);
        base = text + (uint64_t)(s->progress * (float)span);
    } else {
        size_t fsz = s_filesize > (size_t)total ? s_filesize - (size_t)total : 0;
        base = (uint64_t)(s->progress * (float)fsz);
    }
    base -= base % (uint64_t)BPR;

    uint8_t hbuf[NROWS * BPR];
    size_t  got = binview_read(bv, base, hbuf, (size_t)total);

    int hex_rows = rows_tall < NROWS ? rows_tall : NROWS;
    for (int r = 0; r < hex_rows; r++) {
        int row = data_row0 + r;
        char addr[10];
        snprintf(addr, sizeof(addr), "%05llX",
                 (unsigned long long)((base + (uint64_t)(r * BPR)) & 0xFFFFFu));
        draw_text(0, row, addr, 80, 80, 110);

        for (int b = 0; b < BPR; b++) {
            size_t idx = (size_t)(r * BPR + b);
            int col = HEX_COL + b * HEX_STRIDE;
            if (idx < got) {
                uint8_t v = hbuf[idx];
                int rr, gg, bb;
                if (v == 0)         { rr = 50;  gg = 50;  bb = 60;  }
                else if (v == 0xFF) { rr = 255; gg = 100; bb = 100; }
                else if (v >= 0x20 && v < 0x7F) { rr = 0;   gg = 200; bb = 150; }
                else                { rr = 100; gg = 150; bb = 200; }
                char hb[4];
                snprintf(hb, sizeof(hb), "%02X", v);
                draw_text(col, row, hb, rr, gg, bb);
            } else {
                draw_text(col, row, "..", 40, 40, 50);
            }
        }
    }

    /* DISASM, right column. Don't step ESIL while paused — that's what
       advances both the disassembly window and the regs panel. */
    if (!s->paused && s_frame % 4 == 0) {
        uint64_t music_pc = 0;
        if (binview_has_r2(bv)) {
            uint64_t text = binview_text_addr(bv);
            size_t   tsz  = binview_text_size(bv);
            uint64_t span = tsz > 16 ? (uint64_t)(tsz - 16) : 0;
            music_pc = text + (uint64_t)(s->progress * (float)span);
        }
        binview_step(bv, music_pc);
    }

    char     lines[6][BV_LINE_LEN];
    uint64_t addrs[6];
    uint64_t pcb = binview_pc(bv);
    if (pcb == 0) pcb = binview_text_addr(bv);
    int n = binview_disasm(bv, pcb, 6, addrs, lines);

    int dis_w   = SDL_COLS - dis_col;
    for (int ln = 0; ln < 6; ln++) {
        int row = data_row0 + ln;
        char buf[24];
        if (ln < n) {
            snprintf(buf, sizeof(buf), "%04X ",
                     (unsigned)(addrs[ln] & 0xFFFFu));
            draw_text(dis_col, row, buf, 80, 80, 100);

            int rr, gg, bb;
            if (ln == 0)      { rr = 0;   gg = 255; bb = 180; }
            else if (ln < 3)  { rr = 0;   gg = 150; bb = 100; }
            else              { rr = 50;  gg =  90; bb =  70; }

            if (ln == 0) draw_char(dis_col + 5, row, 0x17, rr, gg, bb);
            int max = dis_w - 7;
            if (max > (int)sizeof(buf) - 1) max = (int)sizeof(buf) - 1;
            snprintf(buf, sizeof(buf), "%-.*s", max, lines[ln]);
            draw_text(dis_col + 6, row, buf, rr, gg, bb);
        }
    }
}

static void draw_regs(const SynthState *s)
{
    (void)s;
    int row = 25;
    draw_text(0, row, "REGS", 140, 140, 180);

    BvReg regs[BV_MAX_REGS];
    int   n = binview_regs(s_bv, regs, BV_MAX_REGS);

    int col = 5;
    for (int i = 0; i < n && i < 4; i++) {
        char up[BV_REG_NAME];
        size_t nl = strlen(regs[i].name);
        if (nl >= sizeof(up)) nl = sizeof(up) - 1;
        for (size_t k = 0; k < nl; k++)
            up[k] = (char)toupper((unsigned char)regs[i].name[k]);
        up[nl] = '\0';

        char rb[16];
        snprintf(rb, sizeof(rb), "%s=%04X",
                 up, (unsigned)(regs[i].value & 0xFFFFu));
        draw_text(col, row, rb, 0, 180, 130);
        col += (int)strlen(rb) + 1;
        if (col >= SDL_COLS - 8) break;
    }
}

static void draw_progress(const SynthState *s)
{
    int row = 27;
    int pct = (int)(s->progress * 100.0f);
    if (pct > 100) pct = 100;

    int x = 0;
    int y = row * SDL_CELL_H + 4;
    int w = (SDL_COLS - 5) * SDL_CELL_W;
    int h = SDL_CELL_H - 8;
    int filled = (int)(s->progress * (float)w);

    fill_pixels(x, y, w, h, 30, 30, 45);
    for (int i = 0; i < filled; i++) {
        float t = (float)i / (float)w;
        int r = (int)(180.0f * (1.0f - t));
        int g = (int)( 50.0f + 205.0f * t);
        int b = (int)(200.0f +  55.0f * t);
        if (b > 255) b = 255;
        fill_pixels(x + i, y, 1, h, r, g, b);
    }

    char pb[16];
    snprintf(pb, sizeof(pb), "%3d%%", pct);
    draw_text(SDL_COLS - 4, row, pb, 200, 200, 220);
}

static void draw_status(const SynthState *s)
{
    int row = 28;
    if (s->paused) {
        draw_text(0, row, "PAUSE", 255, 200, 0);
    } else if (s->finished) {
        draw_text(0, row, "DONE ", 0, 255, 100);
    } else {
        draw_text(0, row, "PLAY ", 0, 255, 100);
    }

    row = 29;
    draw_text(0, row, "spc/A dpad x/y l/r b", 70, 70, 100);
}

/* ── Public API ────────────────────────────────────────────────────── */

int sdl_display_init(const char *filename, size_t filesize, BinView *bv)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "error: SDL_InitSubSystem(VIDEO): %s\n", SDL_GetError());
        return -1;
    }

    s_window = SDL_CreateWindow("cheapbin",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                SDL_WIN_W, SDL_WIN_H,
                                SDL_WINDOW_SHOWN);
    if (!s_window) {
        fprintf(stderr, "error: SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1,
                                    SDL_RENDERER_ACCELERATED |
                                    SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer) {
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!s_renderer) {
        fprintf(stderr, "error: SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);

    s_font_tex = build_font_texture(s_renderer);
    if (!s_font_tex) {
        fprintf(stderr, "error: build_font_texture: %s\n", SDL_GetError());
        return -1;
    }

    strncpy(s_filename, filename, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = '\0';
    s_filesize = filesize;
    s_bv       = bv;
    s_frame    = 0;
    s_rng      = (uint32_t)(filesize ^ 0xDEADBEEFu);
    s_kq_head  = s_kq_tail = 0;
    gamepad_init();
    return 0;
}

void sdl_display_cleanup(void)
{
    gamepad_close();
    if (s_gamepad_ready) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
        s_gamepad_ready = false;
    }
    if (s_font_tex) { SDL_DestroyTexture(s_font_tex); s_font_tex = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);    s_window   = NULL; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void sdl_display_update(const SynthState *s)
{
    s_frame++;
    (void)rng_next();

    set_color(8, 10, 14);
    SDL_RenderClear(s_renderer);

    draw_header(s);
    draw_file_info(s);
    draw_meters(s);
    draw_scope(s);
    draw_hex_and_disasm(s);
    draw_regs(s);
    draw_progress(s);
    draw_status(s);

    SDL_RenderPresent(s_renderer);
}

static int translate_keysym(SDL_Keysym k)
{
    SDL_Keymod mods = SDL_GetModState();
    bool shift = (mods & KMOD_SHIFT) != 0;

    switch (k.sym) {
    case SDLK_UP:    return KEY_UP;
    case SDLK_DOWN:  return KEY_DOWN;
    case SDLK_LEFT:  return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_SPACE: return ' ';
    case SDLK_ESCAPE:
    case SDLK_q:     return shift ? 'Q' : 'q';
    case SDLK_c:     return shift ? 'C' : 'c';
    case SDLK_s:     return shift ? 'S' : 's';
    case SDLK_k:     return shift ? 'K' : 'k';
    case SDLK_t:     return shift ? 'T' : 't';
    case SDLK_h:     return shift ? 'H' : 'h';
    case SDLK_l:     return shift ? 'L' : 'l';
    default:         return 0;
    }
}

int sdl_display_poll_key(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            key_push(KEY_QUIT);
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!s_controller && !s_joystick) {
                gamepad_open_device(ev.cdevice.which);
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (ev.cdevice.which == s_controller_id) {
                gamepad_close();
                gamepad_open_first();
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN: {
            int k = translate_controller_button(ev.cbutton.button);
            if (k) key_push(k);
            break;
        }
        case SDL_CONTROLLERAXISMOTION:
            if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                push_axis_motion(ev.caxis.value, &s_ctrl_x_dir,
                                 KEY_LEFT, KEY_RIGHT);
            } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                push_axis_motion(ev.caxis.value, &s_ctrl_y_dir, 'K', 'k');
            }
            break;
        case SDL_JOYDEVICEADDED:
            if (!s_controller && !s_joystick) {
                gamepad_open_device(ev.jdevice.which);
            }
            break;
        case SDL_JOYDEVICEREMOVED:
            if (ev.jdevice.which == s_joystick_id) {
                gamepad_close();
                gamepad_open_first();
            }
            break;
        case SDL_JOYBUTTONDOWN:
            if (ev.jbutton.which == s_joystick_id) {
                int k = translate_joystick_button(ev.jbutton.button);
                if (k) key_push(k);
            }
            break;
        case SDL_JOYHATMOTION:
            if (ev.jhat.which == s_joystick_id) {
                push_hat_motion(ev.jhat.value);
            }
            break;
        case SDL_JOYAXISMOTION:
            if (ev.jaxis.which == s_joystick_id) {
                if (ev.jaxis.axis == 0) {
                    push_axis_motion(ev.jaxis.value, &s_joy_x_dir,
                                     KEY_LEFT, KEY_RIGHT);
                } else if (ev.jaxis.axis == 1) {
                    push_axis_motion(ev.jaxis.value, &s_joy_y_dir, 'K', 'k');
                }
            }
            break;
        case SDL_KEYDOWN: {
            int k = translate_keysym(ev.key.keysym);
            if (k) key_push(k);
            break;
        }
        default:
            break;
        }
    }
    return key_pop();
}
