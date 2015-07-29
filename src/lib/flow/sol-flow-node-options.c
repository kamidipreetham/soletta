/*
 * This file is part of the Soletta Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "sol-flow-internal.h"
#include "sol-str-slice.h"
#include "sol-str-table.h"
#include "sol-util.h"

#ifdef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED

static const char SUBOPTION_SEPARATOR = '|';

static void *
get_member_memory(const struct sol_flow_node_options_member_description *member, struct sol_flow_node_options *opts)
{
    return (uint8_t *)opts + member->offset;
}

#define STRTOL_DECIMAL(_ptr, _endptr) strtol(_ptr, _endptr, 0)

#define KEY_VALUES_RECAP(_max_val, _min_val)    \
    do {                                        \
        if (keys_schema) {                      \
            if (!min_done) ret->min = _min_val; \
            if (!max_done) ret->max = _max_val; \
            if (!step_done) ret->step = 1;      \
            if (!val_done) ret->val = 0;        \
        }                                       \
    } while (0)                                 \

#define LINEAR_VALUES_RECAP(_max_val, _min_val) \
    do {                                        \
        switch (field_cnt) {                    \
        case 1:                                 \
            ret->min = _min_val;                \
        case 2:                                 \
            ret->max = _max_val;                \
        case 3:                                 \
            ret->step = 1;                      \
        default:                                \
            break;                              \
        }                                       \
    } while (0)                                 \

#define ASSIGN_LINEAR_VALUES(_parse_func, \
        _max_val, _max_str, _max_str_len,          \
        _min_val, _min_str, _min_str_len)          \
    do {                                                                \
        char *start, *end, backup;                                      \
        int field_cnt_max = ARRAY_SIZE(store_vals); \
        if (keys_schema) break;                                         \
        start = buf;                                                    \
        end = strchr(start, SUBOPTION_SEPARATOR);                       \
        if (!end) end = start + strlen(start);                          \
        backup = *end;                                                  \
        *end = '\0';                                                    \
        for (field_cnt = 0; field_cnt < field_cnt_max;) { \
            bool is_max = false, is_min = false;                        \
            errno = 0;                                                  \
            if (strlen(start) >= _max_str_len                           \
                && (strncmp(start, _max_str,                            \
                _max_str_len) == 0)) {                      \
                is_max = true;                                          \
            } else if (strlen(start) >= _min_str_len                    \
                && (strncmp(start, _min_str,                     \
                _min_str_len) == 0)) {               \
                is_min = true;                                          \
            }                                                           \
            if (is_max) *store_vals[field_cnt] = _max_val;              \
            else if (is_min) *store_vals[field_cnt] = _min_val;         \
            else { \
                char *endptr; \
                *store_vals[field_cnt] = _parse_func(start, &endptr); \
                /* check if no number was parsed, indicates invalid string */ \
                if (start == endptr) \
                    goto err; \
            } \
            if (errno != 0) goto err;                                   \
            field_cnt++;                                                \
            *end = backup;                                              \
            if (backup == '\0') break;                                  \
            start = end + 1;                                            \
            if (!start) break;                                          \
            end = strchr(start, SUBOPTION_SEPARATOR);                   \
            if (!end) end = start + strlen(start);                      \
            backup = *end;                                              \
            *end = '\0';                                                \
        }                                                               \
    } while (0)

