/* cJSON — minimal JSON parser/generator. MIT license.
   Subset sufficient for Qaryx: parse responses from yt-dlp/history,
   generate status/history JSON for Android. */
#include "cjson.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* ── Allocator ────────────────────────────────────────────────────────────── */
static cJSON *new_item(void) {
    cJSON *n = calloc(1, sizeof(cJSON));
    return n;
}

static void suffix_object(cJSON *prev, cJSON *item) {
    prev->next = item; item->prev = prev;
}

void cJSON_Delete(cJSON *c) {
    cJSON *next;
    while (c) {
        next = c->next;
        if (c->child) cJSON_Delete(c->child);
        free(c->valuestring);
        free(c->string);
        free(c);
        c = next;
    }
}

/* ── Parser ───────────────────────────────────────────────────────────────── */
static const char *skip_ws(const char *s) {
    while (s && *s && (unsigned char)*s <= ' ') s++;
    return s;
}

static const char *parse_string_raw(const char *s, char **out) {
    if (*s != '"') return NULL;
    s++;
    const char *b = s;
    size_t len = 0;
    while (*s && *s != '"') { if (*s == '\\') s++; s++; len++; }
    char *p = malloc(len + 1), *w = p;
    s = b;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            switch (*s) {
                case '"': *w++='"'; break;
                case '\\': *w++='\\'; break;
                case '/': *w++='/'; break;
                case 'n': *w++='\n'; break;
                case 'r': *w++='\r'; break;
                case 't': *w++='\t'; break;
                default: *w++=*s; break;
            }
        } else { *w++=*s; }
        s++;
    }
    *w = '\0';
    *out = p;
    return (*s == '"') ? s+1 : NULL;
}

static const char *parse_value(cJSON *item, const char *s);

static const char *parse_array(cJSON *item, const char *s) {
    item->type = CJSON_ARRAY;
    s = skip_ws(s);
    if (*s == ']') return s+1;
    cJSON *child = new_item();
    item->child = child;
    s = parse_value(child, s);
    while (s && (s=skip_ws(s)) && *s == ',') {
        cJSON *n = new_item();
        suffix_object(child, n);
        child = n;
        s = parse_value(child, skip_ws(s+1));
    }
    return (s && *s==']') ? s+1 : NULL;
}

static const char *parse_object(cJSON *item, const char *s) {
    item->type = CJSON_OBJECT;
    s = skip_ws(s);
    if (*s == '}') return s+1;
    cJSON *child = new_item();
    item->child = child;
    s = parse_string_raw(s, &child->string);
    if (!s) return NULL;
    s = skip_ws(s);
    if (*s != ':') return NULL;
    s = parse_value(child, skip_ws(s+1));
    while (s && (s=skip_ws(s)) && *s == ',') {
        cJSON *n = new_item();
        suffix_object(child, n);
        child = n;
        s = skip_ws(s+1);
        s = parse_string_raw(s, &child->string);
        if (!s) return NULL;
        s = skip_ws(s);
        if (*s != ':') return NULL;
        s = parse_value(child, skip_ws(s+1));
    }
    return (s && *s=='}') ? s+1 : NULL;
}

static const char *parse_value(cJSON *item, const char *s) {
    s = skip_ws(s);
    if (!s) return NULL;
    if (*s == '"') {
        item->type = CJSON_STRING;
        return parse_string_raw(s, &item->valuestring);
    }
    if (*s == '[') return parse_array(item, skip_ws(s+1));
    if (*s == '{') return parse_object(item, skip_ws(s+1));
    if (!strncmp(s,"true",4)){item->type=CJSON_BOOL;item->valuebool=1;item->valueint=1;return s+4;}
    if (!strncmp(s,"false",5)){item->type=CJSON_BOOL;item->valuebool=0;item->valueint=0;return s+5;}
    if (!strncmp(s,"null",4)){item->type=CJSON_NULL;return s+4;}
    /* number */
    char *ep;
    double d = strtod(s, &ep);
    if (s == ep) return NULL;
    item->type = CJSON_NUMBER;
    item->valuedouble = d;
    item->valueint = (int)d;
    return ep;
}

cJSON *cJSON_Parse(const char *json) {
    if (!json) return NULL;
    cJSON *c = new_item();
    if (!parse_value(c, skip_ws(json))) { cJSON_Delete(c); return NULL; }
    return c;
}

/* ── Accessors ────────────────────────────────────────────────────────────── */
cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *key) {
    if (!obj || obj->type != CJSON_OBJECT) return NULL;
    cJSON *c = obj->child;
    while (c) { if (c->string && !strcmp(c->string, key)) return c; c=c->next; }
    return NULL;
}

int cJSON_GetArraySize(const cJSON *arr) {
    if (!arr || arr->type != CJSON_ARRAY) return 0;
    int n=0; cJSON *c=arr->child; while(c){n++;c=c->next;} return n;
}

