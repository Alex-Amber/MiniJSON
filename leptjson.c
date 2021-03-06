#include "leptjson.h"
#include <stdio.h>      /* sprintf() */
#include <assert.h>     /* assert() */
#include <stdlib.h>     /* NULL, strtod(), malloc(), realloc(), free(), strtol() */
#include <string.h>     /* strchr() */
#include <math.h>       /* HUGE_VAL */
#include <errno.h>      /* errno, ERANGE */

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256
#endif

#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)
#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')
#define PUT(c, ch)          do { *((char *)lept_context_push((c), sizeof(char))) = (ch); } while(0)
#define PUTS(c, s, len)     memcpy(lept_context_push(c, len), s, len)

/* The JSON parsing context, i.e. the position where we currently parse */
typedef struct {
    const char* json;
    char *stack;
    size_t size, top;
}lept_context;

static void* lept_context_push(lept_context *c, size_t size) {
    void *ret;
    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0)
            c->size = LEPT_PARSE_STACK_INIT_SIZE;
        while (c->top + size >= c->size)
            c->size += c->size >> 1;
        c->stack = (char *)realloc(c->stack, c->size);
    }
    ret = c->stack + c->top;
    c->top += size;
    return ret;
}

static void* lept_context_pop(lept_context *c, size_t size) {
    assert(c->top >= size);
    c->top -= size;
    return c->stack + c->top;
}    

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
    v->u.n = strtod(c->json, NULL);
    if (errno = ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))      /* number overflow for double type */
        return LEPT_PARSE_NUMBER_TOO_BIG;
    c->json = p;          /* update the JSON parsing context */
    v->type = LEPT_NUMBER;
    return LEPT_PARSE_OK;
}

static const char* lept_parse_hex4(const char *p, unsigned *u) {
    int i;
    *u = 0;
    for (i = 0; i < 4; ++i) {
        char ch = *p++;
        *u <<= 4;
        if (ch >= '0' && ch <= '9')
            *u |= ch - '0';
        else if (ch >= 'A' && ch <= 'F')
            *u |= ch - 'A' + 10;
        else if (ch >= 'a' && ch <= 'f')
            *u |= ch - 'a' + 10;
        else
            return NULL;
    }
    return p;
}

static void lept_encode_utf8(lept_context *c, unsigned u) {
    assert(u >= 0x0000 && u <= 0x10FFFF);
    if (u >= 0x0000 && u < 0x0080) {
        PUT(c, u & 0x7F);
    }
    else if (u >= 0x0080 && u < 0x0800) {
        PUT(c, 0xC0 | ((u >> 6) & 0x1F));
        PUT(c, 0x80 | ( u       & 0x3F));
    }
    else if (u >= 0x0800 && u < 0x10000) {
        PUT(c, 0xE0 | ((u >> 12) & 0x0F));
        PUT(c, 0x80 | ((u >>  6) & 0x3F));
        PUT(c, 0x80 | ((u      ) & 0x3F));
    }
    else {
        PUT(c, 0xF0 | ((u >> 18) & 0x07));
        PUT(c, 0x80 | ((u >> 12) & 0x3F));
        PUT(c, 0x80 | ((u >>  6) & 0x3F));
        PUT(c, 0x80 | ((u      ) & 0x3F));
    }
    return;
}

#define STRING_ERROR(ret) do { c->top = head; return ret; } while(0)