#define ASSIGN_KEY_VAL(_type, _key, _parse_func, _only_not_negative, \
        _max_val, _max_str, _max_str_len,        \
        _min_val, _min_str, _min_str_len)        \
    do {                                                        \
        bool is_max = false, is_min = false;                    \
        _key = strstr(buf, #_key);                              \
        if (_key) {                                             \
            keys_schema = true;                                 \
            _key = strchr(_key, ':');                           \
        } else break;                                           \
        if (_key && _key[0] && _key[1]) {                       \
            _key++;                                             \
            while (_key && isspace(*_key)) _key++;              \
        } else goto err;                                        \
        if (!_key)                                              \
            break;                                              \
        if (strlen(_key) >= _max_str_len                        \
            && (strncmp(_key, _max_str,                         \
            _max_str_len) == 0)) {                  \
            is_max = true;                                      \
        } else if (strlen(_key) >= _min_str_len                 \
            && (strncmp(_key, _min_str,                  \
            _min_str_len) == 0)) {           \
            is_min = true;                                      \
        }                                                       \
        if (is_max)                                             \
            ret->_key = _max_val;                               \
        else if (is_min) { \
            if (_only_not_negative) \
                goto err; \
            ret->_key = _min_val;                               \
        } else { \
            char *_key ## _end = _key;                          \
            char _key ## _backup;                               \
            char *endptr; \
            _type parsed_val; \
            while (_key ## _end                                 \
                && *_key ## _end != '\0'                     \
                && *_key ## _end != SUBOPTION_SEPARATOR)     \
                _key ## _end++;                                 \
            if (_key ## _end) {                                 \
                _key ## _backup = *_key ## _end;                \
                *_key ## _end = '\0';                           \
            }                                                   \
            errno = 0;                                          \
            parsed_val = _parse_func(_key, &endptr); \
            if (_key == endptr) \
                goto err; \
            if (errno != 0)                                     \
                goto err;                                       \
            if (_only_not_negative) { \
                if (parsed_val < 0) \
                    goto err; \
            } \
            ret->_key = parsed_val; \
            _key ## _done = true;                               \
            if (_key ## _end)                                   \
                *_key ## _end = _key ## _backup;                \
        }                                                       \
    } while (0)

static int
irange_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    char *buf;
    int field_cnt = 0;
    bool keys_schema = false;
    char *min, *max, *step, *val;
    bool min_done = false, max_done = false,
        step_done = false, val_done = false;
    static const char INT_MAX_STR[] = "INT32_MAX";
    static const char INT_MIN_STR[] = "INT32_MIN";
    static const size_t INT_LIMIT_STR_LEN = sizeof(INT_MAX_STR) - 1;
    struct sol_irange *ret = &m->irange;
    int32_t *store_vals[] = { &ret->val, &ret->min, &ret->max, &ret->step };

    buf = strdup(value);

    ASSIGN_KEY_VAL(int32_t, min, STRTOL_DECIMAL, false,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, max, STRTOL_DECIMAL, false,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, step, STRTOL_DECIMAL, false,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, val, STRTOL_DECIMAL, false,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);

    KEY_VALUES_RECAP(INT32_MAX, INT32_MIN);

    ASSIGN_LINEAR_VALUES(STRTOL_DECIMAL,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);

    LINEAR_VALUES_RECAP(INT32_MAX, INT32_MIN);

    SOL_DBG("irange opt ends up as min=%d, max=%d, step=%d, val=%d\n",
        ret->min, ret->max, ret->step, ret->val);

    free(buf);
    return 0;

err:
    SOL_DBG("Invalid irange value for option name=\"%s\": \"%s\"."
        " Please use the formats"
        " \"<val_value>|<min_value>|<max_value>|<step_value>\","
        " in that order, or \"<key>:<value>|<...>\", for keys in "
        "[val, min, max, step], in any order. Values may be the "
        "special strings INT32_MAX and INT32_MIN.",
        m->name, value);
    free(buf);
    return -EINVAL;
}

static int
drange_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    char *buf;
    int field_cnt = 0;
    bool keys_schema = false;
    char *min, *max, *step, *val;
    bool min_done = false, max_done = false,
        step_done = false, val_done = false;
    static const char DBL_MAX_STR[] = "DBL_MAX";
    static const char DBL_MIN_STR[] = "-DBL_MAX";
    static const size_t DBL_MAX_STR_LEN = sizeof(DBL_MAX_STR) - 1;
    static const size_t DBL_MIN_STR_LEN = sizeof(DBL_MIN_STR) - 1;
    struct sol_drange *ret = &m->drange;
    double *store_vals[] = { &ret->val, &ret->min, &ret->max, &ret->step };

    buf = strdup(value);

    ASSIGN_KEY_VAL(double, min, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, max, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, step, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, val, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);

    KEY_VALUES_RECAP(DBL_MAX, -DBL_MAX);

    ASSIGN_LINEAR_VALUES(strtod,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);

    LINEAR_VALUES_RECAP(DBL_MAX, -DBL_MAX);

    SOL_DBG("drange opt ends up as min=%lf, max=%lf, step=%lf, val=%lf\n",
        ret->min, ret->max, ret->step, ret->val);

    free(buf);
    return 0;

err:
    SOL_DBG("Invalid drange value for option name=\"%s\": \"%s\"."
        " Please use the formats"
        " \"<val_value>|<min_value>|<max_value>|<step_value>\","
        " in that order, or \"<key>:<value>|<...>\", for keys in "
        "[val, min, max, step], in any order. Values may be the "
        "special strings DBL_MAX and -DBL_MAX. Don't use commas "
        "on the numbers",
        m->name, value);
    free(buf);
    return -EINVAL;
}

static int
rgb_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    char *buf;
    int field_cnt = 0;
    bool keys_schema = false;
    char *red, *green, *blue, *red_max, *green_max, *blue_max;
    bool red_done = false, green_done = false, blue_done = false,
        red_max_done = false, green_max_done = false, blue_max_done = false;
    static const char INT_MAX_STR[] = "INT32_MAX";
    static const char INT_MIN_STR[] = "INT32_MIN";
    static const size_t INT_LIMIT_STR_LEN = sizeof(INT_MAX_STR) - 1;
    struct sol_rgb *ret = &m->rgb;
    uint32_t *store_vals[] = { &ret->red, &ret->green, &ret->blue,
                               &ret->red_max, &ret->green_max, &ret->blue_max };

    buf = strdup(value);

    ASSIGN_KEY_VAL(int32_t, red, STRTOL_DECIMAL, true,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, green, STRTOL_DECIMAL, true,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, blue, STRTOL_DECIMAL, true,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, red_max, STRTOL_DECIMAL, true,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, green_max, STRTOL_DECIMAL, true,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);
    ASSIGN_KEY_VAL(int32_t, blue_max, STRTOL_DECIMAL, true,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);

    if (keys_schema) {
        if (!red_done) ret->red = 0;
        if (!red_max_done) ret->red_max = 255;
        if (!green_done) ret->green = 0;
        if (!green_max_done) ret->green_max = 255;
        if (!blue_done) ret->blue = 0;
        if (!blue_max_done) ret->blue_max = 255;
    }

    ASSIGN_LINEAR_VALUES(STRTOL_DECIMAL,
        INT32_MAX, INT_MAX_STR, INT_LIMIT_STR_LEN,
        INT32_MIN, INT_MIN_STR, INT_LIMIT_STR_LEN);

    /* field_cnt shouldn't start from 0 in switch,
     * since if no value was declared, it doesn't make
     * sense to declare the option. Also, if values were
     * assigned by ASSIGN_KEY_VAL field_cnt would stay 0, and the whole
     * option would be set to default values */
    switch (field_cnt) {
    case 1:
        ret->green = 0;
    case 2:
        ret->blue = 0;
    case 3:
        ret->red_max = 255;
    case 4:
        ret->green_max = 255;
    case 5:
        ret->blue_max = 255;
    default:
        break;
    }

    SOL_DBG("rgb opt ends up as red=%d, green=%d, blue=%d "
        "red_max=%d, green_max=%d, blue_max=%d\n",
        ret->red, ret->green, ret->blue,
        ret->red_max, ret->green_max, ret->blue_max);

    free(buf);
    return 0;

err:
    SOL_DBG("Invalid rgb value for option name=\"%s\": \"%s\"."
        " Please use the formats"
        " \"<red_value>|<green_value>|<blue_value>|"
        "<red_max_value>|<green_max_value>|<blue_max_value>\","
        " in that order, or \"<key>:<value>|<...>\", for keys in "
        "[red, green, blue, red_max, green_max, blue_max], in any order. "
        "Values may be the special strings INT32_MAX. All of them must be "
        "not negative int values.",
        m->name, value);
    free(buf);
    return -EINVAL;
}


