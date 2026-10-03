#pragma once

int config_load(void);   /* 1 = loaded, 0 = file missing */
const char* config_get(const char* section, const char* name, const char* def);
void config_unload(void);
void config_show_all(void);
void config_show_value(const char* section, const char* name);