static int lept_parse_string_raw(lept_context *c, char **str, size_t *len) {
    size_t head = c->top;
    unsigned u, u_low;
    const char *p;
    EXPECT(c, '\"');
    p = c->json;
    for (;;) {
        char ch = *p++;
        switch(ch) {
            case '\"':
                *len = c->top - head;
                *str = lept_context_pop(c, *len);
                c->json = p;
                return LEPT_PARSE_OK;
            case '\0':
                STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
            case '\\':
                switch(*p++) {
                    case '\"': PUT(c, '\"'); break;
                    case '\\': PUT(c, '\\'); break;
                    case '/': PUT(c, '/'); break;
                    case 'b': PUT(c, '\b'); break;
                    case 'f': PUT(c, '\f'); break;
                    case 'n': PUT(c, '\n'); break;
                    case 'r': PUT(c, '\r'); break;
                    case 't': PUT(c, '\t'); break;
                    case 'u': 
                        if (!(p = lept_parse_hex4(p, &u)))
                            STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                        if (u >= 0xD800 && u <= 0xDFFF) {
                            if (*p++ != '\\')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            if (*p++ != 'u')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            if (!(p = lept_parse_hex4(p, &u_low)))
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
                            if (!(u_low >= 0xDC00 && u_low <= 0xDFFF))
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            u = 0x10000 + (u - 0xD800) * 0x400 + (u_low - 0xDC00);
                        }
                        lept_encode_utf8(c, u);
                        break;
                    default:
                       STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
                }
                break;
            default:
                if (ch < 0x20) {
                    STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
                }
                PUT(c, ch);
                break;
        }
    }               
}

static int lept_parse_string(lept_context *c, lept_value *v) {
    char *str;
    size_t len;
    int ret;
    if ((ret = lept_parse_string_raw(c, &str, &len)) == LEPT_PARSE_OK) {
        lept_set_string(v, str, len);
    }
    return ret;
}

static int lept_parse_value(lept_context *c, lept_value *v);

static int lept_parse_array(lept_context *c, lept_value *v) {
    size_t size = 0;
    int ret;
    EXPECT(c, '[');
    lept_parse_whitespace(c);
    if (*c->json == ']') {
        c->json++;
        v->type = LEPT_ARRAY;
        v->u.a.size = 0;
        v->u.a.e = NULL;
        return LEPT_PARSE_OK;
    }
    for (;;) {
        lept_value e;
        lept_init(&e);
        lept_parse_whitespace(c);
        if ((ret = lept_parse_value(c, &e)) != LEPT_PARSE_OK) {
            int i;
            for (i = 0; i < size; ++i) {
                lept_context_pop(c, sizeof(lept_value));
                lept_free((lept_value*)c->stack + c->top);
            }
            return ret;
        }
        memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));
        size++;
        lept_parse_whitespace(c);
        if (*c->json == ',')
            c->json++;
        else if (*c->json == ']') {
            c->json++;
            lept_set_array(v, size);
            v->u.a.size = size;
            size *= sizeof(lept_value);
            memcpy(v->u.a.e, lept_context_pop(c, size), size);
            return LEPT_PARSE_OK;
        }
        else {
            int i;
            for (i = 0; i < size; ++i) {
                lept_context_pop(c, sizeof(lept_value));
                lept_free((lept_value*)c->stack + c->top);
            }
            return LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
        }
    }
}