static int
direction_vector_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    char *buf;
    int field_cnt = 0;
    bool keys_schema = false;
    char *min, *max, *x, *y, *z;
    bool min_done = false, max_done = false,
        x_done = false, y_done = false, z_done = false;
    static const char DBL_MAX_STR[] = "DBL_MAX";
    static const char DBL_MIN_STR[] = "-DBL_MAX";
    static const size_t DBL_MAX_STR_LEN = sizeof(DBL_MAX_STR) - 1;
    static const size_t DBL_MIN_STR_LEN = sizeof(DBL_MIN_STR) - 1;
    struct sol_direction_vector *ret = &m->direction_vector;
    double *store_vals[] = { &ret->x, &ret->y, &ret->z, &ret->min, &ret->max };

    buf = strdup(value);

    ASSIGN_KEY_VAL(double, x, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, y, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, z, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, min, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);
    ASSIGN_KEY_VAL(double, max, strtod, false,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);

    if (keys_schema) {
        if (!x_done) ret->x = 0;
        if (!y_done) ret->y = 0;
        if (!z_done) ret->z = 0;
        if (!min_done) ret->min = -DBL_MAX;
        if (!max_done) ret->max = DBL_MAX;
    }

    ASSIGN_LINEAR_VALUES(strtod,
        DBL_MAX, DBL_MAX_STR, DBL_MAX_STR_LEN,
        -DBL_MAX, DBL_MIN_STR, DBL_MIN_STR_LEN);

    switch (field_cnt) {
    case 1:
        ret->y = 0;
    case 2:
        ret->z = 0;
    case 3:
        ret->min = -DBL_MAX;
    case 4:
        ret->max = DBL_MAX;
    default:
        break;
    }

    SOL_DBG("direction_vector opt ends up as x=%lf, y=%lf, z=%lf, min=%lf, max=%lf\n",
        ret->x, ret->y, ret->z, ret->min, ret->max);

    free(buf);
    return 0;

