/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2017 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:   ZiHang Gao <ocdoco@gmail.com>                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"

#include "zend_smart_str.h"
#include "zend_exceptions.h"
#include "ext/json/php_json.h"
#include "ext/standard/base64.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"

/* OpenSSL includes */
#include <openssl/conf.h>

#include "php_jwt.h"

static options_t *jwt_options;

/* string to algorithm */
jwt_alg_t jwt_str_alg(const char *alg)
{
    if (alg == NULL)
        return JWT_ALG_INVAL;

    if (!strcasecmp(alg, "none"))
        return JWT_ALG_NONE;
    else if (!strcasecmp(alg, "HS256"))
        return JWT_ALG_HS256;
    else if (!strcasecmp(alg, "HS384"))
        return JWT_ALG_HS384;
    else if (!strcasecmp(alg, "HS512"))
        return JWT_ALG_HS512;
    else if (!strcasecmp(alg, "RS256"))
        return JWT_ALG_RS256;
    else if (!strcasecmp(alg, "RS384"))
        return JWT_ALG_RS384;
    else if (!strcasecmp(alg, "RS512"))
        return JWT_ALG_RS512;
    else if (!strcasecmp(alg, "ES256"))
        return JWT_ALG_ES256;
    else if (!strcasecmp(alg, "ES384"))
        return JWT_ALG_ES384;
    else if (!strcasecmp(alg, "ES512"))
        return JWT_ALG_ES512;

    return JWT_ALG_INVAL;
}

/* jwt sign */
static int jwt_sign(jwt_t *jwt, char **out, unsigned int *len)
{
    switch (jwt->alg) {
    /* HMAC */
    case JWT_ALG_HS256:
    case JWT_ALG_HS384:
    case JWT_ALG_HS512:
        return jwt_sign_sha_hmac(jwt, out, len);

    /* RSA */
    case JWT_ALG_RS256:
    case JWT_ALG_RS384:
    case JWT_ALG_RS512:

    /* ECC */
    case JWT_ALG_ES256:
    case JWT_ALG_ES384:
    case JWT_ALG_ES512:
        return jwt_sign_sha_pem(jwt, out, len);

    /* You wut, mate? */
    default:
        return EINVAL;
    }
}

/* jwt verify */
static int jwt_verify(jwt_t *jwt, const char *sig)
{
    switch (jwt->alg) {
    /* HMAC */
    case JWT_ALG_HS256:
    case JWT_ALG_HS384:
    case JWT_ALG_HS512:
        return jwt_verify_sha_hmac(jwt, sig);

    /* RSA */
    case JWT_ALG_RS256:
    case JWT_ALG_RS384:
    case JWT_ALG_RS512:

    /* ECC */
    case JWT_ALG_ES256:
    case JWT_ALG_ES384:
    case JWT_ALG_ES512:
        return jwt_verify_sha_pem(jwt, sig);

    /* You wut, mate? */
    default:
        return EINVAL;
    }
}

int jwt_new(jwt_t **jwt)
{
    if (!jwt) {
        return EINVAL;
    }

    *jwt = emalloc(sizeof(jwt_t));
    if (!*jwt) {
        return ENOMEM;
    }

    memset(*jwt, 0, sizeof(jwt_t));

    return 0;
}

void jwt_free(jwt_t *jwt)
{
    if (!jwt) {
        return;
    }

    efree(jwt);
}

void jwt_b64_url_encode_ex(char *str)
{
    int len = strlen(str);
    int i, t;

    for (i = t = 0; i < len; i++) {
        switch (str[i]) {
        case '+':
            str[t++] = '-';
            break;
        case '/':
            str[t++] = '_';
            break;
        case '=':
            break;
        default:
            str[t++] = str[i];
        }
    }

    str[t] = '\0';
}

char *jwt_b64_url_encode(zend_string *input)
{
    zend_string *b64_str = NULL;
    b64_str = php_base64_encode((const unsigned char *)ZSTR_VAL(input), ZSTR_LEN(input));

    /* replace str */
    zend_string *new = zend_string_dup(b64_str, 0);

    jwt_b64_url_encode_ex(ZSTR_VAL(new));

    zend_string_free(new);
    zend_string_free(b64_str);

    return ZSTR_VAL(new);
}

