/* tls_test: mbedTLS 3.6 smoke test for iPhone OS 3 — TCP connect + TLS handshake + HTTP GET.
 * Usage: tls_test [host] [port]   (default apresolve.spotify.com 443). Certificate verification is
 * OPTIONAL here (no CA bundle on the device), it only reports the verify result. */
#include <stdio.h>
#include <string.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/error.h>

static void die(const char *what, int rc) { char b[128]; mbedtls_strerror(rc, b, sizeof b); printf("%s failed: -0x%04x %s\n", what, -rc, b); }

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "apresolve.spotify.com";
    const char *port = argc > 2 ? argv[2] : "443";
    mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
    mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg;
    mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg);
    int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const unsigned char *)"tls_test", 8);
    if (rc) { die("ctr_drbg_seed (entropy)", rc); return 1; }
    printf("entropy OK\n");
    if ((rc = mbedtls_net_connect(&net, host, port, MBEDTLS_NET_PROTO_TCP))) { die("net_connect", rc); return 1; }
    printf("TCP connected to %s:%s\n", host, port);
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_setup(&ssl, &conf);
    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, NULL);
    while ((rc = mbedtls_ssl_handshake(&ssl)) != 0)
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) { die("ssl_handshake", rc); return 1; }
    printf("TLS handshake OK: %s %s, verify result 0x%08x (no CA bundle, so non-zero is expected)\n",
           mbedtls_ssl_get_version(&ssl), mbedtls_ssl_get_ciphersuite(&ssl), (unsigned)mbedtls_ssl_get_verify_result(&ssl));
    char req[256]; snprintf(req, sizeof req, "GET / HTTP/1.0\r\nHost: %s\r\n\r\n", host);
    mbedtls_ssl_write(&ssl, (unsigned char *)req, strlen(req));
    unsigned char buf[512]; int n = mbedtls_ssl_read(&ssl, buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; char *eol = strchr((char *)buf, '\n'); if (eol) *eol = 0; printf("HTTP: %s\n", buf); }
    mbedtls_ssl_close_notify(&ssl); mbedtls_net_free(&net);
    return 0;
}