err:
    SOL_ERR("Invalid direction_vector value for option name=\"%s\": \"%s\"."
        " Please use the formats"
        " \"<x_value>|<y_value>|<z_value>|<min_value>|<max_value>\","
        " in that order, or \"<key>:<value>|<...>\", for keys in "
        "[x, y, z, min, max], in any order. Values may be the "
        "special strings DBL_MAX and -DBL_MAX. Don't use commas "
        "on the numbers",
        m->name, value);
    free(buf);
    return -EINVAL;
}

#undef STRTOL_DECIMAL
#undef KEY_VALUES_RECAP
#undef LINEAR_VALUES_RECAP
#undef ASSIGN_LINEAR_VALUES
#undef ASSIGN_KEY_VAL

static int
boolean_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    if (streq(value, "1") || streq(value, "true") || streq(value, "on") || streq(value, "yes")) {
        m->boolean = true;
    } else if (streq(value, "0") || streq(value, "false") || streq(value, "off") || streq(value, "no")) {
        m->boolean = false;
    } else {
        return -EINVAL;
    }
    return 0;
}

static int
byte_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    int v;

    errno = 0;
    v = strtol(value, NULL, 0);
    if ((errno != 0) || (v < 0) || (v > 255)) {
        return -EINVAL;
    }

    m->byte = v;
    return 0;
}

static int
string_parse(const char *value, struct sol_flow_node_named_options_member *m)
{
    char *s;

    s = strdup(value);
    if (!s)
        return -errno;

    m->string = s;
    return 0;
}

static int(*const options_parse_functions[]) (const char *, struct sol_flow_node_named_options_member *) = {
    [SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN] = NULL,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN] = boolean_parse,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_BYTE] = byte_parse,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE] = irange_parse,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_DRANGE] = drange_parse,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_RGB] = rgb_parse,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_DIRECTION_VECTOR] = direction_vector_parse,
    [SOL_FLOW_NODE_OPTIONS_MEMBER_STRING] = string_parse,
};

SOL_API int
sol_flow_node_named_options_parse_member(
    struct sol_flow_node_named_options_member *m,
    const char *value)
{
    int r;

    if (m->type == 0) {
        r = -EINVAL;
        SOL_DBG("Unitialized member type for name=\"%s\": \"%s\"", m->name, value);
    } else if (m->type < ARRAY_SIZE(options_parse_functions)) {
        r = options_parse_functions[m->type](value, m);
        if (r < 0) {
            SOL_DBG("Invalid value '%s' for option name='%s' of type='%s'",
                value, m->name, sol_flow_node_options_member_type_to_string(m->type));
        }
    } else {
        r = -ENOTSUP;
        SOL_DBG("Invalid option member type for name=\"%s\": \"%s\"", m->name, value);
    }

    return r;
}

