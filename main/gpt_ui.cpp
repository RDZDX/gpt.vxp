#include "gpt_ui.h"
#include "Console_io.h"
#include "thread.h"
#include "vmeditor.h"
#include "vmstdlib.h"
#include "vmtimer.h"
#include <string.h>
#include <time.h>
#include "vmio.h"

#define VM_CHSET_DEST_BYTES 4096                      /* vm_chset_convert target max bytes (<=4096 per spec) */
#define UCS2_CHUNK_WORDS    (VM_CHSET_DEST_BYTES / 2) /* 2048 words */

static const int history_buffer_bytes = 16384;
static const int input_buffer_bytes = 8192;
static const int input_height = 64;

static VMUWCHAR history_buffer[history_buffer_bytes / sizeof(VMUWCHAR)];
static VMUWCHAR input_buffer[input_buffer_bytes / sizeof(VMUWCHAR)];
static VMUWCHAR input_text[input_buffer_bytes / sizeof(VMUWCHAR)];
static VMUWCHAR input_label[16];
static VMUWCHAR history_label[16];
static VMUWCHAR send_label[16];
static VMUWCHAR save_label[16];
static VMUWCHAR clear_label[16];
static vm_input_mode_enum input_modes[] = {
	VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC,
	VM_INPUT_MODE_123,
	VM_INPUT_MODE_123_SYMBOLS,
	VM_INPUT_MODE_NONE
};

static VMINT screen_width;
static VMINT screen_height;
static VMINT layer_handle = -1;
static VMINT history_editor;
static VMINT input_editor;
static VMINT history_height;
static VMINT history_length;
static VMINT request_timer = -1;
VMWCHAR outfile[100] = {0};

static vm_editor_font_attribute editor_font = {0, 0, 0, 16};

static VMUINT32 ime_callback(VMINT32 handle, vm_editor_message_struct_p message)
{
	(void)handle;
	(void)message;
	return 0;
}

static void advance_request_thread(VMINT timer_id)
{
	(void)timer_id;
	thread_next();
}

static void activate_input()
{
	if (history_editor) {
		vm_editor_set_pos(history_editor, 0, 0);
		vm_editor_set_size(history_editor, screen_width, history_height);
	}
	if (input_editor) {
		vm_editor_set_pos(input_editor, 0, history_height);
		vm_editor_set_size(input_editor, screen_width, input_height);
	}
	if (input_editor)
		vm_editor_activate(input_editor, VM_FALSE);
	if (history_editor)
		vm_editor_show(history_editor);
	if (input_editor)
		vm_editor_show(input_editor);
}

static void activate_history()
{
	if (history_editor) {
		vm_editor_set_pos(history_editor, 0, 0);
		vm_editor_set_size(history_editor, screen_width, screen_height - vm_editor_get_softkey_height());
	}
	if (input_editor) {
		vm_editor_set_pos(input_editor, 0, screen_height);
		vm_editor_set_size(input_editor, 0, 0);
	}
	if (history_editor)
		vm_editor_activate(history_editor, VM_FALSE);
	if (history_editor)
		vm_editor_show(history_editor);
}

static void clear_history()
{
//	history_length = 0;
//	history_buffer[0] = 0;
//	if (history_editor) {
//		vm_editor_set_text(history_editor, history_buffer, history_buffer_bytes);
//		vm_editor_show(history_editor);
//	}
}

void create_auto_filename(VMWSTR text) {

    VMINT drv;
    VMCHAR fAutoFileName[100] = {0};
    VMCHAR fData_text[100] = {0};
    struct vm_time_t curr_time;

    vm_get_time(&curr_time);

    if ((drv = vm_get_removable_driver()) < 0) {
       drv = vm_get_system_driver();
    }

    sprintf(fAutoFileName, "%c:\\", drv);
    sprintf(fData_text, "%02d%02d%02d%02d%02d.txt", curr_time.mon, curr_time.day, curr_time.hour, curr_time.min, curr_time.sec);
    strcat(fAutoFileName, fData_text);
    vm_ascii_to_ucs2(text, (strlen(fAutoFileName) + 1) * 2, fAutoFileName);
}

