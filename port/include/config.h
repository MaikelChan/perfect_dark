#pragma once

#include <PR/ultratypes.h>

#define CONFIG_FNAME "./pd.ini"

void configInit(void);

// loads config from file (path extensions such as ! apply)
s32 configLoad(const char *fname);

// saves config to file (path extensions such as ! apply)
s32 configSave(const char *fname);

// get value of a config variable or create a new one with specified default
// key is a string of the form "SECTION.KEY"
s32 configGetInt(const char *key, s32 defval);
f32 configGetFloat(const char *key, f32 defval);
const char *configGetString(const char *key, const char *defval);

// set value of a config variable, will create it if it doesn't exist
// key is a string of the form "SECTION.KEY"
void configSetInt(const char *key, s32 val);
void configSetFloat(const char *key, f32 val);
void configSetString(const char *key, const char *val);