static void
set_member(
    const struct sol_flow_node_options_member_description *mdesc,
    enum sol_flow_node_options_member_type mdesc_type,
    const struct sol_flow_node_named_options_member *m,
    struct sol_flow_node_options *opts)
{
    char **s;
    void *mem = get_member_memory(mdesc, opts);

    switch (mdesc_type) {
    case SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN:
        *(bool *)mem = m->boolean;
        break;
    case SOL_FLOW_NODE_OPTIONS_MEMBER_BYTE:
        *(unsigned char *)mem = m->byte;
        break;
    case SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE:
        *(struct sol_irange *)mem = m->irange;
        break;
    case SOL_FLOW_NODE_OPTIONS_MEMBER_DRANGE:
        *(struct sol_drange *)mem = m->drange;
        break;
    case SOL_FLOW_NODE_OPTIONS_MEMBER_RGB:
        *(struct sol_rgb *)mem = m->rgb;
        break;
    case SOL_FLOW_NODE_OPTIONS_MEMBER_DIRECTION_VECTOR:
        *(struct sol_direction_vector *)mem = m->direction_vector;
        break;
    case SOL_FLOW_NODE_OPTIONS_MEMBER_STRING:
        s = mem;
        free(*s);
        *s = strdup(m->string);
    default:
        /* Not reached. */
        /* TODO: Create ASSERT_NOT_REACHED() macro.*/
        break;
    }
}

static const struct sol_str_table member_str_to_type[] = {
    SOL_STR_TABLE_ITEM("boolean", SOL_FLOW_NODE_OPTIONS_MEMBER_BOOLEAN),
    SOL_STR_TABLE_ITEM("byte", SOL_FLOW_NODE_OPTIONS_MEMBER_BYTE),
    SOL_STR_TABLE_ITEM("int", SOL_FLOW_NODE_OPTIONS_MEMBER_IRANGE),
    SOL_STR_TABLE_ITEM("float", SOL_FLOW_NODE_OPTIONS_MEMBER_DRANGE),
    SOL_STR_TABLE_ITEM("rgb", SOL_FLOW_NODE_OPTIONS_MEMBER_RGB),
    SOL_STR_TABLE_ITEM("direction-vector", SOL_FLOW_NODE_OPTIONS_MEMBER_DIRECTION_VECTOR),
    SOL_STR_TABLE_ITEM("string", SOL_FLOW_NODE_OPTIONS_MEMBER_STRING),
    {}
};

/* TODO: Change type description to use the enum and remove this
 * function. */
SOL_API enum sol_flow_node_options_member_type
sol_flow_node_options_member_type_from_string(const char *data_type)
{
    if (!data_type)
        return SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN;
    return sol_str_table_lookup_fallback(member_str_to_type,
        sol_str_slice_from_str(data_type), SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN);
}

/* TODO: Make a reverse table? */
SOL_API const char *
sol_flow_node_options_member_type_to_string(enum sol_flow_node_options_member_type type)
{
    const struct sol_str_table *entry;
    const char *data_type = NULL;

    for (entry = member_str_to_type; entry->key; entry++) {
        if ((enum sol_flow_node_options_member_type)entry->val == type) {
            data_type = entry->key;
            break;
        }
    }

    return data_type;
}

