#include <types.h>
#include <stdarg.h>

#include "vga/fb_console.h"
#include "vga/vga_console.h"
#include "console.h"
#include "device/rs232.h"

#define printf tfp_printf 
#define sprintf tfp_sprintf 

typedef void (*putcf) (void*, char);
void kprintf_format(void* putp, putcf putf, char *fmt, va_list va);

static bool fb_console_initialized;

/*
 * Outputs a character to the display.
 */
void console_putc(void* p, char c) {
	if(c == '\n') {
		(fb_console_initialized) ? fb_console_control(c) : vga_console_control(c);
		rs232_putchar(KERN_DEBUG_SERIAL_PORT, '\r');
		rs232_putchar(KERN_DEBUG_SERIAL_PORT, '\n');
	} else {
		rs232_putchar(KERN_DEBUG_SERIAL_PORT, c);
		(fb_console_initialized) ? fb_console_putchar(c) : vga_console_putchar(c);
	}
}

/*
 * Initialises kernel console
 */
void console_init() {
	vga_console_init();
	rs232_init();
}

/*
 * Switches to framebuffer console.
 */
void console_init_fb() {
	fb_console_initialized = true;
	fb_console_init();
}

/*
 * Implements printf for the kernel.
 */
void kprintf(char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	kprintf_format(NULL, console_putc, fmt, va);
	va_end(va);
}
