/* ============================================================================
 *  Nuklear Studio
 *  ---------------------------------------------------------------------------
 *  A redesigned interface for the "Nuklear + Win32 GDI" sample.
 *
 *  What changed compared to the original 77-line main.c
 *  ---------------------------------------------------
 *   - Full-window layout: header bar, sidebar, tab strip, code editor,
 *     console panel and status bar (no floating draggable window).
 *   - Custom theme engine with 4 hand-tuned palettes (2 dark / 2 light)
 *     plus per-widget styling (buttons, edits, scrollbars, popups...).
 *   - Real text editor: nk_edit_buffer() with persistent caret / selection /
 *     scroll state, so the document survives losing focus.
 *   - Line-number gutter that tracks the editor scroll offset.
 *   - Live document stats (lines, characters, Ln/Col) in the status bar.
 *   - Open / Save / Save As (Win32 common dialogs), drag & drop, and
 *     Ctrl+N/O/S, Shift+Ctrl+S, F5 shortcuts.
 *   - Console panel with timestamped, colour-coded build output.
 *
 *  Build (MSVC):
 *      cl /O2 main.c user32.lib gdi32.lib comdlg32.lib
 *  Build (MinGW-w64):
 *      gcc -O2 -o studio.exe main.c -lgdi32 -lcomdlg32 -mwindows
 *
 *  Requires: nuklear.h (v4.x) and nuklear_gdi.h in the same directory.
 * ==========================================================================*/

#define _WIN32_WINNT 0x0601
#define WINVER 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_GDI_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_gdi.h"

/* ------------------------------------------------------------------------- */
/*  Layout metrics                                                           */
/* ------------------------------------------------------------------------- */
#define APP_TITLE       L"Nuklear Studio"
#define HEADER_H        58.0f
#define STATUS_H        26.0f
#define TABS_H          38.0f
#define SIDEBAR_W       246.0f
#define CONSOLE_H       176.0f
#define CONSOLE_HEAD_H  30.0f
#define GUTTER_W        48.0f
#define EDIT_CAP        (256 * 1024)
#define LOG_MAX         48
#define DEFAULT_W       1120
#define DEFAULT_H       720

/* ------------------------------------------------------------------------- */
/*  Palette                                                                  */
/* ------------------------------------------------------------------------- */
struct Theme {
    const char *name;
    int dark;
    struct nk_color bg;          /* window backdrop / separator colour      */
    struct nk_color panel;       /* header + tab strip                      */
    struct nk_color panel_alt;   /* sidebar                                 */
    struct nk_color editor;      /* code surface                            */
    struct nk_color gutter;      /* line-number strip                       */
    struct nk_color console;     /* build output surface                    */
    struct nk_color status;      /* status bar                              */
    struct nk_color border;
    struct nk_color text;
    struct nk_color dim;
    struct nk_color accent;
    struct nk_color accent_hover;
    struct nk_color accent_text;
    struct nk_color sel;
    struct nk_color green;
    struct nk_color amber;
    struct nk_color red;
};

#define C(r, g, b) {(nk_byte)(r), (nk_byte)(g), (nk_byte)(b), 255}

static const struct Theme THEMES[] = {
    /* 0 - Midnight (default dark) */
    { "Midnight", 1,
      C(9, 12, 17), C(19, 25, 34), C(15, 20, 28),
      C(13, 18, 25), C(10, 14, 20), C(11, 16, 22),
      C(9, 13, 19),
      C(31, 39, 49), C(230, 237, 245), C(125, 139, 156),
      C(76, 141, 255), C(107, 163, 255), C(255, 255, 255),
      C(27, 62, 112),
      C(70, 201, 139), C(240, 180, 41), C(255, 107, 107) },

    /* 1 - Graphite (warm dark) */
    { "Graphite", 1,
      C(20, 20, 22), C(29, 29, 32), C(24, 24, 27),
      C(22, 22, 25), C(18, 18, 21), C(19, 19, 22),
      C(17, 17, 20),
      C(44, 44, 48), C(236, 236, 238), C(150, 150, 156),
      C(255, 138, 76), C(255, 164, 112), C(28, 20, 16),
      C(78, 55, 40),
      C(126, 206, 130), C(240, 190, 90), C(255, 120, 120) },

    /* 2 - Paper (light) */
    { "Paper", 0,
      C(219, 225, 234), C(255, 255, 255), C(247, 249, 252),
      C(255, 255, 255), C(244, 247, 251), C(250, 251, 253),
      C(255, 255, 255),
      C(217, 224, 234), C(24, 33, 44), C(110, 125, 143),
      C(47, 111, 237), C(29, 88, 208), C(255, 255, 255),
      C(205, 224, 255),
      C(23, 150, 96), C(176, 112, 14), C(206, 62, 62) },

    /* 3 - Ivory (warm light) */
    { "Ivory", 0,
      C(228, 222, 210), C(253, 251, 246), C(248, 245, 238),
      C(253, 252, 248), C(244, 240, 231), C(250, 248, 243),
      C(252, 250, 245),
      C(226, 219, 206), C(40, 36, 30), C(122, 112, 98),
      C(196, 106, 42), C(171, 88, 32), C(255, 255, 255),
      C(246, 220, 194),
      C(38, 138, 88), C(170, 116, 20), C(190, 66, 60) },
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

/* ------------------------------------------------------------------------- */
/*  Application state                                                        */
/* ------------------------------------------------------------------------- */
enum { LOG_INFO = 0, LOG_OK, LOG_WARN, LOG_ERR, LOG_CMD };

struct LogLine {
    char time[16];
    char text[240];
    int  level;
};

struct App {
    struct nk_context *ctx;

    GdiFont *font_ui;
    GdiFont *font_mono;
    GdiFont *font_title;

    const struct Theme *th;
    int  theme_idx;

    struct nk_style_button btn_default;
    struct nk_style_button btn_primary;
    struct nk_style_button btn_ghost;

    char *buffer;                 /* backing store of the text editor       */
    char  file_name[128];
    char  file_path[MAX_PATH * 3];
    int   dirty;
    unsigned int saved_hash;

    int   caret, sel_start, sel_end;
    struct nk_vec2 scroll;

    /* view options */
    int  show_sidebar;
    int  show_console;
    int  show_gutter;
    int  font_size;

    /* console */
    struct LogLine log[LOG_MAX];
    int  log_count;

    /* status bar */
    char win_title[220];
    char status[192];
    DWORD status_until;
    int  status_kind;             /* 0 info, 1 ok, 2 warn, 3 err */

    /* modal "unsaved changes" guard: 0 none, 1 new, 2 open, 3 quit */
    int  pending;

    /* window */
    HWND hwnd;
    int  quit;
};

static struct App g_app;

/* ------------------------------------------------------------------------- */
/*  Small helpers                                                            */
/* ------------------------------------------------------------------------- */
static float text_w(const struct nk_user_font *f, const char *s)
{
    int len = (int)strlen(s);
    if (!len) return 0.0f;
    return f->width(f->userdata, f->height, s, len);
}

static void draw_str(struct nk_context *ctx, float x, float y, float max_w,
                     const char *s, struct nk_color c)
{
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    struct nk_rect r;
    r.x = x; r.y = y; r.w = max_w; r.h = ctx->style.font->height + 2.0f;
    nk_draw_text(cv, r, s, (int)strlen(s), ctx->style.font, nk_rgba(0, 0, 0, 0), c);
}

/* top-left corner (in window coordinates) of the group currently being filled */
static void group_origin(struct nk_context *ctx, float *x, float *y)
{
    if (ctx->current && ctx->current->layout) {
        *x = ctx->current->layout->bounds.x;
        *y = ctx->current->layout->bounds.y;
    } else {
        *x = 0.0f; *y = 0.0f;
    }
}

static struct nk_color mix(struct nk_color a, struct nk_color b, float t)
{
    struct nk_color c;
    c.r = (nk_byte)(a.r + (b.r - a.r) * t);
    c.g = (nk_byte)(a.g + (b.g - a.g) * t);
    c.b = (nk_byte)(a.b + (b.b - a.b) * t);
    c.a = 255;
    return c;
}

static void set_status(struct App *a, int kind, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(a->status, sizeof(a->status), fmt, args);
    va_end(args);
    a->status_kind = kind;
    a->status_until = GetTickCount() + 6000;
}

/* ------------------------------------------------------------------------- */
/*  Console log                                                              */
/* ------------------------------------------------------------------------- */
static void log_add(struct App *a, int level, const char *fmt, ...)
{
    SYSTEMTIME st;
    struct LogLine *l;

    if (a->log_count == LOG_MAX) {
        memmove(&a->log[0], &a->log[1], sizeof(a->log[0]) * (LOG_MAX - 1));
        a->log_count--;
    }
    l = &a->log[a->log_count++];
    GetLocalTime(&st);
    snprintf(l->time, sizeof(l->time), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(l->text, sizeof(l->text), fmt, args);
        va_end(args);
    }
    l->level = level;
}

static void log_clear(struct App *a) { a->log_count = 0; }

/* ------------------------------------------------------------------------- */
/*  Document helpers                                                         */
/* ------------------------------------------------------------------------- */
static const char *doc_text(struct App *a)
{
    return nk_str_get_const(&a->ctx->text_edit.string);
}

static int doc_len(struct App *a)
{
    return (int)nk_str_len_char(&a->ctx->text_edit.string);
}

static unsigned int hash_text(const char *s, int len)
{
    unsigned int h = 2166136261u;
    int i;
    for (i = 0; i < len; ++i) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

/* line / column of the caret, 1-based, and total line count */
static void caret_pos(struct App *a, int *line, int *col, int *total)
{
    struct nk_text_edit *te = &a->ctx->text_edit;
    const char *s = doc_text(a);
    int len = doc_len(a);
    int i = 0, runes = 0, ln = 1, cl = 1, lines = 1;

    while (i < len) {
        nk_rune u;
        int gl;
        if (runes == te->cursor) break;
        if (s[i] == '\n') { lines++; if (runes < te->cursor) { ln++; cl = 1; } i++; runes++; continue; }
        gl = nk_utf_decode(s + i, &u, len - i);
        if (gl <= 0) break;
        i += gl; runes++;
        if (runes <= te->cursor) cl++;
    }
    if (line)  *line = ln;
    if (col)   *col = cl;
    if (total) {
        lines = 1;
        for (i = 0; i < len; ++i) if (s[i] == '\n') lines++;
        *total = lines;
    }
}

/* reset the editing state after the buffer content was replaced */
static void doc_reset(struct App *a, int byte_len)
{
    struct nk_text_edit *te = &a->ctx->text_edit;
    nk_textedit_clear_state(te, NK_TEXT_EDIT_MULTI_LINE, nk_filter_default);
    nk_str_init_fixed(&te->string, a->buffer, EDIT_CAP);
    te->string.buffer.allocated = (nk_size)byte_len;
    te->string.len = nk_utf_len(a->buffer, byte_len);
    te->mode = NK_TEXT_EDIT_MODE_INSERT;
    te->initialized = 0;
    te->cursor = te->select_start = te->select_end = 0;
    a->caret = a->sel_start = a->sel_end = 0;
    a->scroll = nk_vec2(0, 0);
    a->saved_hash = hash_text(a->buffer, byte_len);
    a->dirty = 0;
}

static void doc_new(struct App *a)
{
    static const char *sample =
        "#include <stdio.h>\n"
        "\n"
        "/* Nuklear Studio - a friendlier face for the GDI sample. */\n"
        "\n"
        "int main(void) {\n"
        "    printf(\"Hello, Nuklear!\\n\");\n"
        "    return 0;\n"
        "}\n";
    int len = (int)strlen(sample);
    if (len >= EDIT_CAP) len = EDIT_CAP - 1;
    memcpy(a->buffer, sample, (nk_size)len);
    a->buffer[len] = 0;
    doc_reset(a, len);
    a->file_name[0] = 0;
    snprintf(a->file_path, sizeof(a->file_path), "%s", "");
    log_add(a, LOG_INFO, "New document created.");
    set_status(a, 0, "New file");
}

/* convert a wide path to UTF-8 */
static void wide_to_utf8(const wchar_t *w, char *out, int cap)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap - 1, NULL, NULL);
    if (n <= 0) out[0] = 0; else out[n > cap - 1 ? cap - 1 : n] = 0;
}

