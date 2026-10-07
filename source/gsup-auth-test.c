#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

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

static struct osmo_gsup_client *g_client;
static int done = 0;

static void dump_hex(const char *name, const uint8_t *p, size_t len)
{
    printf("%-5s = ", name);
    for (size_t i = 0; i < len; i++)
        printf("%02x", p[i]);
    printf("\n");
}

static int gsup_rx_cb(struct osmo_gsup_client *gsupc, struct msgb *msg)
{
    struct osmo_gsup_message rx = {0};
    int rc;

    rc = osmo_gsup_decode(msgb_l2(msg), msgb_l2len(msg), &rx);
    if (rc < 0) {
        fprintf(stderr, "GSUP decode failed: %d\n", rc);
        msgb_free(msg);
        done = 1;
        return rc;
    }

    printf("RX GSUP: %s\n",
           osmo_gsup_message_type_name(rx.message_type));

    if (rx.message_type == OSMO_GSUP_MSGT_SEND_AUTH_INFO_ERROR) {
        fprintf(stderr, "HLR returned authentication error, cause=%d\n",
                rx.cause);
        msgb_free(msg);
        done = 1;
        return 0;
    }

    if (rx.message_type != OSMO_GSUP_MSGT_SEND_AUTH_INFO_RESULT) {
        printf("Ignoring unrelated GSUP message\n");
        msgb_free(msg);
        return 0;
    }

    if (rx.num_auth_vectors < 1) {
        fprintf(stderr, "HLR returned no authentication vectors\n");
        msgb_free(msg);
        done = 1;
        return 0;
    }

    const struct osmo_auth_vector *v = &rx.auth_vectors[0];

    printf("\nAuthentication vector received\n");
    printf("auth_types = 0x%x\n", v->auth_types);

    dump_hex("RAND", v->rand, sizeof(v->rand));

    if (v->auth_types & OSMO_AUTH_TYPE_UMTS) {
        dump_hex("AUTN", v->autn, sizeof(v->autn));
        dump_hex("XRES", v->res, v->res_len);
        dump_hex("CK",   v->ck, sizeof(v->ck));
        dump_hex("IK",   v->ik, sizeof(v->ik));

        printf("\nUMTS AKA vector: YES\n");
    } else {
        printf("\nWARNING: vector is not marked as UMTS AKA\n");
    }

    msgb_free(msg);
    done = 1;
    return 0;
}

static struct log_info_cat log_categories[] = {
    {
        .name = "DTEST",
        .description = "GSUP authentication test",
        .enabled = 1,
        .loglevel = LOGL_NOTICE,
    },
};

static const struct log_info log_info = {
    .cat = log_categories,
    .num_cat = ARRAY_SIZE(log_categories),
};

int main(void)
{
    void *ctx;
    struct osmo_gsup_message req = {0};
    struct ipaccess_unit *ipa_dev;
    int rc;

    ctx = talloc_named_const(NULL, 0, "gsup-auth-test");
    if (!ctx) {
        fprintf(stderr, "talloc failed\n");
        return 1;
    }

    osmo_init_logging2(ctx, &log_info);

    printf("Connecting to OsmoHLR 127.0.0.1:4222...\n");

    ipa_dev = talloc_zero(ctx, struct ipaccess_unit);
    if (!ipa_dev) {
        fprintf(stderr, "Could not allocate IPA identity\n");
        return 1;
    }

    ipa_dev->site_id = 1;
    ipa_dev->bts_id = 1;
    ipa_dev->trx_id = 0;

    ipa_dev->unit_name = talloc_strdup(ipa_dev, "openbts-umts-test");
    ipa_dev->serno = talloc_strdup(ipa_dev, "openbts-umts-test");

    g_client = osmo_gsup_client_create2(
        ctx,
        ipa_dev,
        "127.0.0.1",
        OSMO_GSUP_PORT,
        gsup_rx_cb,
        NULL
    );

    if (!g_client) {
        fprintf(stderr, "Could not create GSUP client\n");
        return 1;
    }

    while (!osmo_gsup_client_is_connected(g_client))
        osmo_select_main(0);

    printf("GSUP TCP connected, completing IPA identity handshake...\\n");

    for (int i = 0; i < 20; i++) {
        osmo_select_main(1);
        usleep(50000);
    }

    printf("IPA handshake should now be complete\\n");

    req.message_type = OSMO_GSUP_MSGT_SEND_AUTH_INFO_REQUEST;
    req.message_class = OSMO_GSUP_MESSAGE_CLASS_SUBSCRIBER_MANAGEMENT;

    strcpy(req.imsi, "991420000000002");

    req.cn_domain = OSMO_GSUP_CN_DOMAIN_PS;

    /* Ask HLR for exactly one vector */
    req.num_auth_vectors = 1;

    printf("Requesting AKA vector for IMSI %s...\n", req.imsi);

    rc = osmo_gsup_client_enc_send(g_client, &req);
    if (rc < 0) {
        fprintf(stderr, "GSUP send failed: %d\n", rc);
        return 1;
    }

    while (!done)
        osmo_select_main(0);

    osmo_gsup_client_destroy(g_client);
    talloc_free(ctx);

    return 0;
}
