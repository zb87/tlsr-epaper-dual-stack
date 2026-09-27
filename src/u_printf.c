#include <stdarg.h>
#include "tl_common.h"
#include "u_printf.h"
#include "app_config.h"

extern void drv_putchar(unsigned char byte);

static int putchar_internal(int c) {
    drv_putchar((unsigned char)c);
    return c;
}

typedef struct {
    char *buf;
    unsigned int max_len;
    unsigned int count;
} print_ctx_t;

static void printchar(print_ctx_t *ctx, int c) {
    if (ctx && ctx->buf) {
        if (ctx->count + 1 < ctx->max_len) {
            ctx->buf[ctx->count] = (char)c;
        }
        ctx->count++;
    } else {
        (void)putchar_internal(c);
    }
}

#define PAD_RIGHT 1
#define PAD_ZERO 2

static int prints(print_ctx_t *ctx, const char *string, int width, int pad) {
    register int pc = 0, padchar = ' ';

    if (width > 0) {
        register int len = 0;
        register const char *ptr;
        for (ptr = string; *ptr; ++ptr)
            ++len;
        if (len >= width)
            width = 0;
        else
            width -= len;
        if (pad & PAD_ZERO)
            padchar = '0';
    }
    if (!(pad & PAD_RIGHT)) {
        for (; width > 0; --width) {
            printchar(ctx, padchar);
            ++pc;
        }
    }
    for (; *string; ++string) {
        printchar(ctx, *string);
        ++pc;
    }
    for (; width > 0; --width) {
        printchar(ctx, padchar);
        ++pc;
    }

    return pc;
}

#define PRINT_BUF_LEN 12

static int printi(print_ctx_t *ctx, int i, int b, int sg, int width, int pad, int letbase) {
    char print_buf[PRINT_BUF_LEN];
    register char *s;
    register int t, neg = 0, pc = 0;
    register unsigned int u = i;

    if (i == 0) {
        print_buf[0] = '0';
        print_buf[1] = '\0';
        return prints(ctx, print_buf, width, pad);
    }

    if (sg && b == 10 && i < 0) {
        neg = 1;
        u = -i;
    }

    s = print_buf + PRINT_BUF_LEN - 1;
    *s = '\0';

    while (u) {
        t = u % b;
        if (t >= 10)
            t += letbase - '0' - 10;
        *--s = t + '0';
        u /= b;
    }

    if (neg) {
        if (width && (pad & PAD_ZERO)) {
            printchar(ctx, '-');
            ++pc;
            --width;
        } else {
            *--s = '-';
        }
    }

    return pc + prints(ctx, s, width, pad);
}

static int print(print_ctx_t *ctx, const char *format, va_list args) {
    register int width, pad;
    register int pc = 0;
    char scr[2];

    for (; *format != 0; ++format) {
        if (*format == '%') {
            ++format;
            width = pad = 0;
            if (*format == '\0')
                break;
            if (*format == '%')
                goto out;
            if (*format == '-') {
                ++format;
                pad = PAD_RIGHT;
            }
            while (*format == '0') {
                ++format;
                pad |= PAD_ZERO;
            }
            for (; *format >= '0' && *format <= '9'; ++format) {
                width *= 10;
                width += *format - '0';
            }
            while (*format == 'l' || *format == 'h') {
                ++format;
            }
            if (*format == 's') {
                register char *s = (char *) va_arg(args, int);
                pc += prints(ctx, s ? s : "(null)", width, pad);
                continue;
            }
            if (*format == 'd') {
                pc += printi(ctx, va_arg(args, int), 10, 1, width, pad, 'a');
                continue;
            }
            if (*format == 'x') {
                pc += printi(ctx, va_arg(args, int), 16, 0, width, pad, 'a');
                continue;
            }
            if (*format == 'X') {
                pc += printi(ctx, va_arg(args, int), 16, 0, width, pad, 'A');
                continue;
            }
            if (*format == 'u') {
                pc += printi(ctx, va_arg(args, int), 10, 0, width, pad, 'a');
                continue;
            }
            if (*format == 'c') {
                scr[0] = (char) va_arg(args, int);
                scr[1] = '\0';
                pc += prints(ctx, scr, width, pad);
                continue;
            }
        } else {
        out:
            printchar(ctx, *format);
            ++pc;
        }
    }
    if (ctx && ctx->buf && ctx->max_len > 0) {
        if (ctx->count < ctx->max_len) {
            ctx->buf[ctx->count] = '\0';
        } else {
            ctx->buf[ctx->max_len - 1] = '\0';
        }
    }
    return pc;
}

int u_printf(const char *format, ...) {
#if DEBUG
    _attribute_custom_bss_ static char my_printf_buff[512];
    unsigned char r = irq_disable();
    print_ctx_t ctx = { .buf = my_printf_buff, .max_len = sizeof(my_printf_buff), .count = 0 };
    va_list args;
    va_start(args, format);
    int ret = print(&ctx, format, args);
    va_end(args);
    irq_restore(r);

    char *s = my_printf_buff;
    while (*s) {
        putchar_internal(*s++);
    }
    return ret;
#else
    (void)format;
    return 0;
#endif
}

int u_sprintf(char *out, const char *format, ...) {
    if (!out) return 0;
    print_ctx_t ctx = { .buf = out, .max_len = 0xFFFFFFFF, .count = 0 };
    va_list args;
    va_start(args, format);
    int ret = print(&ctx, format, args);
    va_end(args);
    return ret;
}

int u_snprintf(char *out, unsigned int max_len, const char *format, ...) {
    if (!out || max_len == 0) return 0;
    print_ctx_t ctx = { .buf = out, .max_len = max_len, .count = 0 };
    va_list args;
    va_start(args, format);
    int ret = print(&ctx, format, args);
    va_end(args);
    return ret;
}

int u_vsnprintf(char *out, unsigned int max_len, const char *format, va_list args) {
    if (!out || max_len == 0) return 0;
    print_ctx_t ctx = { .buf = out, .max_len = max_len, .count = 0 };
    return print(&ctx, format, args);
}

void u_array_printf(unsigned char *data, unsigned int len) {
    u_printf("{");
    for (int i = 0; i < len; ++i) {
        u_printf("%X%s", data[i], i < (len) - 1 ? ":" : "}");
    }
    u_printf("\n");
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && *h == *n) {
                ++h;
                ++n;
            }
            if (!*n) return (char *)haystack;
        }
    }
    return 0;
}