static int lept_parse_object(lept_context *c, lept_value *v) {
    size_t size;
    lept_member m;
    int ret, i;
    EXPECT(c, '{');
    lept_parse_whitespace(c);
    if (*c->json == '}') {
        c->json++;
        v->type = LEPT_OBJECT;
        v->u.o.m = 0;
        v->u.o.size = 0;
        return LEPT_PARSE_OK;
    }
    m.k = NULL;
    size = 0;
    for (;;) {
        char *str = NULL;
        lept_init(&m.v);
        /* parse member key */
        lept_parse_whitespace(c);
        if (*c->json != '"') {
            ret = LEPT_PARSE_MISS_KEY;
            break;
        }
        if (lept_parse_string_raw(c, &str, &m.klen) != LEPT_PARSE_OK) {
            ret = LEPT_PARSE_MISS_KEY;
            break;
        }
        m.k = (char *)malloc(m.klen + 1);
        m.k[m.klen] = '\0';
        memcpy(m.k, str, m.klen);
        /* parse ws [colon] ws */
        lept_parse_whitespace(c);
        if (*c->json != ':') {
            free(m.k);
            ret = LEPT_PARSE_MISS_COLON;
            break;
        }
        c->json++;
        lept_parse_whitespace(c);
        /* parse value */
        if ((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK) {
            free(m.k);
            break;
        }
        memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(lept_member));
        size++;
        m.k = NULL;     /* ownership is transferred to member on stack */
        /* parse ws [comma | right curly brace] ws */
        lept_parse_whitespace(c);
        if (*c->json == '}') {
            c->json++;
            lept_set_object(v, size);
            v->u.o.size = size;
            size *= sizeof(lept_member);
            memcpy(v->u.o.m, lept_context_pop(c, size), size);
            return LEPT_PARSE_OK;
        }
        else if (*c->json == ',') {
            c->json++;
            continue;
        }
        else {
            free(m.k);
            ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }
    /* pop and free members on the stack */
    for (i = 0; i < size; ++i) {
        lept_member *m = lept_context_pop(c, sizeof(lept_member));
        free(m->k);
        lept_free(&m->v);
    }
    return ret;
}

static int lept_parse_value(lept_context *c, lept_value *v) {
    switch (*c->json) {
        case 'n':  return lept_parse_literal(c, v, "null", LEPT_NULL);
        case 't':  return lept_parse_literal(c, v, "true", LEPT_TRUE);
        case 'f':  return lept_parse_literal(c, v, "false", LEPT_FALSE);
        case '\0': return LEPT_PARSE_EXPECT_VALUE;
        case '"': return lept_parse_string(c, v);
        case '[': return lept_parse_array(c, v);
        case '{': return lept_parse_object(c, v);
        default:   return lept_parse_number(c, v);
    }
}

int lept_parse(lept_value* v, const char* json) {
    lept_context c;
    int lept_parse_result = LEPT_PARSE_OK;
    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    lept_init(v);
    lept_parse_whitespace(&c);
    if ((lept_parse_result = lept_parse_value(&c, v)) != LEPT_PARSE_OK) {
        assert(c.top == 0);
        free(c.stack);
        return lept_parse_result;
    }
    lept_parse_whitespace(&c);
    if (*(c.json) != '\0') {
        assert(c.top == 0);
        free(c.stack);
        v->type = LEPT_NULL;
        return LEPT_PARSE_ROOT_NOT_SINGULAR;
    }
    assert(c.top == 0);
    free(c.stack);
    return LEPT_PARSE_OK;
}

#define IS_ESCAPE_CHAR(ch) (ch) == '\"' || (ch) == '\\' || \
                           (ch) == '\b' || \
                           (ch) == '\f' || (ch) == '\n' || \
                           (ch) == '\r' || (ch) == '\t'

static void lept_stringify_string(lept_context *c, const char *s, size_t len) {
    int i;
    PUT(c, '\"');
    for (i = 0; i < len; ++i) {
        char ch = s[i];
        if (IS_ESCAPE_CHAR(ch)) {
            PUT(c, '\\');
            switch(ch) {
                case '\"': PUT(c, '\"'); break;
                case '\\': PUT(c, '\\'); break;
                case '\b': PUT(c, 'b'); break;
                case '\f': PUT(c, 'f'); break;
                case '\n': PUT(c, 'n'); break;
                case '\r': PUT(c, 'r'); break;
                case '\t': PUT(c, 't'); break;
                default:
                    assert(0 && "invalid character");
            }
        }
        else if (ch < 0x20) {
            PUTS(c, "\\u00", 4);
            sprintf(lept_context_push(c, 2), "%.2x", ch);
        }
        else {
            PUT(c, ch);
        }
    }
    PUT(c, '\"');
    return;
}

static void lept_stringify_value(lept_context *c, const lept_value *v) {
    int i;
    switch(v->type) {
        case LEPT_NULL:
            PUTS(c, "null", 4);
            break;
        case LEPT_FALSE:
            PUTS(c, "false", 5);
            break;
        case LEPT_TRUE:
            PUTS(c, "true", 4);
            break;
        case LEPT_NUMBER:
            c->top -= 32 - sprintf(lept_context_push(c, 32), "%.17g", v->u.n);
            break;
        case LEPT_STRING:
            lept_stringify_string(c, v->u.s.s, v->u.s.len);
            break;
        case LEPT_ARRAY:
            PUT(c, '[');
            for (i = 0; i < v->u.a.size; ++i) {
                if (i > 0)
                    PUT(c, ',');
                lept_stringify_value(c, &v->u.a.e[i]);
            }
            PUT(c, ']');
            break;
        case LEPT_OBJECT:
            PUT(c, '{');
            for (i = 0; i < v->u.o.size; ++i) {
                if (i > 0)
                    PUT(c, ',');
                lept_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
                PUT(c, ':');
                lept_stringify_value(c, &v->u.o.m[i].v);
            }
            PUT(c, '}');
            break;
        default:
            assert(0 && "invalid type");
    }
    return;
}

char* lept_stringify(const lept_value *v, size_t* length) {
    lept_context c;
    assert(v != NULL);
    c.stack = (char *)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
    c.top = 0;
    lept_stringify_value(&c, v);
    if (length)
        *length = c.top;
    PUT(&c, '\0');
    return c.stack;
}

void lept_copy(lept_value *dst, const lept_value *src) {
    size_t i;
    assert(dst != NULL && src != NULL && src != dst);
    switch(src->type) {
        case LEPT_STRING:
            lept_set_string(dst, src->u.s.s, src->u.s.len);
            break;
        case LEPT_ARRAY:
            lept_free(dst);
            lept_set_array(dst, src->u.a.capacity);
            for (i = 0; i < src->u.a.size; ++i) {
                lept_init(&dst->u.a.e[i]);
                lept_copy(&dst->u.a.e[i], &src->u.a.e[i]);
            }
            dst->u.a.size = src->u.a.size;
            break;
        case LEPT_OBJECT:
            lept_free(dst);
            lept_set_object(dst, src->u.o.capacity);
            for (i = 0; i < src->u.o.size; ++i) {
                lept_copy(lept_set_object_value(dst, src->u.o.m[i].k, src->u.o.m[i].klen), 
                          &src->u.o.m[i].v);
            }
            dst->u.o.size = src->u.o.size;
            break;
        default:
            lept_free(dst);
            memcpy(dst, src, sizeof(lept_value));
            break;
    }
}

void lept_move(lept_value *dst, lept_value *src) {
    assert(dst != NULL && src != NULL && src != dst);
    lept_free(dst);
    memcpy(dst, src, sizeof(lept_value));
    lept_init(src);
}

void lept_swap(lept_value *lhs, lept_value *rhs) {
    assert(lhs != NULL && rhs != NULL);
    if (lhs != rhs) {
        lept_value temp;
        memcpy(&temp, lhs, sizeof(lept_value));
        memcpy(lhs,   rhs, sizeof(lept_value));
        memcpy(rhs, &temp, sizeof(lept_value));
    }
}

void lept_free(lept_value *v) {
    assert(v != NULL);
    if (v->type == LEPT_STRING) {
        free(v->u.s.s);
        v->u.s.s = NULL;
    }
    else if (v->type == LEPT_ARRAY) {
        size_t i;
        for (i = 0; i < v->u.a.size; ++i) {
            lept_free(lept_get_array_element(v, i));
        }
        free(v->u.a.e);
        v->u.a.e = NULL;
    }
    else if (v->type == LEPT_OBJECT) {
        size_t i;
        for (i = 0; i < v->u.o.size; ++i) {
            free(v->u.o.m[i].k);
            lept_free(&v->u.o.m[i].v);
        }
        free(v->u.o.m);
        v->u.o.m = NULL;
    }
    v->type = LEPT_NULL;
    return;
}

lept_type lept_get_type(const lept_value* v) {
    assert(v != NULL);              /* caller should ensure the v is valid */
    return v->type;
}

int lept_is_equal(const lept_value *lhs, const lept_value *rhs) {
    size_t i;
    assert(lhs != NULL && rhs != NULL);
    if (lhs->type != rhs->type)
        return 0;
    switch (lhs->type) {
        case LEPT_STRING:
            return lhs->u.s.len == rhs->u.s.len &&
                   memcmp(lhs->u.s.s, rhs->u.s.s, lhs->u.s.len) == 0;
        case LEPT_NUMBER:
            return lhs->u.n == rhs->u.n;
        case LEPT_ARRAY:
            if (lhs->u.a.size != rhs->u.a.size)
                return 0;
            for (i = 0; i < lhs->u.a.size; ++i)
                if (!lept_is_equal(&lhs->u.a.e[i], &rhs->u.a.e[i]))
                    return 0;
            return 1;
        case LEPT_OBJECT:
            if (lhs->u.o.size != rhs->u.o.size)
                return 0;
            for (i = 0; i < lhs->u.o.size; ++i) {
                if (!lept_is_equal(&lhs->u.o.m[i].v,
                                   lept_find_object_value((lept_value *)rhs, lhs->u.o.m[i].k, lhs->u.o.m[i].klen))) {
                    return 0;
                }
            }
            return 1;
        default:
            return 1;
    }
}

int lept_get_boolean(const lept_value *v) {
    assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
    return v->type == LEPT_TRUE ? 1 : 0;
}

void lept_set_boolean(lept_value *v, int b) {
    assert(v != NULL);
    v->type = (b != 0 ? LEPT_TRUE : LEPT_FALSE);
    return;
}

double lept_get_number(const lept_value* v) {
    assert(v != NULL && v->type == LEPT_NUMBER);    /* caller should ensure the type of v is correct */
    return v->u.n;
}

void lept_set_number(lept_value *v, double n) {
    assert(v != NULL);
    lept_free(v);
    v->u.n = n;
    v->type = LEPT_NUMBER;
    return;
}

const char *lept_get_string(const lept_value *v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.s;
}

size_t lept_get_string_length(const lept_value *v) {
    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.len;
}

void lept_set_string(lept_value *v, const char *s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    lept_free(v);
    v->u.s.s = (char *)malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}

void lept_set_array(lept_value *v, size_t capacity) {
    assert(v != NULL);
    lept_free(v);
    v->type = LEPT_ARRAY;
    v->u.a.size = 0;
    v->u.a.capacity = capacity;
    v->u.a.e = capacity > 0 ? (lept_value *)malloc(capacity * sizeof(lept_value)) : NULL;
}

size_t lept_get_array_size(const lept_value *v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    return v->u.a.size;
}

size_t lept_get_array_capacity(const lept_value *v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    return v->u.a.capacity;
}

void lept_reserve_array(lept_value *v, size_t capacity) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    if (v->u.a.capacity < capacity) {
        v->u.a.capacity = capacity;
        v->u.a.e = (lept_value *)realloc(v->u.a.e, capacity * sizeof(lept_value));
    }
}

