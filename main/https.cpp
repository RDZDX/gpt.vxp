#define inline  
#include "bearssl.h"
#include "vmsys.h"
#include "vmsock.h"
#include "vmstdlib.h"
#include "Console_io.h"
#include "thread.h"

const char* key = ""; //Put your GPT API key there 

bool connected = false;
bool network_err = false;

static br_ssl_session_parameters saved_session;
static int has_saved_session = 0;

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

static br_x509_class insecure_x509_vtable;

static unsigned insecure_x509_end_chain(const br_x509_class **ctx);
static const br_x509_pkey *insecure_x509_get_pkey(const br_x509_class *const *ctx, unsigned *usages);

static unsigned insecure_x509_end_chain(const br_x509_class **ctx)
{
    br_x509_minimal_vtable.end_chain(ctx);
    return 0;
}

static const br_x509_pkey *insecure_x509_get_pkey(
    const br_x509_class *const *ctx,
    unsigned *usages)
{
    const br_x509_minimal_context *minimal = (const br_x509_minimal_context *)ctx;

    if (minimal->pkey.key_type != BR_KEYTYPE_RSA && minimal->pkey.key_type != BR_KEYTYPE_EC)
        return NULL;

    if (usages)
        *usages = BR_KEYTYPE_SIGN | BR_KEYTYPE_KEYX;
    return &minimal->pkey;
}

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

            // Current character might itself be the
            // beginning of the pattern.
            if (c == find_str[0])
                coincidence = 1;
        }
    }
}

static unsigned long is_leap(int y)
{
    return ((y % 4) == 0) && (((y % 100) != 0) || ((y % 400) == 0));
}

static unsigned long bearssl_days(int year, int month, int day){

    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    unsigned long d = 0;
    int y, m;

    for (y = 0; y < year; y++) {
        d += is_leap(y) ? 366 : 365;
    }

    for (m = 1; m < month; m++) {
        d += mdays[m - 1];

        if (m == 2 && is_leap(year))
            d++;
    }

    d += day - 1;

    return d;
}

void https_request(const char* host, const char* path, const char* body, char* rbody, int &rbody_size)
{

    vm_time_t t;
    connected = false;
    network_err = false;

    int tcp_handle = vm_tcp_connect(host, 443, VM_APN_USER_DEFINE, tcp_callback);

    while (!connected && !network_err)
        thread_next();

    if (network_err) {
        cprintf("\nTCP connection failed\n");
        has_saved_session = 0;   // stale session may exist after network failure
        rbody_size = 0;
        rbody[0] = '\0';
        return;
    }

    br_ssl_client_init_full(&sc, &xc, NULL, 0);

    insecure_x509_vtable = br_x509_minimal_vtable;
    insecure_x509_vtable.end_chain = insecure_x509_end_chain;
    insecure_x509_vtable.get_pkey  = insecure_x509_get_pkey;
    xc.vtable = &insecure_x509_vtable;

    vm_get_time(&t);
    {
        unsigned long days    = bearssl_days(t.year, t.mon, t.day);
        unsigned long seconds = t.hour * 3600UL + t.min * 60UL + t.sec;
        br_x509_minimal_set_time(&xc, days, seconds);
    }

    br_ssl_engine_set_buffer(&sc.eng, iobuf, sizeof iobuf, 1);

    /* restore previously saved session (if any) */
    if (has_saved_session) {
        br_ssl_engine_set_session_parameters(&sc.eng, &saved_session);
    }

    /* last arg enables resume attempt when non-zero */
    br_ssl_client_reset(&sc, host, has_saved_session ? 1 : 0);

    br_sslio_context ioc;
    br_sslio_init(&ioc, &sc.eng, sock_read, &tcp_handle, sock_write, &tcp_handle);

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
        br_sslio_write_all(&ioc, head_tmp, strlen(head_tmp));
    }

    br_sslio_write_all(&ioc, body, strlen(body));
    br_sslio_flush(&ioc);

    skip_to(ioc, "\r\n\r\n");

    int readed = 0;
    while (readed < rbody_size - 1) {
        int rlen = br_sslio_read(&ioc, rbody + readed, rbody_size - 1 - readed);
        if (rlen <= 0)
            break;
        readed += rlen;
    }

    rbody[readed] = '\0';
    rbody_size = readed;

{
    int err = br_ssl_engine_last_error(&sc.eng);

    if ((err == 0 || err == BR_ERR_IO) &&
        readed > 0 &&
        sc.eng.session.session_id_len > 0)
    {
        br_ssl_engine_get_session_parameters(&sc.eng, &saved_session);
        has_saved_session = 1;
    } else {
        has_saved_session = 0;
    }
}

    vm_tcp_close(tcp_handle);

if (br_ssl_engine_current_state(&sc.eng) == BR_SSL_CLOSED) {
    int err = br_ssl_engine_last_error(&sc.eng);
    if (err != 0 && err != BR_ERR_IO) {
        cprintf("SSL error %d\n", err);
        has_saved_session = 0;
    }
} else {
    cprintf("socket closed without proper SSL termination\n");
    /* keep has_saved_session as-is */
}

}
