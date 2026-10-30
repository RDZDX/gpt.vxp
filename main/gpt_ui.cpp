#include <string.h>
#include <time.h>

#include "Console_io.h"
#include "config.h"
#include "gpt_ui.h"
#include "thread.h"
#include "vmeditor.h"
#include "vmio.h"
#include "vmstdlib.h"
#include "vmtimer.h"

#define VM_CHSET_DEST_BYTES 4096 /* vm_chset_convert target max bytes (<=4096 per spec) */
#define UCS2_CHUNK_WORDS (VM_CHSET_DEST_BYTES / 2) /* 2048 words */

static const int input_height = 64;

#define INITIAL_HISTORY_BYTES 4096
#define MAX_HISTORY_BYTES (1024 * 1024)  // 1500 kb ram
//#define MAX_HISTORY_BYTES (2048 * 1024) //3250 kb ram
#define INITIAL_INPUT_BYTES 4096
#define MAX_INPUT_BYTES (64 * 1024)  // 1500 kb ram
//#define MAX_INPUT_BYTES   (128 * 1024) //3250 kb ram

static VMUWSTR history_buffer = NULL;
static VMINT history_buffer_bytes = 0;
static VMINT history_length = 0;

static VMUWSTR input_buffer = NULL;
static VMINT input_buffer_bytes = 0;

static VMUWCHAR input_label[16];
static VMUWCHAR history_label[16];
static VMUWCHAR send_label[16];
static VMUWCHAR save_label[16];
static VMUWCHAR clear_label[16];
static vm_input_mode_enum input_modes[] = {VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, VM_INPUT_MODE_123,
    VM_INPUT_MODE_123_SYMBOLS, VM_INPUT_MODE_NONE};

static VMINT screen_width;
static VMINT screen_height;
static VMINT layer_handle = -1;
static VMINT history_editor = 0;
static VMINT input_editor = 0;
static VMINT history_height;
static VMINT request_timer = -1;
VMWCHAR outfile[100] = {0};

static VMUWCHAR open_label[16];

static VMINT file_selector_cb(VMWCHAR* filepath, VMINT wlen);
static VMINT load_utf8_file_to_input(VMWSTR filepath);
static VMINT ensure_input_capacity_bytes(VMINT needed_bytes);
static VMINT ensure_history_capacity_bytes(VMINT needed_bytes);

extern bool waiting_for_api_key;
extern char key[256];

static vm_editor_font_attribute editor_font = {0, 0, 0, 16};
static VMBOOL history_mode = VM_FALSE;

void open_text_file(void);
void gpt_ui_init();
static void history_make_room(int need);
void func_center_key(void);
void func_left_key(void);
void func_right_key(void);

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
    history_mode = VM_FALSE;

    if (history_editor) {
        vm_editor_set_pos(history_editor, 0, 0);
        vm_editor_set_size(history_editor, screen_width, history_height);
    }
    if (input_editor) {
        vm_editor_set_pos(input_editor, 0, history_height);
        vm_editor_set_size(input_editor, screen_width, input_height);
        vm_editor_set_softkey(input_editor, history_label, VM_LEFT_SOFTKEY, func_left_key);
        vm_editor_set_softkey(input_editor, send_label, VM_CENTER_SOFTKEY, func_center_key);
        vm_editor_set_softkey(input_editor, open_label, VM_RIGHT_SOFTKEY, func_right_key);
    }
    if (input_editor) vm_editor_activate(input_editor, VM_FALSE);
    if (history_editor) vm_editor_show(history_editor);
    if (input_editor) vm_editor_show(input_editor);
}

static void activate_history()
{
    history_mode = VM_TRUE;

    if (history_editor) {
        vm_editor_set_pos(history_editor, 0, 0);
        vm_editor_set_size(history_editor, screen_width, screen_height - vm_editor_get_softkey_height());
        vm_editor_set_softkey(history_editor, input_label, VM_LEFT_SOFTKEY, func_left_key);
        vm_editor_set_softkey(history_editor, save_label, VM_CENTER_SOFTKEY, func_center_key);
        vm_editor_set_softkey(history_editor, clear_label, VM_RIGHT_SOFTKEY, func_right_key);
    }
    if (input_editor) {
        vm_editor_set_pos(input_editor, 0, screen_height);
        vm_editor_set_size(input_editor, 0, 0);
    }
    if (history_editor) vm_editor_activate(history_editor, VM_FALSE);
    if (history_editor) vm_editor_show(history_editor);

    vm_editor_redraw_ime_screen();
}

