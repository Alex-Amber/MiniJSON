#ifndef LEPTJSON_H__
#define LEPTJSON_H__

/* All possible values for JSON type */
typedef enum { LEPT_NULL, LEPT_FALSE, LEPT_TRUE, LEPT_NUMBER, LEPT_STRING, LEPT_ARRAY, LEPT_OBJECT } lept_type;

typedef struct {
    double n;           /* value for a JSON number */
    lept_type type;     /* type for a JSON value */
}lept_value;

enum {
    LEPT_PARSE_OK = 0,
    LEPT_PARSE_EXPECT_VALUE,
    LEPT_PARSE_INVALID_VALUE,
    LEPT_PARSE_ROOT_NOT_SINGULAR,
    LEPT_PARSE_NUMBER_TOO_BIG
};      /* Enumeration for parsing results */

/* This function parsing a JSON text into a JSON value */
int lept_parse(lept_value* v, const char* json);

/* The getter function for the type of a JSON value */
lept_type lept_get_type(const lept_value* v);

/* This function return the value of a JSON number */
double lept_get_number(const lept_value* v);

#endif /* LEPTJSON_H__ */
