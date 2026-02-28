/* cJSON — minimal JSON parser/generator. MIT license. */
#pragma once
#include <stddef.h>

#define CJSON_NUMBER  1
#define CJSON_STRING  2
#define CJSON_ARRAY   3
#define CJSON_OBJECT  4
#define CJSON_BOOL    5
#define CJSON_NULL    6

typedef struct cJSON {
    struct cJSON *next, *prev, *child;
    int    type;
    char  *string;    /* key name */
    char  *valuestring;
    int    valueint;
    double valuedouble;
    int    valuebool;
} cJSON;

/* Parse null-terminated JSON string. Returns root node or NULL. */
cJSON *cJSON_Parse(const char *json);

/* Free a parsed tree. */
void cJSON_Delete(cJSON *item);

/* Object access. */
cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *key);

/* Array access. */
int    cJSON_GetArraySize(const cJSON *arr);
cJSON *cJSON_GetArrayItem(const cJSON *arr, int idx);

/* Value helpers. */
const char *cJSON_GetString(const cJSON *obj, const char *key, const char *def);
double      cJSON_GetNumber(const cJSON *obj, const char *key, double def);
int         cJSON_GetBool  (const cJSON *obj, const char *key, int def);

/* ── Builder ─────────────────────────────────────────────────────────────── */
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *s);
cJSON *cJSON_CreateNumber(double n);
cJSON *cJSON_CreateBool(int b);
cJSON *cJSON_CreateNull(void);

void cJSON_AddItemToObject(cJSON *obj, const char *key, cJSON *item);
void cJSON_AddItemToArray(cJSON *arr, cJSON *item);

/* Shorthand adders. */
void cJSON_AddStringToObject(cJSON *obj, const char *key, const char *val);
void cJSON_AddNumberToObject(cJSON *obj, const char *key, double val);
void cJSON_AddBoolToObject(cJSON *obj, const char *key, int val);

/* Serialize to heap-allocated string. Caller must free(). */
char *cJSON_Print(const cJSON *item);