static void utf8_to_wide(const char *s, wchar_t *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap - 1);
    if (n <= 0) out[0] = 0; else out[n > cap - 1 ? cap - 1 : n] = 0;
}

static void set_file_name(struct App *a, const char *path)
{
    const char *p = path, *q;
    snprintf(a->file_path, sizeof(a->file_path), "%s", path);
    q = strrchr(path, '\\');
    if (!q) q = strrchr(path, '/');
    if (q) p = q + 1;
    snprintf(a->file_name, sizeof(a->file_name), "%s", p);
    if (!a->file_name[0]) snprintf(a->file_name, sizeof(a->file_name), "untitled.c");
}

static int doc_load(struct App *a, const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    int len, i, o;

    if (!f) {
        log_add(a, LOG_ERR, "Cannot open \"%s\"", path);
        set_status(a, 3, "Cannot open file");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) size = 0;
    if (size > EDIT_CAP - 2) size = EDIT_CAP - 2;
    len = (int)fread(a->buffer, 1, (size_t)size, f);
    fclose(f);
    a->buffer[len] = 0;

    /* strip UTF-8 BOM + normalise CRLF -> LF so the caret maths stay simple */
    o = 0;
    i = 0;
    if (len >= 3 && (unsigned char)a->buffer[0] == 0xEF &&
        (unsigned char)a->buffer[1] == 0xBB && (unsigned char)a->buffer[2] == 0xBF) i = 3;
    for (; i < len; ++i) {
        if (a->buffer[i] == '\r') {
            if (i + 1 < len && a->buffer[i + 1] == '\n') continue;
            a->buffer[o++] = '\n';
        } else a->buffer[o++] = a->buffer[i];
    }
    a->buffer[o] = 0;

    doc_reset(a, o);
    set_file_name(a, path);
    log_add(a, LOG_OK, "Opened %s (%d bytes)", a->file_name, o);
    set_status(a, 1, "Opened %s", a->file_name);
    return 1;
}

static int doc_save(struct App *a, int force_dialog)
{
    wchar_t wpath[MAX_PATH * 2];
    char path[MAX_PATH * 3];
    const char *text;
    int len;
    FILE *f;

    if (force_dialog || !a->file_path[0]) {
        OPENFILENAMEW ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        wpath[0] = 0;
        if (a->file_path[0]) utf8_to_wide(a->file_path, wpath, MAX_PATH * 2);
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = a->hwnd;
        ofn.lpstrFilter = L"Source files\0*.c;*.h;*.cpp;*.hpp;*.txt\0All files\0*.*\0";
        ofn.lpstrFile = wpath;
        ofn.nMaxFile = MAX_PATH * 2;
        ofn.lpstrDefExt = L"c";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&ofn)) { set_status(a, 0, "Save cancelled"); return 0; }
        wide_to_utf8(wpath, path, sizeof(path));
    } else {
        snprintf(path, sizeof(path), "%s", a->file_path);
    }

    text = doc_text(a);
    len = doc_len(a);
    f = fopen(path, "wb");
    if (!f) {
        log_add(a, LOG_ERR, "Cannot write \"%s\"", path);
        set_status(a, 3, "Cannot write file");
        return 0;
    }
    if (len) fwrite(text, 1, (size_t)len, f);
    fclose(f);

    set_file_name(a, path);
    a->saved_hash = hash_text(text, len);
    a->dirty = 0;
    log_add(a, LOG_OK, "Saved %s (%d bytes)", a->file_name, len);
    set_status(a, 1, "Saved %s", a->file_name);
    return 1;
}

static void doc_open_dialog(struct App *a)
{
    wchar_t wpath[MAX_PATH * 2];
    char path[MAX_PATH * 3];
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    wpath[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = a->hwnd;
    ofn.lpstrFilter = L"Source files\0*.c;*.h;*.cpp;*.hpp;*.txt\0All files\0*.*\0";
    ofn.lpstrFile = wpath;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) { set_status(a, 0, "Open cancelled"); return; }
    wide_to_utf8(wpath, path, sizeof(path));
    doc_load(a, path);
}

/* run an action, asking about unsaved work first when necessary */
static void request_action(struct App *a, int action)
{
    if (a->dirty) { a->pending = action; return; }
    switch (action) {
        case 1: doc_new(a); break;
        case 2: doc_open_dialog(a); break;
        case 3: a->quit = 1; break;
    }
}

