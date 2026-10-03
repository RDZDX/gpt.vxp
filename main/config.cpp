#include "config.h"
#include "vmio.h"
#include "vmstdlib.h"
#include "vmchset.h"
#include "vmsys.h"
#include <string.h>
#include "gpt_ui.h"
#include <stdio.h>

#define CFG_MAX_ENTRIES 32
#define CFG_SEC_LEN     32
#define CFG_NAME_LEN    32
#define CFG_VALUE_LEN   256

/* defined in your project */
//void create_selfapp_txt_filename(VMWSTR out, const char* ext);

struct config_entry {
    char section[CFG_SEC_LEN];
    char name[CFG_NAME_LEN];
    char value[CFG_VALUE_LEN];
};

static config_entry entries[CFG_MAX_ENTRIES];
static int entry_count = 0;

static char* trim(char* s)
{
    char* e;
    while (*s == ' ' || *s == '\t')
        ++s;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == ';'))
        *--e = 0;
    return s;
}

static char* unquote(char* s)
{
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        s[n - 1] = 0;
        ++s;
    }
    return s;
}

static void copy_str(char* dst, size_t cap, const char* src)
{
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

static void parse_buffer(char* data)
{
    char section[CFG_SEC_LEN] = {0};
    char* line = data;

    while (line && *line && entry_count < CFG_MAX_ENTRIES) {
        char* next = strchr(line, '\n');
        char* eq;
        char* s;

        if (next)
            *next++ = 0;

        s = trim(line);

        if (*s && *s != '#' && *s != ';') {
            if (*s == '[') {
                char* end = strchr(s, ']');
                if (end) {
                    *end = 0;
                    copy_str(section, sizeof(section), trim(s + 1));
                }
            } else if ((eq = strchr(s, '=')) != NULL) {
                char* name;
                char* value;
                *eq = 0;
                name = trim(s);
                value = unquote(trim(eq + 1));
                if (*name) {
                    copy_str(entries[entry_count].section, CFG_SEC_LEN, section);
                    copy_str(entries[entry_count].name, CFG_NAME_LEN, name);
                    copy_str(entries[entry_count].value, CFG_VALUE_LEN, value);
                    ++entry_count;
                }
            }
        }
        line = next;
    }
}

void create_selfapp_txt_filename(VMWSTR text, const char* extt) {

    VMWCHAR fullPath[100] = {0};
    VMWCHAR wfile_extension[8] = {0};

    vm_get_exec_filename(fullPath);
    vm_ascii_to_ucs2(wfile_extension, 8, (VMSTR)extt);
    vm_wstrncpy(text, fullPath, vm_wstrlen(fullPath) - 3);
    vm_wstrcat(text, wfile_extension);

}

int config_load(void)
{
    VMWCHAR path[100] = {0};
    VMFILE fp;
    VMUINT size = 0, nread = 0;
    char* data;

    config_unload();
    create_selfapp_txt_filename(path, "txt");

    fp = vm_file_open(path, MODE_READ, FALSE);
    if (fp < 0)
        return 0;

    vm_file_seek(fp, 0, BASE_END);
    size = vm_file_tell(fp);
    vm_file_seek(fp, 0, BASE_BEGIN);

    data = (char*)vm_malloc(size + 1);
    if (!data) {
        vm_file_close(fp);
        return 0;
    }

    vm_file_read(fp, data, size, &nread);
    data[nread] = 0;
    vm_file_close(fp);

char* p = data;
if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
    p += 3;
parse_buffer(p);

//    parse_buffer(data);
    vm_free(data);          /* values are copied, so no global buffer or flag is needed */
    return 1;
}

const char* config_get(const char* section, const char* name, const char* def)
{
    int i;
    if (!section)
        section = "";
    for (i = 0; i < entry_count; ++i) {
        if (strcmp(entries[i].section, section) == 0 &&
            strcmp(entries[i].name, name) == 0)
            return entries[i].value;
    }
    return def;
}

void config_unload(void)
{
    memset(entries, 0, sizeof(entries));
    entry_count = 0;
}



/* Mask secrets: show first 3 and last 2 chars only, e.g. sk-****************ab */
static void mask_value(const char* in, char* out, size_t cap)
{
    size_t n = strlen(in);
    size_t i;

    if (cap == 0)
        return;
    if (n <= 5) {
        for (i = 0; i < n && i < cap - 1; ++i)
            out[i] = '*';
        out[i] = 0;
        return;
    }
    if (n > cap - 1)
        n = cap - 1;
    for (i = 0; i < n; ++i)
        out[i] = (i < 3 || i >= n - 2) ? in[i] : '*';
    out[n] = 0;
}

static int is_secret(const char* section, const char* name)
{
    return strcmp(section, "key") == 0 || strcmp(name, "key") == 0;
}

static void print_entry(const config_entry& e)
{
    char shown[CFG_VALUE_LEN];
    char line[CFG_SEC_LEN + CFG_NAME_LEN + CFG_VALUE_LEN + 16];

    if (is_secret(e.section, e.name))
        mask_value(e.value, shown, sizeof(shown));
    else
        copy_str(shown, sizeof(shown), e.value);

    if (e.section[0])
        snprintf(line, sizeof(line), "[%s] %s = \"%s\"\n", e.section, e.name, shown);
    else
        snprintf(line, sizeof(line), "%s = \"%s\"\n", e.name, shown);

    gpt_ui_write(line);
}

void config_show_all(void)
{
    int i;

    if (entry_count == 0) {
        gpt_ui_write("No settings loaded. Please load ini file.\n");
        return;
    }
    for (i = 0; i < entry_count; ++i)
        print_entry(entries[i]);
}

void config_show_value(const char* section, const char* name)
{
    int i;
    char line[CFG_SEC_LEN + CFG_NAME_LEN + 40];

    if (!section)
        section = "";

    for (i = 0; i < entry_count; ++i) {
        if (strcmp(entries[i].section, section) == 0 &&
            strcmp(entries[i].name, name) == 0) {
            print_entry(entries[i]);
            return;
        }
    }

    snprintf(line, sizeof(line), "Parameter not found: %s%s%s\n",
             section, section[0] ? "/" : "", name);
    gpt_ui_write(line);
}