static VMINT save_history_to_utf8_chunked(VMWSTR outfile_path)
{
    if (!outfile_path)
        return -1;

    VMFILE fh = vm_file_open(outfile_path, MODE_CREATE_ALWAYS_WRITE, FALSE);

    if (fh < 0)
        return -2;

    VMWCHAR *u_chunk = (VMWCHAR *)vm_calloc((UCS2_CHUNK_WORDS + 4) * sizeof(VMWCHAR));

    VMCHAR *out_utf8 = (VMCHAR *)vm_calloc(VM_CHSET_DEST_BYTES + 8);

    if (!u_chunk || !out_utf8)
    {
        if (u_chunk)
            vm_free(u_chunk);

        if (out_utf8)
            vm_free(out_utf8);

        vm_file_close(fh);

        return -3;
    }

    VMINT history_len = vm_wstrlen((VMWSTR)history_buffer);

    VMINT remaining = history_len;
    VMINT pos = 0;

    while (remaining > 0)
    {
        VMINT take = (remaining > UCS2_CHUNK_WORDS) ? UCS2_CHUNK_WORDS : remaining;

        vm_wstrncpy(u_chunk, (VMWSTR)history_buffer + pos, take);

        u_chunk[take] = 0;

        VMINT conv = vm_chset_convert(VM_CHSET_UCS2, VM_CHSET_UTF8, (VMCHAR *)u_chunk, (VMCHAR *)out_utf8, VM_CHSET_DEST_BYTES);

        if (conv != VM_CHSET_CONVERT_SUCCESS &&
            conv != 0)
        {
            vm_free(u_chunk);
            vm_free(out_utf8);
            vm_file_close(fh);

            return -4;
        }

        VMUINT written = 0;

        vm_file_write(fh, out_utf8, (VMUINT)strlen((VMSTR)out_utf8), &written);

        pos += take;
        remaining -= take;
    }

    vm_free(u_chunk);
    vm_free(out_utf8);

    vm_file_close(fh);

    return 0;
}

void save_history(void)
{
    create_auto_filename(outfile);
    save_history_to_utf8_chunked(outfile);
}

static void append_character(char value)
{
	int capacity = history_buffer_bytes / sizeof(VMUWCHAR);

	if (value == '\b') {
		if (history_length > 0)
			history_buffer[--history_length] = 0;
		return;
	}

	if (value == '\r')
		return;
	if (value != '\n' && (unsigned char)value < 32)
		return;

	if (history_length >= capacity - 1) {
		int drop = 0;
		while (drop < history_length && history_buffer[drop] != '\n')
			++drop;
		if (drop < history_length)
			++drop;
		else
			drop = history_length / 2;
		memmove(history_buffer, history_buffer + drop,
			(history_length - drop) * sizeof(VMUWCHAR));
		history_length -= drop;
		history_buffer[history_length] = 0;
	}

	history_buffer[history_length++] = (unsigned char)value;
	history_buffer[history_length] = 0;
}

static void send_prompt()
{
	char prompt[4096];

	if (!input_editor)
		return;

	vm_editor_get_text(input_editor, input_text, sizeof(input_text));
	/* vm_ucs2_to_ascii() takes VMWSTR (signed short*) while editor buffers use
	 * VMUWCHAR (unsigned short); both are 16-bit UCS2 code units, so the cast
	 * only changes signedness, not representation, and is safe here. */
	vm_ucs2_to_ascii(prompt, sizeof(prompt), (VMWSTR)input_text);
	if (!prompt[0])
		return;

	input_buffer[0] = 0;
	vm_editor_set_text(input_editor, input_buffer, input_buffer_bytes);
	vm_editor_show(input_editor);

//	gpt_ui_write("Client: ");
        gpt_ui_write("Me: ");
	gpt_ui_write(prompt);
	console_str_out(prompt);
	console_str_out("\r\n");
}

void gpt_ui_init()
{
	/* vm_ascii_to_ucs2() takes VMWSTR (signed short*) while the label buffers
	 * use VMUWCHAR (unsigned short) to match vm_editor_* APIs; both are
	 * 16-bit UCS2 code units, so these casts only change signedness, not
	 * representation, and are safe here. */
	vm_ascii_to_ucs2((VMWSTR)input_label, sizeof(input_label), (VMSTR)"Back");         //left_label
	vm_ascii_to_ucs2((VMWSTR)history_label, sizeof(history_label), (VMSTR)"History");  //left_label
	vm_ascii_to_ucs2((VMWSTR)send_label, sizeof(send_label), (VMSTR)"Send");           //center_label
        vm_ascii_to_ucs2((VMWSTR)save_label, sizeof(send_label), (VMSTR)"Save");           //center_label
	vm_ascii_to_ucs2((VMWSTR)clear_label, sizeof(clear_label), (VMSTR)"Clear");        //right_label
}