zend_string *jwt_b64_url_decode(const char *src)
{
    char *new;
    int len, i, z;

    /* Decode based on RFC-4648 URI safe encoding. */
    len = strlen(src);
    new = alloca(len + 4);
    if (!new) {
        return NULL;
    }

    for (i = 0; i < len; i++) {
        switch (src[i]) {
        case '-':
            new[i] = '+';
            break;
        case '_':
            new[i] = '/';
            break;
        default:
            new[i] = src[i];
        }
    }
    z = 4 - (i % 4);
    if (z < 4) {
        while (z--) {
            new[i++] = '=';
        }	
    }
    new[i] = '\0';

    /* base64 decode */
    return php_base64_decode_ex((const unsigned char *)new, strlen(new), 1);
}

char *jwt_hash_str_find_str(zval *arr, char *key)
{
    char *str = NULL;
    zval *zv = zend_hash_str_find(Z_ARRVAL_P(arr), key, strlen(key));

    if (zv != NULL) {
        str = Z_STRVAL_P(zv);
    }

    return str;
}

long jwt_hash_str_find_long(zval *arr, char *key)
{
    zval *zv = zend_hash_str_find(Z_ARRVAL_P(arr), key, strlen(key));

    if (zv != NULL) {
        return Z_LVAL_P(zv);
    }

    return 0;
}

int jwt_verify_claims(zval *arr, char *key, char *str)
{
    char *rs = jwt_hash_str_find_str(arr, key);
    if (rs && strcmp(rs, str)) {
        return FAILURE;
    }

    return 0;
}

int jwt_verify_body(char *body, zval *return_value)
{
    char *err_msg = NULL;
    time_t curr_time = time((time_t*)NULL);
    zend_string *vs = jwt_b64_url_decode(body);
    php_json_decode_ex(return_value, ZSTR_VAL(vs), ZSTR_LEN(vs), PHP_JSON_OBJECT_AS_ARRAY, 512);
    zend_string_free(vs);

    /* Expiration */
    if (jwt_options->expiration && (curr_time - jwt_options->leeway) >= jwt_options->expiration) {
        err_msg = "Expired token";
    }

    /* not before */
    if (jwt_options->not_before && jwt_options->not_before > (curr_time + jwt_options->leeway)) {
        struct tm *timeinfo;
        char buf[128];

        timeinfo = localtime(&curr_time);
        strftime(buf, sizeof(buf), "Cannot handle token prior to %Y-%m-%d %H:%M:%S", timeinfo);
        err_msg = buf;
    }

    /* iss */
    if (jwt_options->iss && jwt_verify_claims(return_value, "iss", jwt_options->iss))
        err_msg = "Iss verify fail";

    /* iat */
    if (jwt_options->iat && jwt_options->iat > (curr_time + jwt_options->leeway)) {
        struct tm *timeinfo;
        char buf[128];

        timeinfo = localtime(&curr_time);
        strftime(buf, sizeof(buf), "Cannot handle token prior to %Y-%m-%d %H:%M:%S", timeinfo);
        err_msg = buf;
    }

    /* jti */
    if (jwt_options->jti && jwt_verify_claims(return_value, "jti", jwt_options->jti))
        err_msg = "Tti verify fail";

    /* aud */
    if (jwt_options->aud && jwt_verify_claims(return_value, "aud", jwt_options->aud))
        err_msg = "Aud verify fail";

    /* sub */
    if (jwt_options->sub && jwt_verify_claims(return_value, "sub", jwt_options->sub))
        err_msg = "Sub verify fail";

    if (err_msg) {
        zend_throw_exception(zend_ce_exception, err_msg, 0);
        return FAILURE;
    }

    return 0;
}