cJSON *cJSON_GetArrayItem(const cJSON *arr, int idx) {
    if (!arr || arr->type != CJSON_ARRAY) return NULL;
    cJSON *c=arr->child; while(c&&idx>0){c=c->next;idx--;} return c;
}

const char *cJSON_GetString(const cJSON *obj, const char *key, const char *def) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return (v && v->type==CJSON_STRING && v->valuestring) ? v->valuestring : def;
}

double cJSON_GetNumber(const cJSON *obj, const char *key, double def) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return (v && v->type==CJSON_NUMBER) ? v->valuedouble : def;
}

int cJSON_GetBool(const cJSON *obj, const char *key, int def) {
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return (v && v->type==CJSON_BOOL) ? v->valuebool : def;
}

/* ── Builder ──────────────────────────────────────────────────────────────── */
cJSON *cJSON_CreateObject(void){cJSON*n=new_item();n->type=CJSON_OBJECT;return n;}
cJSON *cJSON_CreateArray(void){cJSON*n=new_item();n->type=CJSON_ARRAY;return n;}
cJSON *cJSON_CreateNull(void){cJSON*n=new_item();n->type=CJSON_NULL;return n;}
cJSON *cJSON_CreateBool(int b){cJSON*n=new_item();n->type=CJSON_BOOL;n->valuebool=b;n->valueint=b;return n;}
cJSON *cJSON_CreateNumber(double d){cJSON*n=new_item();n->type=CJSON_NUMBER;n->valuedouble=d;n->valueint=(int)d;return n;}
cJSON *cJSON_CreateString(const char *s){cJSON*n=new_item();n->type=CJSON_STRING;n->valuestring=strdup(s?s:"");return n;}

static void attach_child(cJSON *parent, cJSON *child) {
    if (!parent->child) { parent->child=child; return; }
    cJSON *c=parent->child; while(c->next)c=c->next; suffix_object(c,child);
}

void cJSON_AddItemToObject(cJSON *obj, const char *key, cJSON *item) {
    if (!item) return;
    free(item->string); item->string=strdup(key);
    attach_child(obj, item);
}

void cJSON_AddItemToArray(cJSON *arr, cJSON *item) { attach_child(arr, item); }

void cJSON_AddStringToObject(cJSON *o,const char *k,const char *v){cJSON_AddItemToObject(o,k,cJSON_CreateString(v));}
void cJSON_AddNumberToObject(cJSON *o,const char *k,double v){cJSON_AddItemToObject(o,k,cJSON_CreateNumber(v));}
void cJSON_AddBoolToObject(cJSON *o,const char *k,int v){cJSON_AddItemToObject(o,k,cJSON_CreateBool(v));}

/* ── Printer ──────────────────────────────────────────────────────────────── */
static void print_str_escaped(FILE *f, const char *s) {
    fputc('"',f);
    for (;*s;s++){
        switch(*s){
            case '"': fputs("\\\"",f); break;
            case '\\': fputs("\\\\",f); break;
            case '\n': fputs("\\n",f); break;
            case '\r': fputs("\\r",f); break;
            case '\t': fputs("\\t",f); break;
            default: fputc(*s,f); break;
        }
    }
    fputc('"',f);
}

static void print_item(FILE *f, const cJSON *c, int depth) {
    if (!c) return;
    switch(c->type) {
        case CJSON_NULL:   fputs("null",f); break;
        case CJSON_BOOL:   fputs(c->valuebool?"true":"false",f); break;
        case CJSON_NUMBER:
            if (c->valuedouble == (int)c->valuedouble)
                fprintf(f,"%d",(int)c->valuedouble);
            else
                fprintf(f,"%g",c->valuedouble);
            break;
        case CJSON_STRING: print_str_escaped(f, c->valuestring?c->valuestring:""); break;
        case CJSON_ARRAY: {
            fputc('[',f);
            cJSON *ch=c->child; int first=1;
            while(ch){if(!first)fputc(',',f);print_item(f,ch,depth+1);ch=ch->next;first=0;}
            fputc(']',f);
            break;
        }
        case CJSON_OBJECT: {
            fputc('{',f);
            cJSON *ch=c->child; int first=1;
            while(ch){
                if(!first)fputc(',',f);
                print_str_escaped(f,ch->string?ch->string:"");
                fputc(':',f);
                print_item(f,ch,depth+1);
                ch=ch->next; first=0;
            }
            fputc('}',f);
            break;
        }
    }
}

char *cJSON_Print(const cJSON *item) {
    char *buf; size_t sz;
    FILE *f = open_memstream(&buf, &sz);
    if (!f) return NULL;
    print_item(f, item, 0);
    fclose(f);
    return buf;  /* caller must free() */
}
