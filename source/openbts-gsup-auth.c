#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>

#include <talloc.h>

#include <osmocom/core/application.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <osmocom/core/utils.h>

#include <osmocom/gsm/ipa.h>
#include <osmocom/gsm/gsup.h>
#include <osmocom/crypt/auth.h>
#include <osmocom/gsupclient/gsup_client.h>

static struct osmo_gsup_client *client;
static int done;
static int result = 1;

static void print_hex(const uint8_t *p, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++)
        printf("%02x", p[i]);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    c = tolower((unsigned char)c);

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}

static int parse_hex_exact(const char *hex, uint8_t *out, size_t len)
{
    size_t i;

    if (strlen(hex) != len * 2)
        return -1;

    for (i = 0; i < len; i++) {
        int hi = hexval(hex[i * 2]);
        int lo = hexval(hex[i * 2 + 1]);

        if (hi < 0 || lo < 0)
            return -1;

        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return 0;
}

static int rx_cb(struct osmo_gsup_client *gsupc, struct msgb *msg)
{
    struct osmo_gsup_message rx = {0};
    const struct osmo_auth_vector *v;
    int rc;

    (void)gsupc;

    rc = osmo_gsup_decode(msgb_l2(msg), msgb_l2len(msg), &rx);

    if (rc < 0) {
        fprintf(stderr, "GSUP decode error: %d\n", rc);
        goto out;
    }

    if (rx.message_type == OSMO_GSUP_MSGT_SEND_AUTH_INFO_ERROR) {
        fprintf(stderr, "HLR authentication error, cause=%d\n", rx.cause);
        goto out;
    }

    if (rx.message_type != OSMO_GSUP_MSGT_SEND_AUTH_INFO_RESULT)
        goto out;

    if (rx.num_auth_vectors < 1) {
        fprintf(stderr, "HLR returned no authentication vectors\n");
        goto out;
    }

    v = &rx.auth_vectors[0];

    if (!(v->auth_types & OSMO_AUTH_TYPE_UMTS)) {
        fprintf(stderr, "HLR did not return a UMTS AKA vector\n");
        goto out;
    }

    printf("AKA RAND=");
    print_hex(v->rand, sizeof(v->rand));

    printf(" AUTN=");
    print_hex(v->autn, sizeof(v->autn));

    printf(" XRES=");
    print_hex(v->res, v->res_len);

    printf(" CK=");
    print_hex(v->ck, sizeof(v->ck));

    printf(" IK=");
    print_hex(v->ik, sizeof(v->ik));

    printf("\n");
    fflush(stdout);

    result = 0;

out:
    msgb_free(msg);
    done = 1;
    return 0;
}

static struct log_info_cat cats[] = {
    {
        .name = "DGSUPTEST",
        .description = "OpenBTS GSUP helper",
        .enabled = 1,
        .loglevel = LOGL_ERROR,
    },
};

static const struct log_info log_info = {
    .cat = cats,
    .num_cat = ARRAY_SIZE(cats),
};

int main(int argc, char **argv)
{
    void *ctx;
    struct ipaccess_unit *ipa_dev;
    struct osmo_gsup_message req = {0};

    uint8_t resync_rand[16];
    uint8_t resync_auts[14];

    int resync = 0;
    int rc;
    int i;

    if (argc != 2 && argc != 4) {
        fprintf(stderr,
                "usage:\n"
                "  %s IMSI\n"
                "  %s IMSI RAND AUTS\n",
                argv[0], argv[0]);
        return 2;
    }

    if (strlen(argv[1]) < 5 || strlen(argv[1]) >= sizeof(req.imsi)) {
        fprintf(stderr, "invalid IMSI\n");
        return 2;
    }

    if (argc == 4) {
        if (parse_hex_exact(argv[2], resync_rand,
                            sizeof(resync_rand)) < 0) {
            fprintf(stderr,
                    "invalid RAND: expected exactly 32 hex characters\n");
            return 2;
        }

        if (parse_hex_exact(argv[3], resync_auts,
                            sizeof(resync_auts)) < 0) {
            fprintf(stderr,
                    "invalid AUTS: expected exactly 28 hex characters\n");
            return 2;
        }

        resync = 1;
    }

    ctx = talloc_named_const(NULL, 0, "openbts-gsup-auth");
    if (!ctx)
        return 1;

    osmo_init_logging2(ctx, &log_info);

    ipa_dev = talloc_zero(ctx, struct ipaccess_unit);
    if (!ipa_dev)
        return 1;

    ipa_dev->site_id = 1;
    ipa_dev->bts_id = 1;
    ipa_dev->trx_id = 0;

    ipa_dev->unit_name =
        talloc_strdup(ipa_dev, "openbts-umts");

    ipa_dev->serno =
        talloc_strdup(ipa_dev, "openbts-umts");

    client = osmo_gsup_client_create2(
        ctx,
        ipa_dev,
        "127.0.0.1",
        OSMO_GSUP_PORT,
        rx_cb,
        NULL
    );

    if (!client) {
        fprintf(stderr, "cannot create GSUP client\n");
        return 1;
    }

    while (!osmo_gsup_client_is_connected(client))
        osmo_select_main(0);

    /*
     * Let IPA ID_GET / ID_RESP complete before sending GSUP.
     */
    for (i = 0; i < 20; i++) {
        osmo_select_main(1);
        usleep(50000);
    }

    req.message_type =
        OSMO_GSUP_MSGT_SEND_AUTH_INFO_REQUEST;

    req.message_class =
        OSMO_GSUP_MESSAGE_CLASS_SUBSCRIBER_MANAGEMENT;

    req.cn_domain = OSMO_GSUP_CN_DOMAIN_PS;
    req.num_auth_vectors = 1;

    strncpy(req.imsi, argv[1], sizeof(req.imsi) - 1);

    /*
     * UMTS AKA re-synchronization:
     *
     * RAND must be the RAND from the challenge that caused the
     * synchronization failure.
     *
     * AUTS is the 14-byte value returned by the USIM.
     */
    if (resync) {
        req.rand = resync_rand;
        req.auts = resync_auts;
    }

    rc = osmo_gsup_client_enc_send(client, &req);

    if (rc < 0) {
        fprintf(stderr, "GSUP send error: %d\n", rc);
        return 1;
    }

    while (!done)
        osmo_select_main(0);

    osmo_gsup_client_destroy(client);
    talloc_free(ctx);

    return result;
}