/* ------------------------------------------------------------------------- */
/*  Simulated build & run                                                    */
/* ------------------------------------------------------------------------- */
static void doc_run(struct App *a)
{
    const char *src = doc_text(a);
    int warnings = 0, printed = 0;
    const char *p;

    log_add(a, LOG_CMD, "$ gcc -std=c99 -Wall -Wextra %s -o prog.exe",
            a->file_name[0] ? a->file_name : "main.c");

    if (!strstr(src, "int main")) {
        log_add(a, LOG_ERR, "main.c: error: undefined reference to 'main'");
        log_add(a, LOG_ERR, "Build failed: 1 error");
        set_status(a, 3, "Build failed - 1 error");
        return;
    }
    if (strstr(src, "strcpy(") || strstr(src, "sprintf(")) {
        log_add(a, LOG_WARN, "main.c: warning: unsafe string function, prefer snprintf");
        warnings++;
    }
    if (strstr(src, "gets(")) {
        log_add(a, LOG_WARN, "main.c: warning: 'gets' is deprecated");
        warnings++;
    }
    log_add(a, LOG_OK, "Build succeeded in 0.31s (0 errors, %d warnings)", warnings);
    log_add(a, LOG_CMD, "$ ./prog.exe");

    /* echo printf("...") literals so the console has something to show */
    p = src;
    while ((p = strstr(p, "printf(")) != NULL) {
        char lit[200];
        int n = 0;
        p += 7;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '"') continue;
        p++;
        while (*p && *p != '"' && n < (int)sizeof(lit) - 1) {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                    case 'n': lit[n++] = '\n'; break;
                    case 't': lit[n++] = '\t'; break;
                    case '\\': lit[n++] = '\\'; break;
                    case '"': lit[n++] = '"'; break;
                    default: lit[n++] = *p; break;
                }
                p++;
            } else lit[n++] = *p++;
        }
        lit[n] = 0;
        if (*p == '"') p++;
        /* split on newlines so the console shows one entry per output line */
        {
            char *line = lit, *nl;
            while ((nl = strchr(line, '\n')) != NULL) {
                *nl = 0;
                log_add(a, LOG_INFO, "%s", line);
                printed = 1;
                line = nl + 1;
            }
            if (*line) { log_add(a, LOG_INFO, "%s", line); printed = 1; }
        }
    }
    if (!printed) log_add(a, LOG_INFO, "(program produced no output)");
    log_add(a, LOG_OK, "Process exited with code 0 (0.01s)");
    set_status(a, 1, "Build succeeded%s", warnings ? " with warnings" : "");
}

/* ------------------------------------------------------------------------- */
/*  Theming                                                                  */
/* ------------------------------------------------------------------------- */
static struct nk_style_button make_button(struct nk_color normal, struct nk_color hover,
                                          struct nk_color active, struct nk_color border,
                                          struct nk_color text, struct nk_color text_hover,
                                          float rounding)
{
    struct nk_style_button b;
    ZeroMemory(&b, sizeof(b));
    b.normal = nk_style_item_color(normal);
    b.hover  = nk_style_item_color(hover);
    b.active = nk_style_item_color(active);
    b.border_color = border;
    b.text_background = nk_rgba(0, 0, 0, 0);
    b.text_normal = text;
    b.text_hover = text_hover;
    b.text_active = text_hover;
    b.text_alignment = NK_TEXT_CENTERED;
    b.border = 1.0f;
    b.rounding = rounding;
    b.padding = nk_vec2(10, 5);
    b.image_padding = nk_vec2(4, 4);
    b.touch_padding = nk_vec2(0, 0);
    b.color_factor_background = 1.0f;
    b.color_factor_text = 1.0f;
    b.disabled_factor = 0.5f;
    return b;
}

