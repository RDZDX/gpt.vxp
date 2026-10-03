#include "main.h"
#include "CircleBuf.h"
#include "chatgpt.h"
#include "gpt_ui.h"
#include "thread.h"
#include "config.h"

CircleBuf circlebuf;

void vm_main(void)
{
	thread_init();
	circlebuf.init(8192);
	gpt_ui_init();
	vm_reg_sysevt_callback(gpt_ui_handle_sysevt);
	thread_create(0xFFFF, main_gpt);
}
