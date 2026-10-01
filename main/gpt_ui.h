#pragma once

#include "main.h"

void gpt_ui_init();
void gpt_ui_handle_sysevt(VMINT message, VMINT param);
void gpt_ui_write(const char* text);
void gpt_ui_write_n(const char* text, int length);