static void apply_theme(struct App *a)
{
    struct nk_style *s = &a->ctx->style;
    const struct Theme *t = a->th;
    struct nk_color subtle, edit_bg;

    s->text.color = t->text;
    s->text.padding = nk_vec2(0, 0);
    s->text.color_factor = 1.0f;

    /* window / panel defaults ------------------------------------------- */
    s->window.background = t->bg;
    s->window.fixed_background = nk_style_item_color(t->bg);
    s->window.border_color = t->border;
    s->window.popup_border_color = t->border;
    s->window.combo_border_color = t->border;
    s->window.contextual_border_color = t->border;
    s->window.menu_border_color = t->border;
    s->window.group_border_color = t->border;
    s->window.tooltip_border_color = t->border;
    s->window.border = 0.0f;
    s->window.group_border = 0.0f;
    s->window.popup_border = 1.0f;
    s->window.combo_border = 1.0f;
    s->window.rounding = 0.0f;
    s->window.spacing = nk_vec2(0, 0);
    s->window.padding = nk_vec2(0, 0);
    s->window.group_padding = nk_vec2(0, 0);
    s->window.popup_padding = nk_vec2(8, 8);
    s->window.combo_padding = nk_vec2(4, 4);
    s->window.contextual_padding = nk_vec2(4, 4);
    s->window.menu_padding = nk_vec2(4, 4);
    s->window.tooltip_padding = nk_vec2(6, 4);
    s->window.scrollbar_size = nk_vec2(9, 9);
    s->window.min_size = nk_vec2(0, 0);
    s->window.min_row_height_padding = 2.0f;

    /* buttons ------------------------------------------------------------ */
    a->btn_default = make_button(t->panel_alt, mix(t->panel_alt, t->text, 0.10f),
                                 mix(t->panel_alt, t->text, 0.18f), t->border,
                                 t->text, t->text, 5.0f);
    a->btn_primary = make_button(t->accent, t->accent_hover, t->accent,
                                 t->accent, t->accent_text, t->accent_text, 5.0f);
    a->btn_ghost   = make_button(t->panel, mix(t->panel, t->text, 0.08f),
                                 mix(t->panel, t->text, 0.14f), t->border,
                                 mix(t->text, t->dim, 0.15f), t->text, 5.0f);
    s->button = a->btn_default;

    /* selectable (used nowhere critical, but keep it on-palette) --------- */
    s->selectable.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    s->selectable.hover = nk_style_item_color(mix(t->panel, t->text, 0.08f));
    s->selectable.pressed = nk_style_item_color(mix(t->panel, t->text, 0.14f));
    s->selectable.normal_active = nk_style_item_color(t->accent);
    s->selectable.hover_active = nk_style_item_color(t->accent_hover);
    s->selectable.pressed_active = nk_style_item_color(t->accent);
    s->selectable.text_normal = t->dim;
    s->selectable.text_hover = t->text;
    s->selectable.text_pressed = t->text;
    s->selectable.text_normal_active = t->accent_text;
    s->selectable.text_hover_active = t->accent_text;
    s->selectable.text_pressed_active = t->accent_text;
    s->selectable.text_background = nk_rgba(0, 0, 0, 0);
    s->selectable.rounding = 5.0f;
    s->selectable.padding = nk_vec2(8, 4);

    /* toggles (checkboxes) ----------------------------------------------- */
    s->checkbox.normal = nk_style_item_color(t->editor);
    s->checkbox.hover = nk_style_item_color(mix(t->editor, t->text, 0.10f));
    s->checkbox.active = nk_style_item_color(t->accent);
    s->checkbox.border_color = t->border;
    s->checkbox.border = 1.0f;
    s->checkbox.cursor_normal = nk_style_item_color(t->accent);
    s->checkbox.cursor_hover = nk_style_item_color(t->accent_hover);
    s->checkbox.text_normal = t->text;
    s->checkbox.text_hover = t->text;
    s->checkbox.text_active = t->text;
    s->checkbox.text_background = nk_rgba(0, 0, 0, 0);
    s->checkbox.text_alignment = NK_TEXT_LEFT;
    s->checkbox.padding = nk_vec2(3, 3);
    s->checkbox.spacing = 10.0f;
    s->checkbox.touch_padding = nk_vec2(0, 2);
    s->checkbox.touch_padding = nk_vec2(0, 0);
    s->option = s->checkbox;

    /* edit (text editor) -------------------------------------------------- */
    edit_bg = t->editor;
    s->edit.normal = nk_style_item_color(edit_bg);
    s->edit.hover = nk_style_item_color(edit_bg);
    s->edit.active = nk_style_item_color(edit_bg);
    s->edit.border_color = edit_bg;
    s->edit.border = 0.0f;
    s->edit.rounding = 0.0f;
    s->edit.padding = nk_vec2(GUTTER_W + 14.0f, 10.0f);
    s->edit.row_padding = 4.0f;
    s->edit.cursor_normal = t->accent;
    s->edit.cursor_hover = t->accent_hover;
    s->edit.cursor_text_normal = edit_bg;
    s->edit.cursor_text_hover = edit_bg;
    s->edit.cursor_size = 2.0f;
    s->edit.text_normal = t->text;
    s->edit.text_hover = t->text;
    s->edit.text_active = t->text;
    s->edit.selected_normal = t->sel;
    s->edit.selected_hover = t->sel;
    s->edit.selected_text_normal = t->text;
    s->edit.selected_text_hover = t->text;

    /* scrollbars ---------------------------------------------------------- */
    subtle = mix(t->panel, t->text, 0.16f);
    s->edit.scrollbar.normal = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    s->edit.scrollbar.hover = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    s->edit.scrollbar.active = nk_style_item_color(nk_rgba(0, 0, 0, 0));
    s->edit.scrollbar.border_color = nk_rgba(0, 0, 0, 0);
    s->edit.scrollbar.border = 0.0f;
    s->edit.scrollbar.rounding = 4.0f;
    s->edit.scrollbar.cursor_normal = nk_style_item_color(subtle);
    s->edit.scrollbar.cursor_hover = nk_style_item_color(mix(subtle, t->text, 0.25f));
    s->edit.scrollbar.cursor_active = nk_style_item_color(t->accent);
    s->edit.scrollbar.cursor_border_color = nk_rgba(0, 0, 0, 0);
    s->edit.scrollbar.rounding_cursor = 4.0f;
    s->edit.scrollbar.border_cursor = 0.0f;
    s->edit.scrollbar.padding = nk_vec2(2, 2);
    s->edit.scrollbar.show_buttons = 0;
    s->scrollv = s->edit.scrollbar;
    s->scrollh = s->edit.scrollbar;

    /* combo -------------------------------------------------------------- */
    s->combo.normal = nk_style_item_color(t->editor);
    s->combo.hover = nk_style_item_color(mix(t->editor, t->text, 0.08f));
    s->combo.active = nk_style_item_color(mix(t->editor, t->text, 0.12f));
    s->combo.border_color = t->border;
    s->combo.border = 1.0f;
    s->combo.rounding = 5.0f;
    s->combo.label_normal = t->text;
    s->combo.label_hover = t->text;
    s->combo.label_active = t->text;
    s->combo.symbol_normal = t->dim;
    s->combo.symbol_hover = t->text;
    s->combo.symbol_active = t->text;
    s->combo.content_padding = nk_vec2(9, 4);
    s->combo.button_padding = nk_vec2(2, 2);
    s->combo.spacing = nk_vec2(4, 0);
    s->combo.sym_normal = NK_SYMBOL_TRIANGLE_DOWN;
    s->combo.sym_hover = NK_SYMBOL_TRIANGLE_DOWN;
    s->combo.sym_active = NK_SYMBOL_TRIANGLE_DOWN;
    s->combo.button = make_button(nk_rgba(0, 0, 0, 0), nk_rgba(0, 0, 0, 0),
                                  nk_rgba(0, 0, 0, 0), nk_rgba(0, 0, 0, 0),
                                  t->dim, t->text, 4.0f);
    s->combo.button.border = 0.0f;
    s->combo.button.padding = nk_vec2(0, 0);
    s->combo.button.touch_padding = nk_vec2(0, 0);
    s->combo.button.image_padding = nk_vec2(0, 0);

    /* property (used for the font size spinner) -------------------------- */
    s->property.normal = nk_style_item_color(t->editor);
    s->property.hover = nk_style_item_color(t->editor);
    s->property.active = nk_style_item_color(t->editor);
    s->property.border_color = t->border;
    s->property.border = 1.0f;
    s->property.rounding = 5.0f;
    s->property.label_normal = t->text;
    s->property.label_hover = t->text;
    s->property.label_active = t->text;
    s->property.padding = nk_vec2(8, 4);
    s->property.edit = s->edit;
    s->property.edit.padding = nk_vec2(2, 2);
    s->property.edit.border = 0.0f;
    s->property.inc_button = make_button(nk_rgba(0, 0, 0, 0), nk_rgba(0, 0, 0, 0),
                                         nk_rgba(0, 0, 0, 0), nk_rgba(0, 0, 0, 0),
                                         t->dim, t->text, 4.0f);
    s->property.dec_button = s->property.inc_button;
    s->property.sym_left = NK_SYMBOL_TRIANGLE_LEFT;
    s->property.sym_right = NK_SYMBOL_TRIANGLE_RIGHT;

    /* popups use the window background, keep them readable -------------- */
    s->window.background = t->bg;
}

/* ------------------------------------------------------------------------- */
/*  Shared widget helpers                                                    */
/* ------------------------------------------------------------------------- */
static int ui_button(struct App *a, const char *label, int kind)
{
    struct nk_style_button saved = a->ctx->style.button;
    int r;
    if (kind == 1) a->ctx->style.button = a->btn_primary;
    else if (kind == 2) a->ctx->style.button = a->btn_ghost;
    else if (kind == 3) {
        a->ctx->style.button = a->btn_ghost;
        a->ctx->style.button.padding = nk_vec2(2, 2);
        a->ctx->style.button.text_alignment = NK_TEXT_CENTERED;
    }
    else a->ctx->style.button = a->btn_default;
    r = nk_button_label(a->ctx, label) ? 1 : 0;
    a->ctx->style.button = saved;
    return r;
}

/* a full-width row that acts as a switch: ghost button + hand drawn indicator */
static int ui_toggle_row(struct App *a, const char *label, int *value)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_style_button saved = ctx->style.button;
    struct nk_command_buffer *cv;
    struct nk_rect b;
    int pressed;

    b = nk_widget_bounds(ctx);          /* peek the slot this row will occupy */

    ctx->style.button = a->btn_ghost;
    ctx->style.button.text_alignment = NK_TEXT_LEFT;
    ctx->style.button.padding = nk_vec2(30, 4);
    ctx->style.button.text_normal = *value ? t->text : t->dim;
    ctx->style.button.text_hover = t->text;
    ctx->style.button.text_active = t->text;
    pressed = nk_button_label(ctx, label) ? 1 : 0;
    ctx->style.button = saved;

    if (pressed) *value = !*value;

    cv = nk_window_get_canvas(ctx);
    {
        struct nk_rect box;
        box.w = 14.0f; box.h = 14.0f;
        box.x = b.x + 10.0f;
        box.y = b.y + (b.h - box.h) * 0.5f;
        if (*value) {
            nk_fill_rect(cv, box, 4.0f, t->accent);
            nk_stroke_line(cv, box.x + 3.2f, box.y + 7.0f, box.x + 6.0f, box.y + 9.8f,
                           1.6f, t->accent_text);
            nk_stroke_line(cv, box.x + 6.0f, box.y + 9.8f, box.x + 10.8f, box.y + 4.2f,
                           1.6f, t->accent_text);
        } else {
            nk_stroke_rect(cv, box, 4.0f, 1.2f, t->border);
        }
    }
    return pressed;
}

/* one cell of the segmented theme picker */
static int ui_segment(struct App *a, const char *label, int active, float w)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_style_button saved = ctx->style.button;
    int pressed;

    if (active) {
        saved = ctx->style.button;
        ctx->style.button = make_button(t->accent, t->accent_hover, t->accent, t->accent,
                                        t->accent_text, t->accent_text, 5.0f);
    } else {
        saved = ctx->style.button;
        ctx->style.button = a->btn_ghost;
        ctx->style.button.text_normal = t->dim;
    }
    (void)w;
    pressed = nk_button_label(ctx, label) ? 1 : 0;
    ctx->style.button = saved;
    return pressed;
}

static void row_spacer(struct nk_context *ctx, float h)
{
    nk_layout_row_dynamic(ctx, h, 1);
    nk_spacer(ctx);
}