int jwt_parse_options(zval *options)
{
    /* check options */
    if (options != NULL) {
        switch(Z_TYPE_P(options)) {
        case IS_ARRAY:
            {
                /* check algorithm */
                char *alg = jwt_hash_str_find_str(options, "algorithm");
                if (alg) {
                    jwt_options->algorithm = alg;
                }
                
                /* options */
                jwt_options->leeway = jwt_hash_str_find_long(options, "leeway");
                jwt_options->iss = jwt_hash_str_find_str(options, "iss");
                jwt_options->jti = jwt_hash_str_find_str(options, "jti");
                jwt_options->aud = jwt_hash_str_find_str(options, "aud");
                jwt_options->sub = jwt_hash_str_find_str(options, "sub");
            }
            break;
        case IS_NULL:
        case IS_FALSE:
            jwt_options->algorithm = "none";
            break;
        default:
            break;
        }
    }

    return 0;
}

PHP_FUNCTION(jwt_encode)
{
    zval *claims = NULL, header;
    zend_string *key = NULL;
    smart_str json_header = {0}, json_claims = {0}, segments = {0};

    char *sig = NULL, *alg = "HS256";
    unsigned int sig_len;
    size_t alg_len;
    jwt_t *jwt = NULL;
    
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "aS|s", &claims, &key, &alg, &alg_len) == FAILURE) {
        return;
    }

    /* init jwt */
    jwt_new(&jwt);

    /* check algorithm */
    jwt->alg = jwt_str_alg(alg);

    printf("%d\n", jwt->alg);

    if (jwt->alg == JWT_ALG_INVAL) {
        zend_throw_exception(zend_ce_exception, "Algorithm not supported", 0);
        goto encode_done;
    }

    printf("%s\n", "=====");

    /* set expiration and not before */
    jwt_options->expiration = jwt_hash_str_find_long(claims, "exp");
    jwt_options->not_before = jwt_hash_str_find_long(claims, "nbf");
    jwt_options->iat = jwt_hash_str_find_long(claims, "iat");
    
    /* init */
    array_init(&header);

    /* JWT header array */
    add_assoc_string(&header, "typ", "JWT");
    add_assoc_string(&header, "alg", alg);

    /* json encode */
    php_json_encode(&json_header, &header, 0);
    php_json_encode(&json_claims, claims, 0);

    zval_ptr_dtor(&header);

    /* base64 encode */
    smart_str_appends(&segments, jwt_b64_url_encode(json_header.s));
    smart_str_appends(&segments, ".");
    smart_str_appends(&segments, jwt_b64_url_encode(json_claims.s));

    smart_str_free(&json_header);
    smart_str_free(&json_claims);

    /* sign */
    if (jwt->alg == JWT_ALG_NONE) {
        // alg none.
		smart_str_appendl(&segments, ".", 1);
    } else {
        /* set jwt struct */
        jwt->key = key;
        jwt->str = segments.s;

        /* sign */
        if (jwt_sign(jwt, &sig, &sig_len)) {
            zend_throw_exception(zend_ce_exception, "Signature error", 0);
            goto encode_done;
        }

        /* string concatenation */
        smart_str_appends(&segments, ".");

        zend_string *sig_str = zend_string_init(sig, sig_len, 0);

        smart_str_appends(&segments, jwt_b64_url_encode(sig_str));
        zend_string_free(sig_str);
    }

    smart_str_0(&segments);

encode_done:
    /* free */
    if (sig)
        efree(sig);

    jwt_free(jwt);

    if (segments.s) {
        RETURN_STR(segments.s);
    }
}