void lept_shrink_array(lept_value *v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    if (v->u.a.capacity > v->u.a.size) {
        v->u.a.capacity = v->u.a.size;
        v->u.a.e = (lept_value *)realloc(v->u.a.e, v->u.a.capacity * sizeof(lept_value));
    }
}

void lept_clear_array(lept_value *v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    lept_erase_array_element(v, 0, v->u.a.size);
}

lept_value* lept_get_array_element(const lept_value *v, size_t index) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    assert(index < v->u.a.size);
    return (v->u.a.e + index);
}

lept_value* lept_pushback_array_element(lept_value *v) {
    assert(v != NULL && v->type == LEPT_ARRAY);
    if (v->u.a.size == v->u.a.capacity)
        lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
    lept_init(&v->u.a.e[v->u.a.size]);
    return &v->u.a.e[v->u.a.size++];
}

void lept_popback_array_element(lept_value *v) {
    assert(v != NULL && v->type == LEPT_ARRAY && v->u.a.size > 0);
    lept_free(&v->u.a.e[--v->u.a.size]);
}

lept_value* lept_insert_array_element(lept_value *v, size_t index) {
    size_t i;
    assert(v != NULL && v->type == LEPT_ARRAY && index <= v->u.a.size);
    if (index == v->u.a.size)
        return lept_pushback_array_element(v);
    if (v->u.a.size == v->u.a.capacity)
        lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
    for (i = v->u.a.size; i > index; --i) {
        memcpy(&v->u.a.e[i], &v->u.a.e[i-1], sizeof(lept_value));
    }
    v->u.a.size++;
    lept_init(&v->u.a.e[index]);
    return &v->u.a.e[index];
}