/* two-column "key .......... value" row inside a padded panel */
static void kv_row(struct nk_context *ctx, float w, const char *key, const char *val,
                   struct nk_color vc, struct nk_color dim)
{
    nk_layout_row_begin(ctx, NK_STATIC, 18, 4);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 32 - 86);
    nk_label_colored(ctx, key, NK_TEXT_LEFT, dim);
    nk_layout_row_push(ctx, 86);
    nk_label_colored(ctx, val, NK_TEXT_RIGHT, vc);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);
}

static void caption(struct nk_context *ctx, float w, const char *text, struct nk_color c)
{
    nk_layout_row_begin(ctx, NK_STATIC, 16, 3);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 32);
    nk_label_colored(ctx, text, NK_TEXT_LEFT, c);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);
}

/* ------------------------------------------------------------------------- */
/*  Header                                                                   */
/* ------------------------------------------------------------------------- */
static void draw_header(struct App *a, float w)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_command_buffer *cv;
    float left = 54.0f;
    float right_group = 60 + 62 + 62 + 78 + 92 + 104 + 96 + 8 * 6 + 16;
    float flex = w - left - right_group;
    char theme_label[64];
    float ox, oy;

    if (flex < 0) flex = 0;

    row_spacer(ctx, 13);
    nk_layout_row_begin(ctx, NK_STATIC, 30, 17);
    nk_layout_row_push(ctx, left); nk_spacer(ctx);

    /* flexible gap where the wordmark is drawn */
    nk_layout_row_push(ctx, flex); nk_spacer(ctx);

    nk_layout_row_push(ctx, 60);
    if (ui_button(a, "New", 2)) request_action(a, 1);
    nk_layout_row_push(ctx, 8); nk_spacer(ctx);
    nk_layout_row_push(ctx, 62);
    if (ui_button(a, "Open", 2)) request_action(a, 2);
    nk_layout_row_push(ctx, 8); nk_spacer(ctx);
    nk_layout_row_push(ctx, 62);
    if (ui_button(a, "Save", 2)) doc_save(a, 0);
    nk_layout_row_push(ctx, 8); nk_spacer(ctx);
    nk_layout_row_push(ctx, 78);
    if (ui_button(a, "Save As", 2)) doc_save(a, 1);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, 92);
    if (ui_button(a, "Run  (F5)", 1)) doc_run(a);
    nk_layout_row_push(ctx, 8); nk_spacer(ctx);
    nk_layout_row_push(ctx, 104);
    if (ui_button(a, a->show_console ? "Hide console" : "Show console", 2)) {
        a->show_console = !a->show_console;
        set_status(a, 0, a->show_console ? "Console shown" : "Console hidden");
    }
    nk_layout_row_push(ctx, 8); nk_spacer(ctx);
    snprintf(theme_label, sizeof(theme_label), "%s", t->name);
    nk_layout_row_push(ctx, 96);
    if (ui_button(a, theme_label, 2)) {
        a->theme_idx = (a->theme_idx + 1) % THEME_COUNT;
        a->th = &THEMES[a->theme_idx];
        apply_theme(a);
        set_status(a, 0, "Theme: %s", a->th->name);
    }
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    /* wordmark ---------------------------------------------------------- */
    cv = nk_window_get_canvas(ctx);
    group_origin(ctx, &ox, &oy);
    {
        struct nk_rect logo;
        struct nk_user_font *f = &a->font_title->nk;
        float tw, th;
        logo.x = ox + 16; logo.y = oy + 15; logo.w = 26; logo.h = 26;
        nk_fill_rect(cv, logo, 7.0f, t->accent);
        tw = text_w(f, "N");
        th = f->height;
        draw_str(ctx, logo.x + (26 - tw) * 0.5f, logo.y + (26 - th) * 0.5f + 1.0f,
                 26.0f, "N", t->accent_text);

        if (flex > 140.0f) {
            nk_style_set_font(ctx, f);
            tw = text_w(f, "Nuklear Studio");
            th = f->height;
            draw_str(ctx, ox + 54.0f, oy + 15.0f + (26.0f - th) * 0.5f, flex + 40.0f,
                     "Nuklear Studio", t->text);
            nk_style_set_font(ctx, &a->font_ui->nk);
        }
    }
}

/* ------------------------------------------------------------------------- */
/*  Sidebar                                                                  */
/* ------------------------------------------------------------------------- */
static void draw_sidebar(struct App *a, float w, float h)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    char tmp[96];
    int lines, line, col;

    (void)h;
    caption(ctx, w, "DOCUMENT", t->dim);
    row_spacer(ctx, 4);

    nk_layout_row_begin(ctx, NK_STATIC, 22, 3);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 32 - 74);
    nk_label_colored(ctx, a->file_name[0] ? a->file_name : "untitled.c",
                     NK_TEXT_LEFT, t->text);
    nk_layout_row_push(ctx, 74);
    nk_label_colored(ctx, a->dirty ? "unsaved" : "saved",
                     NK_TEXT_RIGHT, a->dirty ? t->amber : t->green);
    nk_layout_row_end(ctx);

    /* full path, wrapped */
    nk_layout_row_begin(ctx, NK_STATIC, 30, 3);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 32);
    nk_label_colored_wrap(ctx, a->file_path[0] ? a->file_path : "(not saved yet)", t->dim);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    row_spacer(ctx, 6);
    caret_pos(a, &line, &col, &lines);
    snprintf(tmp, sizeof(tmp), "%d", lines);
    kv_row(ctx, w, "Lines", tmp, t->text, t->dim);
    snprintf(tmp, sizeof(tmp), "%d", doc_len(a));
    kv_row(ctx, w, "Characters", tmp, t->text, t->dim);

    row_spacer(ctx, 18);

    /* ---- editor ---- */
    caption(ctx, w, "EDITOR", t->dim);
    row_spacer(ctx, 6);

    nk_layout_row_begin(ctx, NK_STATIC, 28, 6);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 32 - 104);
    nk_label_colored(ctx, "Font size", NK_TEXT_LEFT, t->dim);
    nk_layout_row_push(ctx, 26);
    if (ui_button(a, "-", 3) && a->font_size > 9) a->font_size--;
    nk_layout_row_push(ctx, 52);
    {
        char sz[16];
        snprintf(sz, sizeof(sz), "%d px", a->font_size);
        nk_label_colored(ctx, sz, NK_TEXT_CENTERED, t->text);
    }
    nk_layout_row_push(ctx, 26);
    if (ui_button(a, "+", 3) && a->font_size < 28) a->font_size++;
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    row_spacer(ctx, 4);
    nk_layout_row_begin(ctx, NK_STATIC, 28, 3);
    nk_layout_row_push(ctx, 12); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 24);
    ui_toggle_row(a, "Line numbers", &a->show_gutter);
    nk_layout_row_push(ctx, 12); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    nk_layout_row_begin(ctx, NK_STATIC, 28, 3);
    nk_layout_row_push(ctx, 12); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 24);
    ui_toggle_row(a, "Console panel", &a->show_console);
    nk_layout_row_push(ctx, 12); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    row_spacer(ctx, 18);

    /* ---- appearance ---- */
    caption(ctx, w, "APPEARANCE", t->dim);
    row_spacer(ctx, 6);

    {
        static const char *names[THEME_COUNT] = { "Midnight", "Graphite", "Paper", "Ivory" };
        int i, row, col;
        float cell = (w - 32 - 6.0f) * 0.5f;
        for (row = 0; row < 2; ++row) {
            nk_layout_row_begin(ctx, NK_STATIC, 26, 5);
            nk_layout_row_push(ctx, 16); nk_spacer(ctx);
            for (col = 0; col < 2; ++col) {
                i = row * 2 + col;
                nk_layout_row_push(ctx, cell);
                if (ui_segment(a, names[i], i == a->theme_idx, cell)) {
                    a->theme_idx = i;
                    a->th = &THEMES[i];
                    apply_theme(a);
                    set_status(a, 0, "Theme: %s", a->th->name);
                }
                nk_layout_row_push(ctx, 6); nk_spacer(ctx);
            }
            nk_layout_row_push(ctx, 10); nk_spacer(ctx);
            nk_layout_row_end(ctx);
            if (row == 0) row_spacer(ctx, 4);
        }
    }

    row_spacer(ctx, 4);
    nk_layout_row_begin(ctx, NK_STATIC, 28, 3);
    nk_layout_row_push(ctx, 12); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 24);
    ui_toggle_row(a, "Sidebar", &a->show_sidebar);
    nk_layout_row_push(ctx, 12); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    row_spacer(ctx, 24);
    caption(ctx, w, "Nuklear v4.13.3", t->dim);
    row_spacer(ctx, 2);
    caption(ctx, w, "Win32 GDI backend", t->dim);
}