static bool
fill_options_with_named_options(
    struct sol_flow_node_options *opts,
    const struct sol_flow_node_options_description *desc,
    const struct sol_flow_node_named_options *named_opts)
{
    const struct sol_flow_node_options_member_description *mdesc;

    uint16_t count, i;
    bool *handled_member = NULL;
    bool success = false, has_required = false;

    count = 0;
    for (mdesc = desc->members; mdesc->name != NULL; mdesc++) {
        count++;
        has_required |= mdesc->required;
    }

    if (has_required) {
        handled_member = calloc(count, sizeof(bool));
        SOL_NULL_CHECK(handled_member, false);
    }

    for (i = 0; i < named_opts->count; i++) {
        const struct sol_flow_node_named_options_member *m = named_opts->members + i;
        enum sol_flow_node_options_member_type mdesc_type;
        bool found = false;

        mdesc = NULL;
        if (m->name) {
            for (mdesc = desc->members; mdesc->name; mdesc++) {
                if (streq(mdesc->name, m->name)) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            SOL_DBG("Unknown option: \"%s\"", m->name);
            goto end;
        }

        mdesc_type = sol_flow_node_options_member_type_from_string(mdesc->data_type);
        if (mdesc_type != m->type || mdesc_type == SOL_FLOW_NODE_OPTIONS_MEMBER_UNKNOWN) {
            SOL_DBG("Wrong type passed to member #%u "
                "name=\"%s\", type=\"%s\"",
                (unsigned)(mdesc - desc->members), mdesc->name, mdesc->data_type);
            goto end;
        }

        set_member(mdesc, mdesc_type, m, opts);

        if (has_required) {
            handled_member[mdesc - desc->members] = true;
        }

        SOL_DBG("Parsed option \"%s\" member #%u "
            "name=\"%s\", type=\"%s\", offset=%hu, size=%hu",
            m->name, (unsigned)(mdesc - desc->members), mdesc->name,
            mdesc->data_type, mdesc->offset, mdesc->size);
    }

    if (has_required) {
        for (mdesc = desc->members; mdesc->name != NULL; mdesc++) {
            if (mdesc->required && !handled_member[mdesc - desc->members]) {
                SOL_DBG("Required member not in options: "
                    "name=\"%s\", type=\"%s\"", mdesc->name, mdesc->data_type);
                goto end;
            }
        }
    }

    success = true;

end:
    free(handled_member);
    return success;
}

static int
split_option(const char *input, const char **key, unsigned int *key_len, const char **value)
{
    const char *equal = strchr(input, '=');

    if (!equal || equal == input || equal + 1 == '\0')
        return -EINVAL;

    *key = input;
    *key_len = equal - input;
    *value = equal + 1;
    return 0;
}
#endif

SOL_API int
sol_flow_node_options_new(
    const struct sol_flow_node_type *type,
    const struct sol_flow_node_named_options *named_opts,
    struct sol_flow_node_options **out_opts)
{
#ifndef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    SOL_WRN("does not work if compiled with --disable-flow-node-type-description");
    return -ENOTSUP;
#else
    struct sol_flow_node_options *tmp_opts;
    const struct sol_flow_node_type_description *desc;
    bool type_has_options;

    SOL_NULL_CHECK(type, -EINVAL);
    SOL_NULL_CHECK(named_opts, -EINVAL);
    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, -EINVAL);

    if (type->init_type)
        type->init_type();
    SOL_NULL_CHECK(type->description, -EINVAL);

    desc = type->description;
    type_has_options = desc->options && desc->options->members;
    if (!type_has_options && named_opts->count > 0) {
        /* TODO: improve the error handling here so caller knows more
         * details about this. */
        return -EINVAL;
    }

    if (!type->new_options) {
        if (!type_has_options)
            return 0;
        return -EINVAL;
    }

    tmp_opts = type->new_options(type, NULL);
    SOL_NULL_CHECK(tmp_opts, -EINVAL);

    if (type_has_options) {
        if (!fill_options_with_named_options(tmp_opts, desc->options, named_opts)) {
            type->free_options(type, tmp_opts);
            return -EINVAL;
        }
    }

    *out_opts = tmp_opts;
    return 0;
#endif
}

