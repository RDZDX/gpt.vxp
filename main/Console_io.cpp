#include "Console_io.h"
#include "CircleBuf.h"
#include "gpt_ui.h"
extern CircleBuf circlebuf;

void console_char_in(char ch){
	gpt_ui_write_n(&ch, 1);
}

void console_str_in(const char* str){
	gpt_ui_write(str);
}

void console_str_with_length_in(const char* str, int length){
	gpt_ui_write_n(str, length);
}

void console_char_out(char ch){
	circlebuf.push(ch);
}

void console_str_out(const char* str){
	for(unsigned int i = 0; i < strlen(str); i++)
		console_char_out(str[i]);
}

void console_str_with_length_out(const char* str, int length){
	for(unsigned int i = 0; i < length; i++)
		console_char_out(str[i]);
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