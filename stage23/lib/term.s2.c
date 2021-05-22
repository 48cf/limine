#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/term.h>
#include <lib/real.h>
#include <lib/image.h>
#include <lib/blib.h>
#include <drivers/vga_textmode.h>

#define TERM_TABSIZE (8)

int current_video_mode = -1;

int term_backend = NOT_READY;

void (*raw_putchar)(uint8_t c);
void (*clear)(bool move);
void (*enable_cursor)(void);
void (*disable_cursor)(void);
void (*set_cursor_pos)(int x, int y);
void (*get_cursor_pos)(int *x, int *y);
void (*set_text_fg)(int fg);
void (*set_text_bg)(int bg);

void (*term_double_buffer)(bool status);
void (*term_double_buffer_flush)(void);

int term_rows, term_cols;

#if defined (bios)
void term_textmode(void) {
    term_backend = NOT_READY;

    init_vga_textmode(&term_rows, &term_cols, true);

    raw_putchar    = text_putchar;
    clear          = text_clear;
    enable_cursor  = text_enable_cursor;
    disable_cursor = text_disable_cursor;
    set_cursor_pos = text_set_cursor_pos;
    get_cursor_pos = text_get_cursor_pos;
    set_text_fg    = text_set_text_fg;
    set_text_bg    = text_set_text_bg;

    term_double_buffer       = text_double_buffer;
    term_double_buffer_flush = text_double_buffer_flush;

    term_backend = TEXTMODE;
}
#endif

void term_deinit(void) {
    term_backend = NOT_READY;
}

static void term_putchar(uint8_t c);

void term_write(const char *buf, size_t count) {
    if (term_backend == NOT_READY)
        return;
    for (size_t i = 0; i < count; i++)
        term_putchar(buf[i]);
}

static int get_cursor_pos_x(void) {
    int x, y;
    get_cursor_pos(&x, &y);
    return x;
}

static int get_cursor_pos_y(void) {
    int x, y;
    get_cursor_pos(&x, &y);
    return y;
}

static void escape_parse(uint8_t c);

static int escape = 0;
static int esc_value0 = 0;
static int esc_value1 = 0;
static int *esc_value = &esc_value0;
static int esc_default0 = 1;
static int esc_default1 = 1;
static int *esc_default = &esc_default0;

static void term_putchar(uint8_t c) {
    if (escape) {
        escape_parse(c);
        return;
    }
    switch (c) {
        case 0x00:
            break;
        case '\e':
            escape = 1;
            return;
        case '\t':
            if ((get_cursor_pos_x() / TERM_TABSIZE + 1) >= term_cols)
                break;
            set_cursor_pos((get_cursor_pos_x() / TERM_TABSIZE + 1) * TERM_TABSIZE, get_cursor_pos_y());
            break;
        default:
            raw_putchar(c);
            break;
    }
}

static void sgr(void) {
    if (esc_value0 == 0){
        set_text_bg(8);
        set_text_fg(9);
        return;
    }

    if (esc_value0 >= 30 && esc_value0 <= 37) {
        set_text_fg(esc_value0 - 30);
        return;
    }

    if (esc_value0 >= 40 && esc_value0 <= 47) {
        set_text_bg(esc_value0 - 40);
        return;
    }
}

static void escape_parse(uint8_t c) {
    if (c >= '0' && c <= '9') {
        *esc_value *= 10;
        *esc_value += c - '0';
        *esc_default = 0;
        return;
    }

    switch (c) {
        case 0x1b:
            escape = 0;
            raw_putchar(0x1b);
            return;
        case '[':
            return;
        case ';':
            esc_value = &esc_value1;
            esc_default = &esc_default1;
            return;
        case 'A':
            if (esc_default0)
                esc_value0 = 1;
            if (esc_value0 > get_cursor_pos_y())
                esc_value0 = get_cursor_pos_y();
            set_cursor_pos(get_cursor_pos_x(),
                                get_cursor_pos_y() - esc_value0);
            break;
        case 'B':
            if (esc_default0)
                esc_value0 = 1;
            if ((get_cursor_pos_y() + esc_value0) > (term_rows - 1))
                esc_value0 = (term_rows - 1) - get_cursor_pos_y();
            set_cursor_pos(get_cursor_pos_x(),
                                get_cursor_pos_y() + esc_value0);
            break;
        case 'C':
            if (esc_default0)
                esc_value0 = 1;
            if ((get_cursor_pos_x() + esc_value0) > (term_cols - 1))
                esc_value0 = (term_cols - 1) - get_cursor_pos_x();
            set_cursor_pos(get_cursor_pos_x() + esc_value0,
                                get_cursor_pos_y());
            break;
        case 'D':
            if (esc_default0)
                esc_value0 = 1;
            if (esc_value0 > get_cursor_pos_x())
                esc_value0 = get_cursor_pos_x();
            set_cursor_pos(get_cursor_pos_x() - esc_value0,
                                get_cursor_pos_y());
            break;
        case 'H':
            esc_value0--;
            esc_value1--;
            if (esc_default0)
                esc_value0 = 0;
            if (esc_default1)
                esc_value1 = 0;
            if (esc_value1 >= term_cols)
                esc_value1 = term_cols - 1;
            if (esc_value0 >= term_rows)
                esc_value0 = term_rows - 1;
            set_cursor_pos(esc_value1, esc_value0);
            break;
        case 'm':
            sgr();
            break;
        case 'J':
            switch (esc_value0) {
                case 2:
                    clear(false);
                    break;
                default:
                    break;
            }
            break;
        case 'K':
            switch (esc_value0) {
                case 2: {
                    int x = get_cursor_pos_x();
                    int y = get_cursor_pos_y();
                    set_cursor_pos(0, y);
                    for (int i = 0; i < term_cols; i++)
                        raw_putchar(' ');
                    set_cursor_pos(x, y);
                    break;
                }
            }
            break;
        default:
            escape = 0;
            raw_putchar('?');
            break;
    }

    esc_value = &esc_value0;
    esc_value0 = 0;
    esc_value1 = 0;
    esc_default = &esc_default0;
    esc_default0 = 1;
    esc_default1 = 1;
    escape = 0;
}