SOL_API int
sol_flow_node_named_options_init_from_strv(
    struct sol_flow_node_named_options *named_opts,
    const struct sol_flow_node_type *type,
    const char *const *strv)
{
#ifndef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    SOL_WRN("does not work if compiled with --disable-flow-node-type-description");
    return -ENOTSUP;
#else
    const struct sol_flow_node_options_description *desc;
    struct sol_flow_node_named_options_member *members = NULL, *m;
    uint16_t members_count = 0;
    const struct sol_flow_node_options_member_description *mdesc;
    const char *const *entry;
    int r;

    SOL_NULL_CHECK(named_opts, -EINVAL);

    if (type->init_type)
        type->init_type();
    SOL_NULL_CHECK(type->description, -EINVAL);
    SOL_NULL_CHECK(type->description->options, -EINVAL);
    SOL_NULL_CHECK(type->description->options->members, -EINVAL);

    desc = type->description->options;

    if (strv) {
        for (entry = strv; *entry; entry++) {
            members_count++;
        }
        members = calloc(members_count, sizeof(struct sol_flow_node_named_options_member));
    }

    for (entry = strv, m = members; entry && *entry != NULL; entry++, m++) {
        const char *key, *value;
        unsigned int key_len;

        if (split_option(*entry, &key, &key_len, &value)) {
            r = -EINVAL;
            SOL_DBG("Invalid option #%u format: \"%s\"", (unsigned)(entry - strv), *entry);
            goto end;
        }

        mdesc = NULL;
        for (mdesc = desc->members; mdesc->name != NULL; mdesc++) {
            if (sol_str_slice_str_eq(SOL_STR_SLICE_STR(key, key_len), mdesc->name))
                break;
        }
        if (!mdesc) {
            r = -EINVAL;
            SOL_DBG("Unknown option: \"%s\"", *entry);
            goto end;
        }

        m->type = sol_flow_node_options_member_type_from_string(mdesc->data_type);
        m->name = mdesc->name;

        r = sol_flow_node_named_options_parse_member(m, value);
        if (r < 0) {
            SOL_DBG("Could not parse member #%u "
                "name=\"%s\", type=\"%s\", option=\"%s\": %s",
                (unsigned)(mdesc - desc->members), mdesc->name,
                mdesc->data_type, *entry, sol_util_strerrora(-r));
            goto end;
        }

        SOL_DBG("Parsed option \"%s\" member #%u "
            "name=\"%s\", type=\"%s\", offset=%hu, size=%hu",
            *entry, (unsigned)(mdesc - desc->members), mdesc->name,
            mdesc->data_type, mdesc->offset, mdesc->size);
    }

    named_opts->members = members;
    members = NULL;
    named_opts->count = members_count;
    r = 0;

end:
    free(members);
    return r;
#endif
}

SOL_API struct sol_flow_node_options *
sol_flow_node_options_copy(const struct sol_flow_node_type *type, const struct sol_flow_node_options *opts)
{
    SOL_FLOW_NODE_TYPE_API_CHECK(type, SOL_FLOW_NODE_TYPE_API_VERSION, NULL);
    SOL_FLOW_NODE_OPTIONS_API_CHECK(type, SOL_FLOW_NODE_OPTIONS_API_VERSION, NULL);

#ifndef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    SOL_WRN("does not work if compiled with --disable-flow-node-type-description");
    return NULL;
#else
    SOL_NULL_CHECK(type->description, NULL);
    SOL_NULL_CHECK(type->description->options, NULL);
    SOL_NULL_CHECK(type->description->options->members, NULL);
    SOL_NULL_CHECK(type->new_options, NULL);
    return type->new_options(type, opts);
#endif
}

SOL_API void
sol_flow_node_options_del(const struct sol_flow_node_type *type, struct sol_flow_node_options *options)
{
    SOL_NULL_CHECK(type);
    SOL_FLOW_NODE_OPTIONS_API_CHECK(options, SOL_FLOW_NODE_OPTIONS_API_VERSION);
#ifndef SOL_FLOW_NODE_TYPE_DESCRIPTION_ENABLED
    SOL_WRN("does not work if compiled with --disable-flow-node-type-description");
#else
    SOL_NULL_CHECK(type->description);
    SOL_NULL_CHECK(type->description->options);
    SOL_NULL_CHECK(type->description->options->members);
    SOL_NULL_CHECK(type->free_options);
    SOL_FLOW_NODE_OPTIONS_SUB_API_CHECK(options, type->description->options->sub_api);
    type->free_options(type, options);
#endif
}

SOL_API void
sol_flow_node_options_strv_del(char **opts_strv)
{
    char **opts_it;

    if (!opts_strv)
        return;
    for (opts_it = opts_strv; *opts_it != NULL; opts_it++)
        free(*opts_it);
    free(opts_strv);
}

SOL_API void
sol_flow_node_named_options_fini(struct sol_flow_node_named_options *named_opts)
{
    struct sol_flow_node_named_options_member *m;
    uint16_t i;

    if (!named_opts || !named_opts->members)
        return;

    for (i = 0, m = named_opts->members; i < named_opts->count; i++, m++) {
        if (m->type == SOL_FLOW_NODE_OPTIONS_MEMBER_STRING)
            free((void *)m->string);
    }

    free(named_opts->members);

    named_opts->members = NULL;
    named_opts->count = 0;
}