/* ------------------------------------------------------------------------- */
/*  Tab strip                                                                */
/* ------------------------------------------------------------------------- */
static void draw_tabs(struct App *a, float w, float h)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    const struct nk_user_font *f = &a->font_ui->nk;
    char name[160], info[128];
    int line, col, lines;
    float tw;

    float ox, oy;

    nk_layout_row_dynamic(ctx, h, 1);
    nk_spacer(ctx);
    group_origin(ctx, &ox, &oy);

    /* active tab chip */
    snprintf(name, sizeof(name), "%s", a->file_name[0] ? a->file_name : "untitled.c");
    tw = text_w(f, name);
    {
        struct nk_rect chip;
        float fh = f->height;
        chip.x = ox + 10; chip.y = oy + 7; chip.h = 24;
        chip.w = 14 + tw + (a->dirty ? 16.0f : 0.0f) + 14;
        nk_fill_rect(cv, chip, 6.0f, t->editor);
        draw_str(ctx, chip.x + 14, chip.y + (chip.h - fh) * 0.5f, tw + 2, name,
                 a->dirty ? t->text : mix(t->text, t->dim, 0.35f));
        if (a->dirty) {
            struct nk_rect dot;
            dot.x = chip.x + chip.w - 18; dot.y = chip.y + 9; dot.w = 7; dot.h = 7;
            nk_fill_circle(cv, dot, t->accent);
        }
    }

    /* right hand information */
    caret_pos(a, &line, &col, &lines);
    snprintf(info, sizeof(info), "Ln %d, Col %d    %d lines    UTF-8    LF", line, col, lines);
    tw = text_w(f, info);
    draw_str(ctx, ox + w - 16 - tw, oy + (h - f->height) * 0.5f, tw + 4, info, t->dim);
}

/* ------------------------------------------------------------------------- */
/*  Editor with line-number gutter                                           */
/* ------------------------------------------------------------------------- */
static void draw_gutter(struct App *a, float x, float y, float w, float h, float scroll_y)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    const struct nk_user_font *f = &a->font_mono->nk;
    struct nk_rect strip;
    float row_h = f->height + ctx->style.edit.row_padding;
    float pad_y = ctx->style.edit.padding.y;
    int first, visible, i, cur_line, col, total;
    char num[16];

    strip.x = x; strip.y = y; strip.w = w; strip.h = h;
    nk_fill_rect(cv, strip, 0.0f, t->gutter);
    nk_fill_rect(cv, nk_rect(x + w - 1, y, 1, h), 0.0f, t->border);

    if (!row_h) return;
    first = (int)(scroll_y / row_h);
    visible = (int)((h - 2.0f * pad_y) / row_h) + 2;
    caret_pos(a, &cur_line, &col, &total);

    nk_style_set_font(ctx, f);
    for (i = 0; i < visible; ++i) {
        int ln = first + i + 1;
        float tw, ly;
        if (ln > total) break;
        snprintf(num, sizeof(num), "%d", ln);
        tw = text_w(f, num);
        ly = y + pad_y - scroll_y + (float)i * row_h;
        if (ly < y - row_h || ly > y + h) continue;
        draw_str(ctx, x + w - 12 - tw, ly, tw + 2, num,
                 ln == cur_line ? t->accent : mix(t->dim, t->gutter, 0.35f));
    }
    nk_style_set_font(ctx, &a->font_ui->nk);
}

static void draw_editor(struct App *a, float x, float y, float w, float h)
{
    struct nk_context *ctx = a->ctx;
    (void)w; (void)x; (void)y;
    struct nk_text_edit *te = &ctx->text_edit;
    int was_active = te->active;
    float scroll_y;

    /* keep our own copy of the editing state while the widget is idle */
    if (!was_active) {
        te->cursor = a->caret;
        te->select_start = a->sel_start;
        te->select_end = a->sel_end;
        te->scrollbar = a->scroll;
    }

    nk_layout_row_dynamic(ctx, h, 1);
    nk_style_set_font(ctx, &a->font_mono->nk);
    /* NK_EDIT_EDITOR on its own stays in view mode; the editor must also be
     * told to always insert, otherwise typing is ignored. */
    nk_edit_buffer(ctx, NK_EDIT_EDITOR | NK_EDIT_ALWAYS_INSERT_MODE, te, nk_filter_default);
    nk_style_set_font(ctx, &a->font_ui->nk);

    if (te->active) {
        a->caret = te->cursor;
        a->sel_start = te->select_start;
        a->sel_end = te->select_end;
        a->scroll = te->scrollbar;
    }
    scroll_y = (float)te->scrollbar.y;

    if (a->show_gutter) {
        float ox, oy;
        group_origin(ctx, &ox, &oy);
        draw_gutter(a, ox, oy, GUTTER_W, h, scroll_y);
    }

    /* track dirty state */
    {
        unsigned int hsh = hash_text(doc_text(a), doc_len(a));
        a->dirty = (hsh != a->saved_hash);
    }
}

/* ------------------------------------------------------------------------- */
/*  Console                                                                  */
/* ------------------------------------------------------------------------- */
static void draw_console(struct App *a, float w, float h)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_command_buffer *cv;
    const struct nk_user_font *f = &a->font_mono->nk;
    float line_h = f->height + 2.0f;
    float y0, fy, pad;
    float ox, oy;
    int fit, start, i;
    char count[32];

    group_origin(ctx, &ox, &oy);

    /* header */
    nk_layout_row_begin(ctx, NK_STATIC, CONSOLE_HEAD_H, 4);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_push(ctx, 140);
    nk_style_set_font(ctx, &a->font_ui->nk);
    nk_label_colored(ctx, "BUILD / OUTPUT", NK_TEXT_LEFT, t->dim);
    nk_layout_row_push(ctx, w - 16 - 140 - 64 - 16);
    snprintf(count, sizeof(count), "%d entries", a->log_count);
    nk_label_colored(ctx, count, NK_TEXT_RIGHT, mix(t->dim, t->console, 0.35f));
    nk_layout_row_push(ctx, 64);
    if (ui_button(a, "Clear", 2)) log_clear(a);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);

    /* hairline under the console header */
    nk_layout_row_dynamic(ctx, 1, 1);
    nk_spacer(ctx);
    cv = nk_window_get_canvas(ctx);
    nk_fill_rect(cv, nk_rect(ox, oy + CONSOLE_HEAD_H, w, 1), 0.0f, t->border);

    /* log lines drawn by hand: exact baseline, colours, tiny time stamp */
    y0 = oy + CONSOLE_HEAD_H + 1.0f;
    pad = 10.0f;
    fy = y0 + pad;
    fit = (int)((oy + h - y0 - pad) / line_h);
    if (fit < 1) fit = 1;
    start = a->log_count - fit;
    if (start < 0) start = 0;

    nk_style_set_font(ctx, f);
    for (i = start; i < a->log_count; ++i) {
        const struct LogLine *l = &a->log[i];
        struct nk_color c;
        float ly = fy + (float)(i - start) * line_h;
        if (ly + line_h > oy + h) break;
        switch (l->level) {
            case LOG_OK:   c = t->green; break;
            case LOG_WARN: c = t->amber; break;
            case LOG_ERR:  c = t->red;   break;
            case LOG_CMD:  c = t->accent; break;
            default:       c = t->text;  break;
        }
        draw_str(ctx, ox + 16.0f, ly, 62.0f, l->time, mix(t->dim, t->console, 0.45f));
        draw_str(ctx, ox + 82.0f, ly, w - 82.0f - 16.0f, l->text, c);
    }
    nk_style_set_font(ctx, &a->font_ui->nk);

    if (!a->log_count) {
        draw_str(ctx, ox + 16.0f, fy, w - 32.0f,
                 "Press Run (F5) to build and execute the document.", t->dim);
    }
}