void lept_erase_array_element(lept_value *v, size_t index, size_t count) {
    size_t i;
    assert(v != NULL && v->type == LEPT_ARRAY && index + count <= v->u.a.size);
    for (i = index; i < index + count; ++i)
        lept_free(&v->u.a.e[i]);
    if (count != 0 && index + count < v->u.a.size) {
        for (i = index + count; i < v->u.a.size; ++i) {
            lept_move(&v->u.a.e[i-count], &v->u.a.e[i]);
        }
    }
    v->u.a.size -= count;
}

void lept_set_object(lept_value *v, size_t capacity) {
    assert(v != NULL);
    lept_free(v);
    v->type = LEPT_OBJECT;
    v->u.o.size = 0;
    v->u.o.capacity = capacity;
    v->u.o.m = capacity > 0 ? (lept_member *)malloc(capacity * sizeof(lept_member)) : NULL;
}

size_t lept_get_object_size(const lept_value *v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    return v->u.o.size;
}

size_t lept_get_object_capacity(const lept_value *v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    return v->u.o.capacity;
}

void lept_reserve_object(lept_value *v, size_t capacity) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    if (v->u.o.capacity < capacity) {
        v->u.o.capacity = capacity;
        v->u.o.m = (lept_member *)realloc(v->u.o.m, capacity * sizeof(lept_member));
    }
}

