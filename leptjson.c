#include "leptjson.h"
#include <assert.h>     /* assert() */
#include <stdlib.h>     /* NULL, strtod()*/
#include <string.h>     /* strchr() */
#include <math.h>       /* HUGE_VAL */
#include <errno.h>      /* errno, ERANGE */

#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)
#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')

/* The JSON parsing context, i.e. the position where we currently parse */
typedef struct {
    const char* json;
}lept_context;

/* This function skips all whitespaces in JSON text until reaching a non-space literal or the end */
static void lept_parse_whitespace(lept_context* c) {
    const char *p = c->json;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    c->json = p;    /* update the JSON parsing context */
}

/* This function parses JSON true or false or null value in a JSON text */
static int lept_parse_literal(lept_context *c, lept_value *v, const char *literal, lept_type type) {
    size_t i = 0;
    EXPECT(c, literal[0]);
    for (i = 1; literal[i] != '\0'; ++i) {
        if (*c->json != literal[i])
            return LEPT_PARSE_INVALID_VALUE;
        c->json++;
    }
    v->type = type;
    return LEPT_PARSE_OK;
}

static int lept_parse_number(lept_context *c, lept_value *v) {
    const char *p = c->json;
    if (*p == '-')
        ++p;
    if (*p == '0')
        ++p;
    else {
        if (!ISDIGIT1TO9(*p))
            return LEPT_PARSE_INVALID_VALUE;
        while (ISDIGIT(*p))
            ++p;
    }
    if (*p == '.') {
        ++p;
        if (!ISDIGIT(*p))
            return LEPT_PARSE_INVALID_VALUE;
        while (ISDIGIT(*p))
            ++p;
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-')
            ++p;
        if (!ISDIGIT(*p))
            return LEPT_PARSE_INVALID_VALUE;
        while (ISDIGIT(*p))
            ++p;
    }
    errno = 0;
    v->n = strtod(c->json, NULL);
    if (errno = ERANGE && (v->n == HUGE_VAL || v->n == -HUGE_VAL))      /* number overflow for double type */
        return LEPT_PARSE_NUMBER_TOO_BIG;
    c->json = p;          /* update the JSON parsing context */
    v->type = LEPT_NUMBER;
    return LEPT_PARSE_OK;
}


static int lept_parse_value(lept_context* c, lept_value* v) {
    switch (*c->json) {
        case 'n':  return lept_parse_literal(c, v, "null", LEPT_NULL);
        case 't':  return lept_parse_literal(c, v, "true", LEPT_TRUE);
        case 'f':  return lept_parse_literal(c, v, "false", LEPT_FALSE);
        case '\0': return LEPT_PARSE_EXPECT_VALUE;
        default:   return lept_parse_number(c, v);
    }
}

int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int lept_parse_result = LEPT_PARSE_OK;
    assert(v != NULL);
    c.json = json;
    v->type = LEPT_NULL;
    lept_parse_whitespace(&c);
    if ((lept_parse_result = lept_parse_value(&c, v)) != LEPT_PARSE_OK)
        return lept_parse_result;
    lept_parse_whitespace(&c);
    if (*(c.json) != '\0') {
        v->type = LEPT_NULL;
        return LEPT_PARSE_ROOT_NOT_SINGULAR;
    }
    return LEPT_PARSE_OK;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);              /* caller should ensure the v is valid */
    return v->type;
}

double lept_get_number(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);    /* caller should ensure the type of v is correct */
    return v->n;
}
