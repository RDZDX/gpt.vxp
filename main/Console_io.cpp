#include "Console_io.h"
#include "CircleBuf.h"
#include "gpt_ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern CircleBuf circlebuf;

void console_str_in(const char* str){
	gpt_ui_write(str);
}

void console_str_out(const char* str){
	for(unsigned int i = 0; i < strlen(str); i++)
		circlebuf.push(str[i]);
}

int cprintf(const char* format, ...) {
	static char buf[1024 * 4];
	va_list aptr;

	va_start(aptr, format);
	int ret = vsprintf(buf, format, aptr);
	va_end(aptr);

	gpt_ui_write(buf);
	return ret;
}