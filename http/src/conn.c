/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A connection to a URL's host over the port's tgg/net.h: plain TCP for the
 * local network, TLS (Mbed TLS, src/mbedtls/) for everything else, checked
 * against the system's root certificates (tgg_net_trust_roots). */
#include "internal.h"

#include "tgg/events.h"
#include "tgg/net.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mbed TLS's entropy (MBEDTLS_ENTROPY_HARDWARE_ALT): the system's secure
 * generator, through the port */
int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen)
{
    (void) data;
    if (tgg_net_random(output, len) != TGG_NET_OK) {
        *olen = 0;
        return -1;
    }
    *olen = len;
    return 0;
}

/* Mbed TLS's clock (MBEDTLS_PLATFORM_MS_TIME_ALT): the port's, not the system's */
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t) tgg_net_time_ms();
}

/* ---- TLS, set up once ---------------------------------------------------------- */

static struct {
    int state; /* 0 not yet, 1 ready, -1 failed */
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt roots;
    mbedtls_ssl_config config;
} tls;

static int tls_ready(void)
{
    if (tls.state != 0) {
        return tls.state > 0;
    }
    tls.state = -1;
    if (psa_crypto_init() != PSA_SUCCESS) {
        tgg_log("the crypto library didn't start");
        return 0;
    }
    mbedtls_entropy_init(&tls.entropy);
    mbedtls_ctr_drbg_init(&tls.drbg);
    if (mbedtls_ctr_drbg_seed(&tls.drbg, mbedtls_entropy_func, &tls.entropy,
                              (const unsigned char*) "tgg.http", 8) != 0)
    {
        tgg_log("no secure random numbers");
        return 0;
    }
    mbedtls_x509_crt_init(&tls.roots);
    const tgg_net_cert* certs = NULL;
    const int n = tgg_net_trust_roots(&certs);
    int loaded = 0;
    for (int i = 0; i < n; i++) {
        loaded += mbedtls_x509_crt_parse_der(&tls.roots, certs[i].der, certs[i].len) == 0;
    }
    tgg_log("%d of the system's %d root certificates", loaded, n > 0 ? n : 0);
    mbedtls_ssl_config_init(&tls.config);
    if (mbedtls_ssl_config_defaults(&tls.config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        return 0;
    }
    mbedtls_ssl_conf_authmode(&tls.config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&tls.config, &tls.roots, NULL);
    mbedtls_ssl_conf_rng(&tls.config, mbedtls_ctr_drbg_random, &tls.drbg);
    tls.state = 1;
    return 1;
}

/* ---- Connections --------------------------------------------------------------- */

enum { STEP_CONNECT, STEP_HANDSHAKE, STEP_READY, STEP_FAILED };

struct conn {
    int step;
    int tls;
    char host[256];
    tgg_socket* sock;
    mbedtls_ssl_context ssl;
    int ssl_on;
    char error[320];
};

static char open_error[320];

const char* conn_open_error(void)
{
    return open_error;
}

static int is_private(const tgg_net_addr* a)
{
    const uint8_t* ip = a->ip;
    if (a->family == TGG_NET_IPV4) {
        return ip[0] == 10 || ip[0] == 127 || (ip[0] == 172 && (ip[1] & 0xF0) == 16) ||
               (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254) ||
               (ip[0] == 100 && (ip[1] & 0xC0) == 64);
    }
    static const uint8_t loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    return (ip[0] & 0xFE) == 0xFC || (ip[0] == 0xFE && (ip[1] & 0xC0) == 0x80) ||
           memcmp(ip, loopback, 16) == 0;
}

static void failed(conn* c, const char* why)
{
    if (c->step != STEP_FAILED) {
        c->step = STEP_FAILED;
        snprintf(c->error, sizeof c->error, "%s", why);
        tgg_log("%s", why);
    }
}

static int bio_send(void* ctx, const unsigned char* data, size_t len)
{
    const int n = tgg_net_write(((conn*) ctx)->sock, data, len);
    if (n >= 0) {
        return n;
    }
    return n == TGG_NET_ERR_FULL ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int bio_recv(void* ctx, unsigned char* data, size_t len)
{
    const int n = tgg_net_read(((conn*) ctx)->sock, data, len);
    if (n >= 0) {
        return n;
    }
    if (n == TGG_NET_EMPTY) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return n == TGG_NET_CLOSED ? 0 : MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

conn* conn_open(const url* u)
{
    open_error[0] = '\0';
    tgg_net_addr addr;
    const int found = tgg_net_resolve(u->host, u->port, &addr);
    if (found != TGG_NET_OK) {
        snprintf(open_error, sizeof open_error, "Couldn't find %s", u->host);
        return NULL;
    }
    if (!u->tls && !is_private(&addr)) {
        snprintf(open_error, sizeof open_error,
                 "%s isn't on the local network, so use https or wss", u->host);
        return NULL;
    }
    if (u->tls && !tls_ready()) {
        snprintf(open_error, sizeof open_error, "Secure connections aren't working here");
        return NULL;
    }
    conn* c = calloc(1, sizeof *c);
    if (c == NULL) {
        snprintf(open_error, sizeof open_error, "Out of memory");
        return NULL;
    }
    c->tls = u->tls;
    snprintf(c->host, sizeof c->host, "%s", u->host);
    const int err = tgg_net_connect(&addr, &c->sock);
    if (err != TGG_NET_OK) {
        snprintf(open_error, sizeof open_error, "Couldn't reach %s: %s", u->host,
                 tgg_net_error(err));
        free(c);
        return NULL;
    }
    c->step = STEP_CONNECT;
    return c;
}

int conn_step(conn* c)
{
    if (c->step == STEP_CONNECT) {
        const int up = tgg_net_connected(c->sock);
        if (up < 0) {
            char why[320];
            snprintf(why, sizeof why, "Couldn't reach %s", c->host);
            failed(c, why);
        } else if (up > 0) {
            if (!c->tls) {
                c->step = STEP_READY;
            } else {
                mbedtls_ssl_init(&c->ssl);
                c->ssl_on = 1;
                if (mbedtls_ssl_setup(&c->ssl, &tls.config) != 0 ||
                    mbedtls_ssl_set_hostname(&c->ssl, c->host) != 0)
                {
                    failed(c, "Couldn't start a secure connection");
                } else {
                    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, NULL);
                    c->step = STEP_HANDSHAKE;
                }
            }
        }
    }
    if (c->step == STEP_HANDSHAKE) {
        const int r = mbedtls_ssl_handshake(&c->ssl);
        if (r == 0) {
            c->step = STEP_READY;
        } else if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            char why[320];
            if (r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                snprintf(why, sizeof why, "%s's certificate isn't one this computer trusts",
                         c->host);
            } else {
                char detail[100];
                mbedtls_strerror(r, detail, sizeof detail);
                snprintf(why, sizeof why, "Couldn't connect securely to %s (%s)", c->host,
                         detail);
            }
            failed(c, why);
        }
    }
    return c->step == STEP_READY ? CONN_READY : c->step == STEP_FAILED ? CONN_FAILED
                                                                        : CONN_WORKING;
}

int conn_write(conn* c, const void* data, size_t len)
{
    if (conn_step(c) != CONN_READY) {
        return c->step == STEP_FAILED ? -1 : 0;
    }
    if (len == 0) {
        return 0;
    }
    if (!c->tls) {
        const int n = tgg_net_write(c->sock, data, len);
        if (n >= 0 || n == TGG_NET_ERR_FULL) {
            return n >= 0 ? n : 0;
        }
        failed(c, "The connection broke");
        return -1;
    }
    const int n = mbedtls_ssl_write(&c->ssl, data, len);
    if (n >= 0) {
        return n;
    }
    if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    failed(c, "The connection broke");
    return -1;
}

int conn_read(conn* c, void* buf, size_t cap)
{
    if (conn_step(c) != CONN_READY) {
        return c->step == STEP_FAILED ? -1 : 0;
    }
    if (!c->tls) {
        const int n = tgg_net_read(c->sock, buf, cap);
        if (n >= 0 || n == TGG_NET_EMPTY) {
            return n >= 0 ? n : 0;
        }
        return -1;
    }
    for (;;) {
        const int n = mbedtls_ssl_read(&c->ssl, buf, cap);
        if (n > 0) {
            return n;
        }
        if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return 0;
        }
        /* A TLS 1.3 server sends tickets after the handshake: read on */
        if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
            continue;
        }
        return -1; /* closed (0, close_notify) or broken */
    }
}

int conn_flush(conn* c, buf* out)
{
    while (out->len > 0) {
        const int n = conn_write(c, out->data, out->len);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        buf_drop(out, (size_t) n);
    }
    return 0;
}

const char* conn_error(const conn* c)
{
    return c->error;
}

void conn_close(conn* c)
{
    if (c == NULL) {
        return;
    }
    if (c->ssl_on) {
        if (c->step == STEP_READY) {
            (void) mbedtls_ssl_close_notify(&c->ssl);
        }
        mbedtls_ssl_free(&c->ssl);
    }
    tgg_net_close(c->sock);
    free(c);
}

/* ---- Each frame ------------------------------------------------------------------ */

static void on_frame(void)
{
    request_pump_all();
    ws_pump_all();
}
TGG_ON(frame, on_frame);
