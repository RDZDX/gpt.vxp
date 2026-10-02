#define inline  
#include "bearssl.h"
//#include "certificates.h"
#include "vmsys.h"
#include "vmsock.h"
#include "vmstdlib.h"
#include "Console_io.h"
#include "thread.h"

const char* key = ""; //Put your GPT API key there 

vm_time_t t;

bool connected = false;
bool network_err = false;

/* keep declarations if you still use them elsewhere */
// static unsigned long is_leap(int y);
// static unsigned long bearssl_days(int year, int month, int day);

/* --- INSECURE X509 (NO CERT CHECK) --- */
typedef struct {
    const br_x509_class* vtable;
    int done_cert;
    br_x509_decoder_context ctx;
} br_x509_insecure_context;

static void insecure_start_chain(const br_x509_class** ctx, const char* server_name) {
    br_x509_insecure_context* xc = (br_x509_insecure_context*)ctx;
    br_x509_decoder_init(&xc->ctx, NULL, NULL);
    xc->done_cert = 0;
    (void)server_name;
}
static void insecure_start_cert(const br_x509_class** ctx, uint32_t length) {
    (void)ctx; (void)length;
}
static void insecure_append(const br_x509_class** ctx, const unsigned char* buf, size_t len) {
    br_x509_insecure_context* xc = (br_x509_insecure_context*)ctx;
    if (!xc->done_cert) br_x509_decoder_push(&xc->ctx, buf, len);
}
static void insecure_end_cert(const br_x509_class** ctx) {
    br_x509_insecure_context* xc = (br_x509_insecure_context*)ctx;
    xc->done_cert = 1;
}
static unsigned insecure_end_chain(const br_x509_class** ctx) {
    (void)ctx;
    return 0; // accept chain
}
static const br_x509_pkey* insecure_get_pkey(const br_x509_class* const* ctx, unsigned* usages) {
    const br_x509_insecure_context* xc = (const br_x509_insecure_context*)ctx;
    if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return br_x509_decoder_get_pkey((br_x509_decoder_context*)&xc->ctx);
}

static const br_x509_class insecure_vtable = {
    sizeof(br_x509_insecure_context),
    insecure_start_chain,
    insecure_start_cert,
    insecure_append,
    insecure_end_cert,
    insecure_end_chain,
    insecure_get_pkey
};

static br_x509_insecure_context insecure;
/* --- END INSECURE X509 --- */

int sock_read(void* ctx, unsigned char* buf, size_t len){
    for (;;) {
        int rlen;

        rlen = vm_tcp_read(*(int*)ctx, buf, len);
        if (rlen <= 0) {
            if (!network_err) {
                thread_next();
                continue;
            }
            return -1;
        }
        return (int)rlen;
    }
}

int sock_write(void* ctx, const unsigned char* buf, size_t len){
    for (;;) {
        int wlen;

        wlen = vm_tcp_write(*(int*)ctx, (void*)buf, len);
        if (wlen <= 0) {
            if (!network_err) {
                thread_next();
                continue;
            }
            return -1;
        }
        return (int)wlen;
    }
}

void tcp_callback(VMINT handle, VMINT event) {
    switch (event) {
    case VM_TCP_EVT_CONNECTED:
        connected = true;
        break;
    case VM_TCP_EVT_PIPE_BROKEN:
    case VM_TCP_EVT_HOST_NOT_FOUND:
    case VM_TCP_EVT_PIPE_CLOSED:
        network_err = true;
        break;
    }
}

br_ssl_client_context sc;
br_x509_minimal_context xc;
unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];

void skip_to(br_sslio_context &ioc, const char* find_str)
{
    int len = strlen(find_str);

    if (len <= 0)
        return;

    int coincidence = 0;

    while (true) {
        char c;

        int rlen = br_sslio_read(&ioc, &c, 1);

        if (rlen <= 0)
            break;

        if (c == find_str[coincidence]) {
            coincidence++;

            if (coincidence == len)
                break;
        }
        else {
            coincidence = 0;

            if (c == find_str[0])
                coincidence = 1;
        }
    }
}

void https_request(
    const char* host,
    const char* path,
    const char* body,
    char* rbody,
    int &rbody_size
){
    connected = false;
    network_err = false;

    int tcp_handle = vm_tcp_connect(
        host,
        443,
        VM_APN_USER_DEFINE,
        tcp_callback
    );

    cprintf("0%%");

    while (!connected && !network_err)
        thread_next();

    if (network_err) {
        cprintf("\nTCP connection failed\n");
        rbody_size = 0;
        rbody[0] = '\0';
        return;
    }

    cprintf("\b\b25%%");

    // NO CERT CHECK: no trust anchors
    br_ssl_client_init_full(&sc, &xc, NULL, 0);
    memset(&insecure, 0, sizeof(insecure));
    insecure.vtable = &insecure_vtable;
    br_ssl_engine_set_x509(&sc.eng, &insecure.vtable);

    // remove time-based x509 validation block:
    // vm_get_time(&t);
    // br_x509_minimal_set_time(...);

    br_ssl_engine_set_buffer(
        &sc.eng,
        iobuf,
        sizeof iobuf,
        1
    );

    br_ssl_client_reset(&sc, host, 0);

    br_sslio_context ioc;

    br_sslio_init(
        &ioc,
        &sc.eng,
        sock_read,
        &tcp_handle,
        sock_write,
        &tcp_handle
    );

    {
        char head_tmp[1024];

        sprintf(
            head_tmp,
            "POST %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            path,
            host,
            key,
            (int)strlen(body)
        );

        br_sslio_write_all(
            &ioc,
            head_tmp,
            strlen(head_tmp)
        );
    }

    br_sslio_write_all(
        &ioc,
        body,
        strlen(body)
    );

    br_sslio_flush(&ioc);

    cprintf("\b\b\b50%%");

    char status_line[128];
    int status_pos = 0;

    while (status_pos < (int)sizeof(status_line) - 1) {
        char c;
        int rlen = br_sslio_read(&ioc, &c, 1);

        if (rlen <= 0)
            break;

        if (c == '\r') {
            char next;
            br_sslio_read(&ioc, &next, 1);

            if (next == '\n')
                break;

            continue;
        }

        status_line[status_pos++] = c;
    }

    status_line[status_pos] = '\0';

    skip_to(ioc, "\r\n\r\n");

    cprintf("\b\b\b75%%");

    int readed = 0;

    while (readed < rbody_size - 1) {
        int rlen = br_sslio_read(
            &ioc,
            rbody + readed,
            rbody_size - 1 - readed
        );

        if (rlen <= 0)
            break;

        readed += rlen;
    }

    rbody[readed] = '\0';
    rbody_size = readed;

    vm_tcp_close(tcp_handle);

    if (br_ssl_engine_current_state(&sc.eng) == BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(&sc.eng);
        if (err != 0) cprintf("SSL error %d\n", err);
    } else {
        cprintf("socket closed without proper SSL termination\n");
    }

    cprintf("\b\b\b   \b\b\b");
}