void gpt_ui_write_n(const char* text, int length)
{
	int i;

	if (!text || length <= 0)
		return;

	for (i = 0; i < length; ++i)
		append_character(text[i]);

	if (history_editor) {
		vm_editor_set_text(history_editor, history_buffer, history_buffer_bytes);
		vm_editor_show(history_editor);
	}
}

void gpt_ui_write(const char* text)
{
	if (text)
		gpt_ui_write_n(text, strlen(text));
}

void gpt_ui_handle_sysevt(VMINT message, VMINT param)
{
	(void)param;

	switch (message) {
	case VM_MSG_CREATE:
	case VM_MSG_ACTIVE:
		screen_width = vm_graphic_get_screen_width();
		screen_height = vm_graphic_get_screen_height();
		history_height = screen_height - input_height - vm_editor_get_softkey_height();
		if (layer_handle < 0) {
			layer_handle = vm_graphic_create_layer(0, 0, screen_width, screen_height, -1);
			vm_graphic_set_clip(0, 0, screen_width, screen_height);
		}
		if (history_editor == 0) {
			history_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, 0,
				screen_width, history_height, history_buffer,
				history_buffer_bytes, VM_FALSE, layer_handle);
			if (history_editor) {
				vm_editor_set_bg_border_style(history_editor, VM_EDITOR_NO_BORDER, 0x001F, 0x001F);
				vm_editor_set_multiline_text_font(history_editor, editor_font);
				vm_editor_set_IME(history_editor, VM_INPUT_TYPE_SENTENCE, input_modes,
					VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_callback);
				vm_editor_set_softkey(history_editor, input_label, VM_LEFT_SOFTKEY, activate_input);
				vm_editor_set_softkey(history_editor, save_label, VM_CENTER_SOFTKEY, save_history);
				vm_editor_set_softkey(history_editor, clear_label, VM_RIGHT_SOFTKEY, clear_history);
			}
		}
		if (input_editor == 0) {
			input_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, history_height,
				screen_width, input_height, input_buffer,
				input_buffer_bytes, VM_FALSE, layer_handle);
			if (input_editor) {
				vm_editor_set_bg_border_style(input_editor, VM_EDITOR_DOUBLE_BORDER, 0x07E0, 0x07E0);
				vm_editor_set_multiline_text_font(input_editor, editor_font);
				vm_editor_set_IME(input_editor, VM_INPUT_TYPE_MULTITAP_SENTENCE,
					input_modes, VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_callback);
				vm_editor_set_softkey(input_editor, history_label, VM_LEFT_SOFTKEY, activate_history);
				vm_editor_set_softkey(input_editor, send_label, VM_CENTER_SOFTKEY, send_prompt);
				vm_editor_set_softkey(input_editor, clear_label, VM_RIGHT_SOFTKEY, clear_history);
				vm_editor_activate(input_editor, VM_FALSE);
			}
		}
		vm_switch_power_saving_mode(turn_off_mode);
		if (request_timer < 0)
			request_timer = vm_create_timer(1000 / 15, advance_request_thread);
		break;

	case VM_MSG_PAINT:
		if (history_editor)
			vm_editor_show(history_editor);
		if (input_editor)
			vm_editor_show(input_editor);
		break;

	case VM_MSG_INACTIVE:
		vm_switch_power_saving_mode(turn_on_mode);
		if (request_timer >= 0) {
			vm_delete_timer(request_timer);
			request_timer = -1;
		}
		if (input_editor) {
			vm_editor_deactivate(input_editor);
			vm_editor_close(input_editor);
			input_editor = 0;
		}
		if (history_editor) {
			vm_editor_close(history_editor);
			history_editor = 0;
		}
		if (layer_handle >= 0) {
			vm_graphic_delete_layer(layer_handle);
			layer_handle = -1;
		}
		break;

	case VM_MSG_QUIT:
		if (request_timer >= 0) {
			vm_delete_timer(request_timer);
			request_timer = -1;
		}
		if (input_editor) {
			vm_editor_deactivate(input_editor);
			vm_editor_close(input_editor);
			input_editor = 0;
		}
		if (history_editor) {
			vm_editor_close(history_editor);
			history_editor = 0;
		}
		if (layer_handle >= 0) {
			vm_graphic_delete_layer(layer_handle);
			layer_handle = -1;
		}
		break;
	}
}