void lept_shrink_object(lept_value *v) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    if (v->u.o.capacity > v->u.o.size) {
        v->u.o.capacity = v->u.o.size;
        v->u.o.m = (lept_member *)realloc(v->u.o.m, v->u.o.size * sizeof(lept_member));
    }
}

void lept_clear_object(lept_value *v) {
    size_t i;
    assert(v != NULL && v->type == LEPT_OBJECT);
    for (i = 0; i < v->u.o.size; ++i) {
        free(v->u.o.m[i].k);
        lept_free(&v->u.o.m[i].v);
    }
    v->u.o.size = 0;
}

const char* lept_get_object_key(const lept_value *v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].k;
}

size_t lept_get_object_key_length(const lept_value *v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].klen;
}

lept_value* lept_get_object_value(const lept_value *v, size_t index) {
    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return &v->u.o.m[index].v;
}

size_t lept_find_object_index(const lept_value *v, const char *key, size_t klen) {
    size_t i;
    assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);
    for (i = 0; i < v->u.o.size; ++i)
        if (v->u.o.m[i].klen == klen && memcmp(v->u.o.m[i].k, key, klen) == 0)
            return i;
    return LEPT_KEY_NOT_EXIST;
}

lept_value* lept_find_object_value(lept_value *v, const char *key, size_t klen) {
    size_t index = lept_find_object_index(v, key, klen);
    return index != LEPT_KEY_NOT_EXIST ? &v->u.o.m[index].v : NULL;
}

lept_value* lept_set_object_value(lept_value *v, const char *key, size_t klen) {
    size_t new_member_index;
    lept_value *member_v;
    assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);
    if ((member_v = lept_find_object_value(v, key, klen)) != NULL) {
        lept_free(member_v);
        return member_v;
    }
    if (v->u.o.size == v->u.o.capacity)
        lept_reserve_object(v, v->u.o.capacity == 0 ? 1 : v->u.o.capacity * 2);
    new_member_index = v->u.o.size++;
    memcpy(v->u.o.m[new_member_index].k = (char *)malloc(klen+1), key, klen);
    v->u.o.m[new_member_index].k[klen] = '\0';
    v->u.o.m[new_member_index].klen = klen;
    lept_init(&v->u.o.m[new_member_index].v);
    return &v->u.o.m[new_member_index].v;
}

void lept_remove_object_value(lept_value *v, size_t index) {
    size_t last_member_index;
    assert(v != NULL && v->type == LEPT_OBJECT && index < v->u.o.size);
    free(v->u.o.m[index].k);
    lept_free(&v->u.o.m[index].v);
    last_member_index = --v->u.o.size;
    if (index != last_member_index) {
        v->u.o.m[index].k = v->u.o.m[last_member_index].k;
        v->u.o.m[index].klen = v->u.o.m[last_member_index].klen;
        lept_move(&v->u.o.m[index].v, &v->u.o.m[last_member_index].v);
        v->u.o.m[last_member_index].k = NULL;
        v->u.o.m[last_member_index].klen = 0;
    }
}