static void clear_history()
{
    //	history_length = 0;
    //	history_buffer[0] = 0;
    //	if (history_editor) {
    //		vm_editor_set_text(history_editor, history_buffer,
    //history_buffer_bytes); 		vm_editor_show(history_editor);
    //	}
}

void create_auto_filename(VMWSTR text)
{
    VMINT drv;
    VMCHAR fAutoFileName[100] = {0};
    VMCHAR fData_text[100] = {0};
    struct vm_time_t curr_time;

    vm_get_time(&curr_time);

    if ((drv = vm_get_removable_driver()) < 0) {
        drv = vm_get_system_driver();
    }

    sprintf(fAutoFileName, "%c:\\", drv);
    sprintf(fData_text, "%02d%02d%02d%02d%02d.txt", curr_time.mon,
            curr_time.day, curr_time.hour, curr_time.min, curr_time.sec);
    strcat(fAutoFileName, fData_text);
    vm_ascii_to_ucs2(text, (strlen(fAutoFileName) + 1) * 2, fAutoFileName);
}

static VMINT save_history_to_utf8_chunked(VMWSTR outfile_path)
{
    if (!outfile_path) return -1;

    VMFILE fh = vm_file_open(outfile_path, MODE_CREATE_ALWAYS_WRITE, FALSE);

    if (fh < 0) return -2;

    VMWCHAR* u_chunk = (VMWCHAR*)vm_calloc((UCS2_CHUNK_WORDS + 4) * sizeof(VMWCHAR));
    VMCHAR* out_utf8 = (VMCHAR*)vm_calloc(VM_CHSET_DEST_BYTES + 8);

    if (!u_chunk || !out_utf8) {
        if (u_chunk) vm_free(u_chunk);

        if (out_utf8) vm_free(out_utf8);

        vm_file_close(fh);

        return -3;
    }

    VMINT history_len = history_length;

    VMINT remaining = history_len;
    VMINT pos = 0;

    while (remaining > 0) {
        VMINT take = (remaining > UCS2_CHUNK_WORDS) ? UCS2_CHUNK_WORDS : remaining;
        vm_wstrncpy(u_chunk, (VMWSTR)history_buffer + pos, take);
        u_chunk[take] = 0;
        VMINT conv = vm_chset_convert(VM_CHSET_UCS2, VM_CHSET_UTF8, (VMCHAR*)u_chunk,
                             (VMCHAR*)out_utf8, VM_CHSET_DEST_BYTES);

        if (conv != VM_CHSET_CONVERT_SUCCESS && conv != 0) {
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

static void send_prompt()
{
    if (!input_editor) return;

    static VMWCHAR input_text[2048];

    vm_editor_get_text(input_editor, (VMUWSTR)input_text, sizeof(input_text));

    static char prompt[4096];
    static char line[4096 + 16];

    memset(prompt, 0, sizeof(prompt));

    vm_chset_convert(VM_CHSET_UCS2, VM_CHSET_UTF8, (VMCHAR*)input_text,
                     (VMCHAR*)prompt, sizeof(prompt));

    if (!prompt[0]) {
        return;
    }

    input_buffer[0] = 0;

    if (input_editor) {
        vm_editor_set_text(input_editor, input_buffer, input_buffer_bytes);
        vm_editor_show(input_editor);
        vm_editor_redraw_ime_screen();
    }

    /* one combined write to the history editor */
    snprintf(line, sizeof(line), "Me: %s\n", prompt);
    gpt_ui_write(line);

    if (waiting_for_api_key) {
        strncpy(key, prompt, 255);
        key[255] = '\0';

        config_save_key(key);

        waiting_for_api_key = false;

        gpt_ui_write("API key accepted.\n" "You can now enter prompts.\n");

        return;
    }
    /* one combined write to the request thread */
    snprintf(line, sizeof(line), "%s\r\n", prompt);
    console_str_out(line);
}

void gpt_ui_init()
{
    ensure_history_capacity_bytes(INITIAL_HISTORY_BYTES);
    ensure_input_capacity_bytes(INITIAL_INPUT_BYTES);

    if (history_buffer) history_buffer[0] = 0;

    if (input_buffer) input_buffer[0] = 0;

    vm_ascii_to_ucs2((VMWSTR)input_label, sizeof(input_label), (VMSTR) "Back");  // left_label
    vm_ascii_to_ucs2((VMWSTR)history_label, sizeof(history_label), (VMSTR) "History");  // left_label
    vm_ascii_to_ucs2((VMWSTR)send_label, sizeof(send_label), (VMSTR) "Send");  // center_label
    vm_ascii_to_ucs2((VMWSTR)save_label, sizeof(save_label), (VMSTR) "Save");  // center_label
    vm_ascii_to_ucs2((VMWSTR)clear_label, sizeof(clear_label), (VMSTR) "Clear");  // right_label
    vm_ascii_to_ucs2((VMWSTR)open_label, sizeof(open_label), (VMSTR) "Open");
}

static void append_utf8_to_history(const char* text, int len)
{
    char src[513];
    VMWCHAR u_chunk[1024];
    int in_off = 0;

    if (!text || len <= 0) return;

    while (in_off < len) {
        int n = len - in_off;
        if (n > 512) n = 512;

        /* don't cut a UTF-8 multibyte sequence */
        if (in_off + n < len) {
            while (n > 1 && (((unsigned char)text[in_off + n]) & 0xC0) == 0x80)
                --n;
        }

        memcpy(src, text + in_off, n);
        src[n] = 0;
        memset(u_chunk, 0, sizeof(u_chunk));

        vm_chset_convert(VM_CHSET_UTF8, VM_CHSET_UCS2, (VMCHAR*)src,
                         (VMCHAR*)u_chunk, sizeof(u_chunk));

        {
            int i;
            int count = vm_wstrlen(u_chunk);

            VMINT needed = (history_length + count + 1) * sizeof(VMWCHAR);

            if (ensure_history_capacity_bytes(needed) < 0) {
                history_make_room(count);

                needed = (history_length + count + 1) * sizeof(VMWCHAR);

                if (needed > history_buffer_bytes) return;
            }

            for (i = 0; i < count; ++i) {
                VMUWCHAR c = (VMUWCHAR)u_chunk[i];
                if (c == '\b') {
                    if (history_length > 0)
                        history_buffer[--history_length] = 0;
                    continue;
                }
                if (c == '\r') continue;
                if (c != '\n' && c < 32) continue;
                history_buffer[history_length++] = c;
            }
            history_buffer[history_length] = 0;
        }

        in_off += n;
    }
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

                if (!history_buffer && ensure_history_capacity_bytes(INITIAL_HISTORY_BYTES) < 0) {
                    return;
                }

                if (!input_buffer && ensure_input_capacity_bytes(INITIAL_INPUT_BYTES) < 0) {
                    return;
                }
            }
            if (history_editor == 0) {
                history_editor = vm_editor_create(
                    VM_EDITOR_MULTILINE, 0, 0, screen_width, history_height,
                    history_buffer, history_buffer_bytes, VM_FALSE, layer_handle);
                if (history_editor) {
                    vm_editor_set_bg_border_style(
                        history_editor, VM_EDITOR_NO_BORDER, 0x001F, 0x001F);
                    vm_editor_set_multiline_text_font(history_editor, editor_font);
                    vm_editor_set_IME(history_editor, VM_INPUT_TYPE_SENTENCE, input_modes,
                        VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_callback);
                    vm_editor_set_softkey(history_editor, input_label, VM_LEFT_SOFTKEY, func_left_key);
                    vm_editor_set_softkey(history_editor, save_label, VM_CENTER_SOFTKEY, func_center_key);
                    vm_editor_set_softkey(history_editor, clear_label, VM_RIGHT_SOFTKEY, func_right_key);
                }
            }
            if (input_editor == 0) {
                input_editor = vm_editor_create(
                    VM_EDITOR_MULTILINE, 0, history_height, screen_width,
                    input_height, input_buffer, input_buffer_bytes, VM_FALSE, layer_handle);

                if (input_editor) {
                    vm_editor_set_bg_border_style(
                        input_editor, VM_EDITOR_DOUBLE_BORDER, 0x0000, 0x0000);
                    vm_editor_set_multiline_text_font(input_editor, editor_font);
                    vm_editor_set_IME(input_editor, VM_INPUT_TYPE_MULTITAP_SENTENCE,
                        input_modes, VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_callback);
                    vm_editor_set_softkey(input_editor, history_label, VM_LEFT_SOFTKEY, func_left_key);
                    vm_editor_set_softkey(input_editor, send_label, VM_CENTER_SOFTKEY, func_center_key);
                    vm_editor_set_softkey(input_editor, open_label, VM_RIGHT_SOFTKEY, func_right_key);
                    vm_editor_activate(input_editor, VM_FALSE);
                }
            }
            vm_switch_power_saving_mode(turn_off_mode);
            if (request_timer < 0)
                request_timer = vm_create_timer_ex(1000 / 15, advance_request_thread);
            break;

        case VM_MSG_PAINT:
            if (history_editor) vm_editor_show(history_editor);
            if (input_editor) vm_editor_show(input_editor);
            break;

        case VM_MSG_INACTIVE:
            vm_switch_power_saving_mode(turn_on_mode);
            if (request_timer >= 0) {
                vm_delete_timer_ex(request_timer);
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
                vm_delete_timer_ex(request_timer);
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

            if (history_buffer) {
                vm_free(history_buffer);
                history_buffer = NULL;
            }

            if (input_buffer) {
                vm_free(input_buffer);
                input_buffer = NULL;
            }

            if (layer_handle >= 0) {
                vm_graphic_delete_layer(layer_handle);
                layer_handle = -1;
            }
            break;
    }
}

void gpt_ui_write_n(const char* text, int length)
{
    if (!text || length <= 0) return;

    append_utf8_to_history(text, length);

    if (history_editor) {
        vm_editor_set_text(history_editor, history_buffer, history_buffer_bytes);
        vm_editor_show(history_editor);
    }

    if (layer_handle >= 0) {
        VMINT layers[1];
        layers[0] = layer_handle;
        vm_graphic_flush_layer(layers, 1);
    }
}

void gpt_ui_write(const char* text)
{
    if (text) gpt_ui_write_n(text, strlen(text));
}

static VMINT ensure_history_capacity_bytes(VMINT needed_bytes)
{
    if (needed_bytes <= history_buffer_bytes) return 0;

    if (needed_bytes > MAX_HISTORY_BYTES) return -1;

    VMINT new_bytes = history_buffer_bytes ? history_buffer_bytes * 2 : INITIAL_HISTORY_BYTES;

    while (new_bytes < needed_bytes) {
        new_bytes *= 2;

        if (new_bytes > MAX_HISTORY_BYTES) {
            new_bytes = MAX_HISTORY_BYTES;
            break;
        }
    }

    if (!history_buffer) {
        history_buffer = (VMUWSTR)vm_malloc(new_bytes);

        if (!history_buffer) return -1;

        history_buffer[0] = 0;
    } else {
        void* p = vm_realloc(history_buffer, new_bytes);

        if (!p) return -1;

        history_buffer = (VMUWSTR)p;
    }

    history_buffer_bytes = new_bytes;

    return 0;
}

static VMINT ensure_input_capacity_bytes(VMINT needed_bytes)
{
    if (needed_bytes <= input_buffer_bytes) return 0;

    if (needed_bytes > MAX_INPUT_BYTES) return -1;

    VMINT new_bytes = input_buffer_bytes ? input_buffer_bytes * 2 : INITIAL_INPUT_BYTES;

    while (new_bytes < needed_bytes) {
        new_bytes *= 2;

        if (new_bytes > MAX_INPUT_BYTES) {
            new_bytes = MAX_INPUT_BYTES;
            break;
        }
    }

    if (!input_buffer) {
        input_buffer = (VMUWSTR)vm_malloc(new_bytes);

        if (!input_buffer) return -1;

        input_buffer[0] = 0;
    } else {
        void* p = vm_realloc(input_buffer, new_bytes);

        if (!p) return -1;

        input_buffer = (VMUWSTR)p;
    }

    input_buffer_bytes = new_bytes;

    return 0;
}

void open_text_file(void) { vm_selector_run(0, 0, file_selector_cb); }

static VMINT file_selector_cb(VMWCHAR* filepath, VMINT wlen)
{
    (void)wlen;

    if (filepath) {
        load_utf8_file_to_input(filepath);
    }

    return 0;
}

VMINT load_utf8_file_to_input(VMWSTR filepath)
{
    VMFILE fh = vm_file_open(filepath, MODE_READ, TRUE);

    if (fh < 0) return -1;

    VMUINT file_size = 0;

    if (vm_file_getfilesize(fh, &file_size) < 0) {
        vm_file_close(fh);
        return -2;
    }

    if (file_size > 4096) {
        vm_file_close(fh);
        return -5;
    }

    VMINT old_size = input_buffer_bytes;

    if (ensure_input_capacity_bytes(file_size * 2 + 1024) < 0) {
        vm_file_close(fh);
        return -3;
    }

    if (input_buffer_bytes != old_size) {
        if (input_editor) {
            vm_editor_close(input_editor);
            input_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, history_height,
                                 screen_width, input_height, input_buffer, input_buffer_bytes, VM_FALSE, layer_handle);

            if (input_editor) {
                vm_editor_set_bg_border_style(input_editor, VM_EDITOR_DOUBLE_BORDER, 0x0000, 0x0000);
                vm_editor_set_multiline_text_font(input_editor, editor_font);
                vm_editor_set_IME(input_editor, VM_INPUT_TYPE_MULTITAP_SENTENCE, input_modes,
                    VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, ime_callback);
                vm_editor_set_softkey(input_editor, history_label, VM_LEFT_SOFTKEY, func_left_key);
                vm_editor_set_softkey(input_editor, send_label, VM_CENTER_SOFTKEY, func_center_key);
                vm_editor_set_softkey(input_editor, open_label, VM_RIGHT_SOFTKEY, func_right_key);
                vm_editor_activate(input_editor, VM_FALSE);
                vm_editor_redraw_ime_screen();
            }
        }
    }

    char* utf8 = (char*)vm_malloc(file_size + 1);

    if (!utf8) {
        vm_file_close(fh);
        return -4;
    }

    VMUINT read = 0;

    vm_file_read(fh, utf8, file_size, &read);

    utf8[read] = 0;

    vm_file_close(fh);

    vm_chset_convert(VM_CHSET_UTF8, VM_CHSET_UCS2, utf8, (VMSTR)input_buffer, input_buffer_bytes);

    vm_free(utf8);

    if (input_editor) {
        vm_editor_set_text(input_editor, input_buffer, input_buffer_bytes);
        vm_editor_show(input_editor);
        vm_editor_redraw_ime_screen();
    }

    return 0;
}

static void history_make_room(int need)
{
    while ((history_length + need + 1) * sizeof(VMWCHAR) >
           history_buffer_bytes) {
        int drop = 0;

        while (drop < history_length && history_buffer[drop] != '\n') {
            ++drop;
        }

        if (drop < history_length) ++drop;

        if (drop <= 0) break;

        memmove(history_buffer, history_buffer + drop,
                (history_length - drop + 1) * sizeof(VMWCHAR));

        history_length -= drop;
    }
}

void func_center_key(void)
{
    if (history_mode) {
        save_history();
    } else {
        send_prompt();
    }
}

void func_left_key(void)
{
    if (history_mode) {
        activate_input();
    } else {
        activate_history();
    }
}

void func_right_key(void)
{
    if (history_mode) {
        clear_history();
    } else {
        open_text_file();
    }
}