PHP_FUNCTION(jwt_decode)
{
    zend_string *token = NULL, *key = NULL;
    zval *options = NULL;
    smart_str segments = {0};
    char *body = NULL, *sig = NULL;
    jwt_t *jwt = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "SS|z", &token, &key, &options) == FAILURE) {
        return;
    }

    char *head = estrdup(ZSTR_VAL(token));

    /* jwt init */
    jwt_new(&jwt);

    /* Parse options */
    if (jwt_parse_options(options) == FAILURE) {
        zend_throw_exception(zend_ce_exception, "Options parse error", 0);
        goto decode_done;
    }

    /* Algorithm */
    jwt->alg = jwt_str_alg(jwt_options->algorithm);

    if (jwt->alg == JWT_ALG_INVAL) {
        zend_throw_exception(zend_ce_exception, "Algorithm not supported", 0);
        goto decode_done;
    }

    /* Find the components. */
    for (body = head; body[0] != '.'; body++) {
        if (body[0] == '\0') {
            goto decode_done;
        }	
    }

    body[0] = '\0';
    body++;

    for (sig = body; sig[0] != '.'; sig++) {
        if (sig[0] == '\0') {
            goto decode_done;
        }
    }

    sig[0] = '\0';
    sig++;

    /* verify head */
    zval zv;
    zend_string *json_h = jwt_b64_url_decode(head);

    if (!json_h) {
        zend_throw_exception(zend_ce_exception, "Base64 decode error", 0);
        goto decode_done;
    }

    php_json_decode_ex(&zv, ZSTR_VAL(json_h), ZSTR_LEN(json_h), PHP_JSON_OBJECT_AS_ARRAY, 512);
    zend_string_free(json_h);

    if (Z_TYPE(zv) == IS_ARRAY) {
        zval *zalg = zend_hash_str_find(Z_ARRVAL(zv), "alg", strlen("alg"));

        zval_ptr_dtor(&zv);

        if (strcmp(Z_STRVAL_P(zalg), jwt_options->algorithm)) {
            zend_throw_exception(zend_ce_exception, "Algorithm not allowed", 0);
            goto decode_done;
        }
    } else {
        zend_throw_exception(zend_ce_exception, "Json decode error", 0);
        goto decode_done;
    }

    /* parse body */
    if (jwt_verify_body(body, return_value) == FAILURE) {
        goto decode_done;
    }

    /* verify */
    if (jwt->alg == JWT_ALG_NONE) {
        /* done */
    } else {
        /* set jwt struct */
        jwt->key = key;

        smart_str_appends(&segments, head);
        smart_str_appends(&segments, ".");
        smart_str_appends(&segments, body);

        jwt->str = segments.s;

        if (jwt_verify(jwt, sig)) {
            zend_throw_exception(zend_ce_exception, "Signature verification failed", 0);
        }
    }

    smart_str_free(&segments);

decode_done:
    efree(head);
    jwt_free(jwt);
}

const zend_function_entry jwt_functions[] = {
    PHP_FE(jwt_encode,	NULL)
    PHP_FE(jwt_decode,	NULL)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(jwt)
{
    jwt_options = emalloc(sizeof(options_t));
    if (!jwt_options) {
        return FAILURE;
    }

    memset(jwt_options, 0, sizeof(options_t));

    /* Init options */
    jwt_options->leeway = 0;
    jwt_options->algorithm = "HS256";

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(jwt)
{
    /* free */
    return SUCCESS;
}

PHP_MINFO_FUNCTION(jwt)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "jwt support", "enabled");
    php_info_print_table_row(2, "Version", PHP_JWT_VERSION);
    php_info_print_table_row(2, "JWT SSL library", "OpenSSL");

    /* openssl version info */
    php_info_print_table_row(2, "OpenSSL Library Version", SSLeay_version(SSLEAY_VERSION));
    php_info_print_table_row(2, "OpenSSL Header Version", OPENSSL_VERSION_TEXT);

    php_info_print_table_end();
}

static const zend_module_dep jwt_deps[] = {
    ZEND_MOD_REQUIRED("json")
    ZEND_MOD_END
};

zend_module_entry jwt_module_entry = {
    STANDARD_MODULE_HEADER_EX, NULL,
    jwt_deps,
    "jwt",
    jwt_functions,
    PHP_MINIT(jwt),
    PHP_MSHUTDOWN(jwt),
    NULL,		/* Replace with NULL if there's nothing to do at request start */
    NULL,	/* Replace with NULL if there's nothing to do at request end */
    PHP_MINFO(jwt),
    PHP_JWT_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_JWT
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(jwt)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