/* ------------------------------------------------------------------------- */
/*  Status bar                                                               */
/* ------------------------------------------------------------------------- */
static void draw_status(struct App *a, float w, float h)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
    char right[160];
    char msg[192];
    int line, col, lines;
    struct nk_color dot = t->accent;
    float ox, oy;

    group_origin(ctx, &ox, &oy);
    caret_pos(a, &line, &col, &lines);
    if (GetTickCount() > a->status_until) {
        snprintf(msg, sizeof(msg), a->dirty ? "Modified" : "Ready");
        a->status_kind = a->dirty ? 2 : 1;
    } else snprintf(msg, sizeof(msg), "%s", a->status);

    switch (a->status_kind) {
        case 1: dot = t->green; break;
        case 2: dot = t->amber; break;
        case 3: dot = t->red;   break;
        default: dot = t->accent; break;
    }
    nk_fill_circle(cv, nk_rect(ox + 16, oy + (h - 8) * 0.5f + 1.0f, 8, 8), dot);

    snprintf(right, sizeof(right), "Ln %d, Col %d     %d lines     %d chars",
             line, col, lines, doc_len(a));

    nk_layout_row_begin(ctx, NK_STATIC, h, 4);
    nk_layout_row_push(ctx, 32); nk_spacer(ctx);
    nk_layout_row_push(ctx, w - 32 - 300 - 16);
    nk_label_colored(ctx, msg, NK_TEXT_LEFT, mix(t->text, t->dim, 0.25f));
    nk_layout_row_push(ctx, 300);
    nk_label_colored(ctx, right, NK_TEXT_RIGHT, t->dim);
    nk_layout_row_push(ctx, 16); nk_spacer(ctx);
    nk_layout_row_end(ctx);
}

/* ------------------------------------------------------------------------- */
/*  Frame                                                                    */
/* ------------------------------------------------------------------------- */
static void apply_font_size(struct App *a)
{
    if (a->font_mono) { nk_gdifont_del(a->font_mono); a->font_mono = NULL; }
    a->font_mono = nk_gdifont_create("Consolas", a->font_size);
    if (!a->font_mono) a->font_mono = nk_gdifont_create("Courier New", a->font_size);
    if (a->font_mono) nk_gdi_set_font(a->font_mono);
    if (a->font_ui) nk_gdi_set_font(a->font_ui);
    a->scroll = a->ctx->text_edit.scrollbar;
}

