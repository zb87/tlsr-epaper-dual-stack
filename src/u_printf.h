#ifndef _U_PRINTF_H_
#define _U_PRINTF_H_

#include <stdarg.h>

int  u_printf(const char *fmt, ...);
int  u_sprintf(char* s, const char *fmt, ...);
int  u_snprintf(char* s, unsigned int max_len, const char *fmt, ...);
int  u_vsnprintf(char* s, unsigned int max_len, const char *fmt, va_list args);
void u_array_printf(unsigned char* data, unsigned int len);

#ifdef printf
#undef printf
#endif
#define printf          u_printf

#ifdef sprintf
#undef sprintf
#endif
#define sprintf         u_sprintf

#ifdef snprintf
#undef snprintf
#endif
#define snprintf        u_snprintf

#ifdef vsnprintf
#undef vsnprintf
#endif
#define vsnprintf       u_vsnprintf

#define array_printf    u_array_printf

#endif // _U_PRINTF_H_
