#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <PR/ultratypes.h>
#include "fs.h"
#include "config.h"
#include "system.h"
#include "utils.h"

#define CONFIG_MAX_STR 512
#define CONFIG_MAX_SECNAME 128
#define CONFIG_MAX_KEYNAME 256
#define CONFIG_MAX_SETTINGS 128

struct configentry {
	char key[CONFIG_MAX_KEYNAME + 1];
	char strval[CONFIG_MAX_STR + 1];
	s32 s32val;
	f32 f32val;
	s32 seclen;
} settings[CONFIG_MAX_SETTINGS + 1];
static s32 numSettings = 0;

static inline struct configentry *configFindEntry(const char *key)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (!strncasecmp(settings[i].key, key, CONFIG_MAX_KEYNAME)) {
			return &settings[i];
		}
	}
	return NULL;
}

static inline struct configentry *configAddEntry(const char *key)
{
	if (numSettings < CONFIG_MAX_SETTINGS) {
		struct configentry *cfg = &settings[numSettings++];
		strncpy(cfg->key, key, CONFIG_MAX_KEYNAME);
		const char *delim = strrchr(cfg->key, '.');
		cfg->seclen = delim ? (delim - cfg->key) : 0;
		return cfg;
	}
	return NULL;
}

static inline struct configentry *configFindOrAddEntry(const char *key)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (!strncasecmp(settings[i].key, key, CONFIG_MAX_KEYNAME)) {
			return &settings[i];
		}
	}
	return configAddEntry(key);
}

static inline const char *configGetSection(char *sec, const struct configentry *cfg)
{
	if (!cfg->seclen || cfg->seclen > CONFIG_MAX_SECNAME) {
		strncpy(sec, cfg->key, CONFIG_MAX_SECNAME);
		sec[CONFIG_MAX_SECNAME] = '\0';
		return sec;
	}

	memcpy(sec, cfg->key, cfg->seclen);
	sec[cfg->seclen] = '\0';

	return sec;
}

void configSetInt(const char *key, s32 val)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->s32val = val;
		snprintf(cfg->strval, CONFIG_MAX_STR, "%d", val);
	}
}

void configSetFloat(const char *key, f32 val)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->f32val = val;
		snprintf(cfg->strval, CONFIG_MAX_STR, "%f", val);
	}
}

void configSetString(const char *key, const char *val)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		strncpy(cfg->strval, val, CONFIG_MAX_STR);
	}
}

void configSetFromString(const char *key, const char *val)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		strncpy(cfg->strval, val, CONFIG_MAX_STR);
		cfg->f32val = atof(val);
		cfg->s32val = atoi(val);
	}
}

s32 configGetInt(const char *key, s32 defval)
{
	struct configentry *cfg = configFindEntry(key);
	if (cfg) {
		return cfg->s32val;
	}

	cfg = configAddEntry(key);
	if (cfg) {
		cfg->s32val = defval;
		snprintf(cfg->strval, CONFIG_MAX_STR, "%d", defval);
	}

	return defval;
}

f32 configGetFloat(const char *key, f32 defval)
{
	struct configentry *cfg = configFindEntry(key);
	if (cfg) {
		return cfg->f32val;
	}

	cfg = configAddEntry(key);
	if (cfg) {
		cfg->f32val = defval;
		snprintf(cfg->strval, CONFIG_MAX_STR, "%f", defval);
	}

	return defval;
}

const char *configGetString(const char *key, const char *defval)
{
	struct configentry *cfg = configFindEntry(key);
	if (cfg) {
		return cfg->strval;
	}

	cfg = configAddEntry(key);
	if (cfg) {
		strncpy(cfg->strval, defval, CONFIG_MAX_STR);
	}

	return defval;
}

s32 configGetIntClamped(const char *key, s32 defval, s32 minval, s32 maxval)
{
	const s32 ret = configGetInt(key, defval);
	return (ret < minval) ? minval : ((ret > maxval) ? maxval : ret);
}

f32 configGetFloatClamped(const char *key, f32 defval, f32 minval, f32 maxval)
{
	const f32 ret = configGetFloat(key, defval);
	return (ret < minval) ? minval : ((ret > maxval) ? maxval : ret);
}

static s32 configCmp(const struct configentry *a, const struct configentry *b) {
	char tmpa[CONFIG_MAX_SECNAME + 1];
	char tmpb[CONFIG_MAX_SECNAME + 1];
	configGetSection(tmpa, a);
	configGetSection(tmpb, b);

	// compare section names first
	const s32 seccmp = strncmp(tmpa, tmpb, CONFIG_MAX_SECNAME);
	if (seccmp) {
		return seccmp;
	}

	// same section; sort keys by first letter
	return a->key[a->seclen + 1] - b->key[b->seclen + 1];
}

s32 configSave(const char *fname)
{
	FILE *f = fsFileOpenWrite(fname);
	if (!f) {
		return 0;
	}

	// sort the config so that the sections appear in order
	qsort(settings, numSettings, sizeof(*settings), (void *)configCmp);

	char tmpSec[CONFIG_MAX_SECNAME + 1] = { 0 };
	char curSec[CONFIG_MAX_SECNAME + 1] = { 0 };
	configGetSection(curSec, &settings[0]);
	fprintf(f, "[%s]\n", curSec);

	for (s32 i = 0; i < numSettings; ++i) {
		struct configentry *cfg = &settings[i];
		configGetSection(tmpSec, cfg);
		if (strncmp(curSec, tmpSec, CONFIG_MAX_SECNAME) != 0) {
			fprintf(f, "\n[%s]\n", tmpSec);
			strncpy(curSec, tmpSec, CONFIG_MAX_SECNAME);
		}
		fprintf(f, "%s=%s\n", cfg->key + cfg->seclen + 1, cfg->strval);
	}

	fsFileClose(f);
	return 1;
}

s32 configLoad(const char *fname)
{
	FILE *f = fsFileOpenRead(fname);
	if (!f) {
		return 0;
	}

	char curSec[CONFIG_MAX_SECNAME + 1] = { 0 };
	char keyBuf[CONFIG_MAX_SECNAME * 2 + 2] = { 0 }; // SECTION + . + KEY + \0
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char lineBuf[2048] = { 0 };
	char *line = lineBuf;
	s32 lineLen = 0;

	while (fgets(lineBuf, sizeof(lineBuf), f)) {
		line = lineBuf;

		line = strParseToken(line, token, NULL);

		if (token[0] == '[' && token[1] == '\0') {
			// section; get name
			line = strParseToken(line, token, NULL);
			if (!token[0]) {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed section line: %s", lineBuf);
				continue;
			}
			strncpy(curSec, token, CONFIG_MAX_SECNAME);
			// eat ]
			line = strParseToken(line, token, NULL);
			if (token[0] != ']' || token[1] != '\0') {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed section line: %s", lineBuf);
			}
		} else if (token[0]) {
			// probably a key=value pair; append key name to section name
			snprintf(keyBuf, sizeof(keyBuf) - 1, "%s.%s", curSec, token);
			// eat =
			line = strParseToken(line, token, NULL);
			if (token[0] != '=' || token[1] != '\0') {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed keyvalue line: %s", lineBuf);
				continue;
			}
			// the rest of the line is the value
			line = strTrim(line);
			if (line[0] == '"') {
				line = strUnquote(line);
			}
			configSetFromString(keyBuf, line);
		}
	}

	fsFileClose(f);

	return 1;
}

void configInit(void)
{
	if (fsFileSize(CONFIG_PATH) > 0) {
		configLoad(CONFIG_PATH);
	}
}