static void draw_ui(struct App *a, float W, float H)
{
    struct nk_context *ctx = a->ctx;
    const struct Theme *t = a->th;
    float body_y, body_h, sb_w, cx, cw;
    float tabs_h, console_h, editor_h;
    int old_size = a->font_size;

    if (W < 200) W = 200;
    if (H < 160) H = 160;

    /* root panel background == separator colour: the 1px gaps between the
     * panels below read as hairlines.                                    */
    ctx->style.window.fixed_background = nk_style_item_color(t->border);
    if (nk_begin(ctx, "##root", nk_rect(0, 0, W, H),
                 NK_WINDOW_NO_SCROLLBAR | (a->pending ? NK_WINDOW_ROM : 0))) {

        body_y = HEADER_H + 1.0f;
        body_h = H - body_y - STATUS_H - 1.0f;
        if (body_h < 40.0f) body_h = 40.0f;
        sb_w = (a->show_sidebar && W > 640.0f) ? NK_MIN(SIDEBAR_W, W * 0.34f) : 0.0f;
        cx = sb_w > 0 ? sb_w + 1.0f : 0.0f;
        cw = NK_MAX(60.0f, W - cx);

        /* ---------------- header ---------------- */
        ctx->style.window.fixed_background = nk_style_item_color(t->panel);
        nk_layout_row_dynamic(ctx, HEADER_H, 1);
        if (nk_group_begin(ctx, "header", NK_WINDOW_NO_SCROLLBAR)) {
            draw_header(a, W);
            nk_group_end(ctx);
        }

        /* 1 px hairline */
        nk_layout_row_dynamic(ctx, 1, 1);
        nk_spacer(ctx);

        /* ---------------- body ---------------- */
        nk_layout_row_begin(ctx, NK_STATIC, body_h, sb_w > 0 ? 3 : 1);

        if (sb_w > 0) {
            nk_layout_row_push(ctx, sb_w);
            ctx->style.window.fixed_background = nk_style_item_color(t->panel_alt);
            if (nk_group_begin(ctx, "sidebar", NK_WINDOW_NO_SCROLLBAR)) {
                draw_sidebar(a, sb_w, body_h);
                nk_group_end(ctx);
            }
            nk_layout_row_push(ctx, 1);
            nk_spacer(ctx);
        }

        nk_layout_row_push(ctx, cw);
        ctx->style.window.fixed_background = nk_style_item_color(t->editor);
        if (nk_group_begin(ctx, "content", NK_WINDOW_NO_SCROLLBAR)) {

            tabs_h = NK_MIN(TABS_H, NK_MAX(24.0f, body_h - 80.0f));
            console_h = a->show_console ? NK_MIN(CONSOLE_H, NK_MAX(0.0f, body_h * 0.45f)) : 0.0f;
            editor_h = NK_MAX(40.0f, body_h - tabs_h - console_h);

            /* tab strip */
            ctx->style.window.fixed_background = nk_style_item_color(t->panel);
            nk_layout_row_dynamic(ctx, tabs_h, 1);
            if (nk_group_begin(ctx, "tabs", NK_WINDOW_NO_SCROLLBAR)) {
                draw_tabs(a, cw, tabs_h);
                nk_group_end(ctx);
            }

            /* editor */
            ctx->style.window.fixed_background = nk_style_item_color(t->editor);
            nk_layout_row_dynamic(ctx, editor_h, 1);
            if (nk_group_begin(ctx, "editor", NK_WINDOW_NO_SCROLLBAR)) {
                draw_editor(a, cx, body_y + tabs_h, cw, editor_h);
                nk_group_end(ctx);
            }

            /* console */
            if (console_h > 0.0f) {
                ctx->style.window.fixed_background = nk_style_item_color(t->console);
                nk_layout_row_dynamic(ctx, console_h, 1);
                if (nk_group_begin(ctx, "console", NK_WINDOW_NO_SCROLLBAR)) {
                    nk_layout_row_dynamic(ctx, 1, 1); /* top hairline */
                    nk_spacer(ctx);
                    {
                        struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
                        float ox, oy;
                        group_origin(ctx, &ox, &oy);
                        nk_fill_rect(cv, nk_rect(ox, oy, cw, 1), 0.0f, t->border);
                    }
                    draw_console(a, cw, console_h - 1.0f);
                    nk_group_end(ctx);
                }
            }
            nk_group_end(ctx);
        }
        nk_layout_row_end(ctx);

        /* ---------------- status bar ---------------- */
        nk_layout_row_dynamic(ctx, 1, 1);
        nk_spacer(ctx);
        ctx->style.window.fixed_background = nk_style_item_color(t->status);
        nk_layout_row_dynamic(ctx, STATUS_H, 1);
        if (nk_group_begin(ctx, "status", NK_WINDOW_NO_SCROLLBAR)) {
            draw_status(a, W, STATUS_H);
            nk_group_end(ctx);
        }

        /* soft drop shadow behind the modal guard (GDI cannot blend, so the
         * shadow is faked with a few nested rounded rectangles) */
        if (a->pending) {
            struct nk_command_buffer *cv = nk_window_get_canvas(ctx);
            struct nk_rect dlg = nk_rect((W - 400.0f) * 0.5f, (H - 150.0f) * 0.5f, 400.0f, 150.0f);
            int i;
            for (i = 4; i >= 1; --i) {
                struct nk_rect sh;
                sh.x = dlg.x - i * 5.0f;
                sh.y = dlg.y - i * 5.0f + 3.0f;
                sh.w = dlg.w + i * 10.0f;
                sh.h = dlg.h + i * 10.0f;
                nk_fill_rect(cv, sh, 10.0f + i * 3.0f,
                             mix(t->bg, nk_rgb(0, 0, 0), t->dark ? 0.30f - i * 0.07f
                                                                 : 0.10f - i * 0.02f));
            }
        }
    }
    nk_end(ctx);

    /* modal guard for unsaved documents ---------------------------------- */
    if (a->pending) {
        const float pw = 400.0f, ph = 150.0f;
        char title[96], text[192];
        struct nk_rect r = nk_rect((W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph);

        snprintf(title, sizeof(title), "%s", "Unsaved changes");
        ctx->style.window.fixed_background = nk_style_item_color(t->panel);
        ctx->style.window.padding = nk_vec2(18, 16);
        ctx->style.window.border = 1.0f;
        ctx->style.window.border_color = t->border;
        ctx->style.window.rounding = 8.0f;
        if (nk_begin(ctx, "##prompt", r, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_style_set_font(ctx, &a->font_title->nk);
            nk_label_colored(ctx, title, NK_TEXT_LEFT, t->text);
            nk_style_set_font(ctx, &a->font_ui->nk);
            nk_layout_row_dynamic(ctx, 20, 1);
            snprintf(text, sizeof(text), "Save changes to %s before continuing?",
                     a->file_name[0] ? a->file_name : "untitled.c");
            nk_label_colored(ctx, text, NK_TEXT_LEFT, t->dim);
            nk_layout_row_dynamic(ctx, 16, 1);
            nk_spacer(ctx);

            nk_layout_row_begin(ctx, NK_STATIC, 30, 7);
            nk_layout_row_push(ctx, 16); nk_spacer(ctx);
            nk_layout_row_push(ctx, 96);
            if (ui_button(a, "Save", 1)) {
                int act = a->pending;
                if (doc_save(a, 0)) {
                    a->pending = 0;
                    if (act == 1) doc_new(a);
                    else if (act == 2) doc_open_dialog(a);
                    else if (act == 3) a->quit = 1;
                }
            }
            nk_layout_row_push(ctx, 8); nk_spacer(ctx);
            nk_layout_row_push(ctx, 96);
            if (ui_button(a, "Discard", 2)) {
                int act = a->pending;
                a->pending = 0;
                a->dirty = 0;
                if (act == 1) doc_new(a);
                else if (act == 2) doc_open_dialog(a);
                else if (act == 3) a->quit = 1;
            }
            nk_layout_row_push(ctx, 8); nk_spacer(ctx);
            nk_layout_row_push(ctx, 96);
            if (ui_button(a, "Cancel", 2)) a->pending = 0;
            nk_layout_row_push(ctx, 16); nk_spacer(ctx);
            nk_layout_row_end(ctx);
        }
        nk_end(ctx);
        ctx->style.window.padding = nk_vec2(0, 0);
        ctx->style.window.border = 0.0f;
        ctx->style.window.rounding = 0.0f;
    }

    /* react to the font-size spinner after the frame was recorded */
    if (a->font_size != old_size) apply_font_size(a);
}

/* ------------------------------------------------------------------------- */
/*  Input shortcuts                                                          */
/* ------------------------------------------------------------------------- */
static int key_pressed(int vk)
{
    static unsigned char prev[256];
    int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    int r = down && !prev[vk];
    prev[vk] = (unsigned char)down;
    return r;
}

static void handle_shortcuts(struct App *a)
{
    int ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    int shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (a->pending) return;
    if (ctrl && key_pressed('S')) doc_save(a, shift);
    else if (ctrl && key_pressed('O')) request_action(a, 2);
    else if (ctrl && key_pressed('N')) request_action(a, 1);
    if (key_pressed(VK_F5)) doc_run(a);
}

/* ------------------------------------------------------------------------- */
/*  Window plumbing                                                          */
/* ------------------------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wpar, LPARAM lpar)
{
    switch (msg) {
        case WM_CLOSE:
            if (g_app.dirty && !g_app.pending) { g_app.pending = 3; return 0; }
            return DefWindowProcW(wnd, msg, wpar, lpar);
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wpar;
            wchar_t wpath[MAX_PATH * 2];
            char path[MAX_PATH * 3];
            if (DragQueryFileW(drop, 0, wpath, MAX_PATH * 2)) {
                wide_to_utf8(wpath, path, sizeof(path));
                if (g_app.dirty) set_status(&g_app, 2, "Save or discard changes first");
                else doc_load(&g_app, path);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SIZE:
            InvalidateRect(wnd, NULL, FALSE);
            break;
        case WM_ERASEBKGND:
            return 1;
    }
    if (nk_gdi_handle_event(wnd, msg, wpar, lpar))
        return 0;
    return DefWindowProcW(wnd, msg, wpar, lpar);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSW wc;
    HWND hwnd;
    RECT rc;
    struct App *a = &g_app;
    MSG msg;
    DWORD last;
    GdiFont *ui, *mono, *title;

    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    SetProcessDPIAware();

    ZeroMemory(a, sizeof(*a));
    a->font_size = 16;
    a->show_sidebar = 1;
    a->show_console = 1;
    a->show_gutter = 1;
    a->theme_idx = 0;
    a->th = &THEMES[0];
    a->buffer = (char *)calloc(1, EDIT_CAP);
    if (!a->buffer) return 1;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"NuklearStudioClass";
    RegisterClassW(&wc);

    {
        HDC screen = GetDC(NULL);
        int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);
        if (dpi <= 0) dpi = 96;
        rc.left = 0; rc.top = 0;
        rc.right = DEFAULT_W * dpi / 96;
        rc.bottom = DEFAULT_H * dpi / 96;
    }
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowW(L"NuklearStudioClass", APP_TITLE,
                         WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top,
                         NULL, NULL, hInstance, NULL);
    if (!hwnd) return 1;
    a->hwnd = hwnd;
    DragAcceptFiles(hwnd, TRUE);

    ui = nk_gdifont_create("Segoe UI", 15);
    if (!ui) ui = nk_gdifont_create("Tahoma", 15);
    mono = nk_gdifont_create("Consolas", a->font_size);
    if (!mono) mono = nk_gdifont_create("Courier New", a->font_size);
    title = nk_gdifont_create("Segoe UI Semibold", 18);
    if (!title) title = nk_gdifont_create("Segoe UI", 18);
    a->font_ui = ui;
    a->font_mono = mono;
    a->font_title = title;

    a->ctx = nk_gdi_init(ui, GetDC(hwnd), DEFAULT_W, DEFAULT_H);
    /* nk_gdi_init() only initialises the user-font struct of the font it is
     * given, so the others are set up through nk_gdi_set_font().           */
    nk_gdi_set_font(mono);
    nk_gdi_set_font(title);
    nk_gdi_set_font(ui);

    nk_str_init_fixed(&a->ctx->text_edit.string, a->buffer, EDIT_CAP);
    apply_theme(a);
    doc_new(a);
    log_clear(a);
    log_add(a, LOG_INFO, "Nuklear Studio ready. Drop a file here or press Ctrl+O.");
    log_add(a, LOG_INFO, "Note: Run is simulated - wire up CreateProcess to call a real compiler.");
    set_status(a, 1, "Ready");

    last = GetTickCount();
    while (!a->quit) {
        RECT cr;
        float W, H;

        /* Nuklear needs its input state mirrored every frame: all nk_input_*()
         * calls made by the window procedure land between begin and end. */
        nk_input_begin(a->ctx);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            int click = 0;
            if (msg.message == WM_QUIT) { a->quit = 1; break; }
            if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONUP ||
                msg.message == WM_RBUTTONDOWN || msg.message == WM_RBUTTONUP)
                click = 1;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            /* a press and its release must not be swallowed by the same frame,
             * otherwise quick clicks are lost: render as soon as one arrives */
            if (click) break;
        }
        if (a->quit) break;
        nk_input_end(a->ctx);

        handle_shortcuts(a);

        /* keep the caption in sync with the document */
        {
            char want[220];
            snprintf(want, sizeof(want), "%s%s%s  -  Nuklear Studio",
                     a->dirty ? "*" : "",
                     a->file_path[0] ? a->file_name : "untitled.c",
                     a->dirty ? " (modified)" : "");
            if (strcmp(want, a->win_title) != 0) {
                wchar_t wtext[240];
                snprintf(a->win_title, sizeof(a->win_title), "%s", want);
                utf8_to_wide(want, wtext, 240);
                SetWindowTextW(hwnd, wtext);
            }
        }

        GetClientRect(hwnd, &cr);
        W = (float)(cr.right - cr.left);
        H = (float)(cr.bottom - cr.top);

        draw_ui(a, W, H);
        nk_gdi_render(a->th->bg);

        /* ~60 fps cap so an idle window is not a busy loop */
        {
            DWORD now = GetTickCount();
            DWORD spent = now - last;
            if (spent < 16) Sleep(16 - spent);
            last = GetTickCount();
        }
    }

    nk_gdifont_del(mono);
    nk_gdifont_del(title);
    nk_gdifont_del(ui);
    nk_gdi_shutdown();
    free(a->buffer);
    return 0;
}
