#include "resp.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>

static const char *find_crlf(const char *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') return buf + i;
    }
    return NULL;
}

static int parse_int64(const char *buf, size_t len, int64_t *out, const char **end) {
    const char *crlf = find_crlf(buf, len);
    if (!crlf) return 1;

    int64_t val = 0;
    int neg = 0;
    const char *p = buf;
    if (p < crlf && *p == '-') { neg = 1; p++; }
    if (p == crlf) return -1;
    for (; p < crlf; p++) {
        if (*p < '0' || *p > '9') return -1;
        val = val * 10 + (*p - '0');
    }
    *out = neg ? -val : val;
    *end = crlf + 2;
    return 0;
}

int resp_parse(const char *buf, size_t buflen, resp_value *out, size_t *consumed) {
    if (buflen == 0) return 1;

    char prefix = buf[0];
    const char *rest = buf + 1;
    size_t rlen = buflen - 1;

    if (prefix == '+') {
        const char *crlf = find_crlf(rest, rlen);
        if (!crlf) return 1;
        size_t slen = (size_t)(crlf - rest);
        char *s = malloc(slen + 1);
        if (!s) return -1;
        memcpy(s, rest, slen);
        s[slen] = '\0';
        out->type       = RESP_SIMPLE_STRING;
        out->v.str.data = s;
        out->v.str.len  = slen;
        *consumed = (size_t)(crlf + 2 - buf);
        return 0;
    }

    if (prefix == '-') {
        const char *crlf = find_crlf(rest, rlen);
        if (!crlf) return 1;
        size_t slen = (size_t)(crlf - rest);
        char *s = malloc(slen + 1);
        if (!s) return -1;
        memcpy(s, rest, slen);
        s[slen] = '\0';
        out->type       = RESP_ERROR;
        out->v.str.data = s;
        out->v.str.len  = slen;
        *consumed = (size_t)(crlf + 2 - buf);
        return 0;
    }

    if (prefix == ':') {
        int64_t val;
        const char *end;
        int rc = parse_int64(rest, rlen, &val, &end);
        if (rc != 0) return rc;
        out->type      = RESP_INTEGER;
        out->v.integer = val;
        *consumed = (size_t)(end - buf);
        return 0;
    }

    if (prefix == '$') {
        int64_t blen;
        const char *after_len;
        int rc = parse_int64(rest, rlen, &blen, &after_len);
        if (rc != 0) return rc;

        if (blen == -1) {
            out->type = RESP_NIL;
            *consumed = (size_t)(after_len - buf);
            return 0;
        }
        if (blen < 0) return -1;

        size_t need  = (size_t)blen + 2;
        size_t avail = buflen - (size_t)(after_len - buf);
        if (avail < need) return 1;

        char *s = malloc((size_t)blen + 1);
        if (!s) return -1;
        memcpy(s, after_len, (size_t)blen);
        s[blen] = '\0';
        out->type       = RESP_BULK_STRING;
        out->v.str.data = s;
        out->v.str.len  = (size_t)blen;
        *consumed = (size_t)(after_len - buf) + need;
        return 0;
    }

    if (prefix == '*') {
        int64_t count;
        const char *after_count;
        int rc = parse_int64(rest, rlen, &count, &after_count);
        if (rc != 0) return rc;

        if (count == -1) {
            out->type = RESP_NIL;
            *consumed = (size_t)(after_count - buf);
            return 0;
        }
        if (count < 0) return -1;

        resp_value *items = calloc((size_t)count, sizeof(resp_value));
        if (!items && count > 0) return -1;

        size_t offset = (size_t)(after_count - buf);
        for (int64_t i = 0; i < count; i++) {
            size_t sub_consumed = 0;
            rc = resp_parse(buf + offset, buflen - offset, &items[i], &sub_consumed);
            if (rc != 0) {
                for (int64_t j = 0; j < i; j++) resp_value_free(&items[j]);
                free(items);
                return rc;
            }
            offset += sub_consumed;
        }

        out->type          = RESP_ARRAY;
        out->v.array.items = items;
        out->v.array.count = (size_t)count;
        *consumed = offset;
        return 0;
    }

    return -1; // unknown prefix
}

void resp_value_free(resp_value *v) {
    if (!v) return;
    switch (v->type) {
    case RESP_SIMPLE_STRING:
    case RESP_ERROR:
    case RESP_BULK_STRING:
        free(v->v.str.data);
        break;
    case RESP_ARRAY:
        for (size_t i = 0; i < v->v.array.count; i++)
            resp_value_free(&v->v.array.items[i]);
        free(v->v.array.items);
        break;
    case RESP_INTEGER:
    case RESP_NIL:
        break;
    }
}

static int buf_grow(char **buf, size_t *used, size_t *cap, size_t need) {
    if (*cap - *used >= need) return 0;
    size_t newcap = *cap ? *cap : 64;
    while (newcap - *used < need) newcap *= 2;
    char *nb = realloc(*buf, newcap);
    if (!nb) return -1;
    *buf = nb;
    *cap = newcap;
    return 0;
}

static int buf_append(char **buf, size_t *used, size_t *cap, const char *s, size_t n) {
    if (buf_grow(buf, used, cap, n) < 0) return -1;
    memcpy(*buf + *used, s, n);
    *used += n;
    return 0;
}

int resp_append_simple_string(char **buf, size_t *used, size_t *cap, const char *s, size_t len) {
    if (buf_append(buf, used, cap, "+", 1) < 0) return -1;
    if (buf_append(buf, used, cap, s, len) < 0) return -1;
    return buf_append(buf, used, cap, "\r\n", 2);
}

int resp_append_error(char **buf, size_t *used, size_t *cap, const char *s, size_t len) {
    if (buf_append(buf, used, cap, "-", 1) < 0) return -1;
    if (buf_append(buf, used, cap, s, len) < 0) return -1;
    return buf_append(buf, used, cap, "\r\n", 2);
}

int resp_append_integer(char **buf, size_t *used, size_t *cap, int64_t n) {
    char tmp[32];
    int tlen = snprintf(tmp, sizeof(tmp), ":%" PRId64 "\r\n", n);
    return buf_append(buf, used, cap, tmp, (size_t)tlen);
}

int resp_append_bulk_string(char **buf, size_t *used, size_t *cap, const char *s, size_t len) {
    char hdr[32];
    int hlen = snprintf(hdr, sizeof(hdr), "$%zu\r\n", len);
    if (buf_append(buf, used, cap, hdr, (size_t)hlen) < 0) return -1;
    if (buf_append(buf, used, cap, s, len) < 0) return -1;
    return buf_append(buf, used, cap, "\r\n", 2);
}

int resp_append_nil(char **buf, size_t *used, size_t *cap) {
    return buf_append(buf, used, cap, "$-1\r\n", 5);
}

int resp_append_array_header(char **buf, size_t *used, size_t *cap, size_t count) {
    char hdr[32];
    int hlen = snprintf(hdr, sizeof(hdr), "*%zu\r\n", count);
    return buf_append(buf, used, cap, hdr, (size_t)hlen);
}
