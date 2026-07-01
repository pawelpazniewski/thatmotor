#include "usb_console.h"

#include <stdio.h>
#include <string.h>

#include "blackbox.h"
#include "blackbox_csv.h"
#include "blackbox_record.h"
#include "control_loop.h"
#include "esp_console.h"
#include "esp_log.h"
#include "params_cmd.h"
#include "settings_model.h"

static const char *TAG = "usb_console";

/* REPL task runs at the same low priority as the other background observers
 * (GPS, IMU, recorder): it must never preempt the 50 Hz control task. The stack
 * covers linenoise + the dump's per-row CSV buffer and the HAL read buffer. */
#define USB_CONSOLE_TASK_PRIO 2
#define USB_CONSOLE_TASK_STACK 6144

/* Per-dump state threaded through blackbox_read_all: the most recent session
 * header seen, denormalised onto every following sample row. */
typedef struct {
    bool have_header;
    blackbox_session_header header;
} dump_state;

/* One raw slot from the ring: skip erased/corrupt slots, latch a header as the
 * active session context, and emit a denormalised CSV row for each sample. */
static void dump_slot(const uint8_t *record, size_t len, void *ctx)
{
    dump_state *state = (dump_state *)ctx;

    blackbox_record_type type;
    if (blackbox_record_classify(record, len, &type) != BLACKBOX_REC_OK) {
        return; /* erased or corrupt slot: not data */
    }

    if (type == BLACKBOX_TYPE_HEADER) {
        if (blackbox_record_decode_header(record, len, &state->header) ==
            BLACKBOX_REC_OK) {
            state->have_header = true;
        }
        return;
    }

    if (!state->have_header) {
        return; /* orphan sample (e.g. ring wrapped past its header): skip */
    }
    blackbox_sample sample;
    if (blackbox_record_decode_sample(record, len, &sample) != BLACKBOX_REC_OK) {
        return;
    }
    char row[BLACKBOX_CSV_LINE_MAX];
    if (blackbox_csv_row(&state->header, &sample, row, sizeof(row)) > 0U) {
        printf("%s\n", row);
    }
}

/* `spotlog dump`: print the CSV header, then stream every valid record as a
 * denormalised CSV row. Read-only; runs off the control path. */
static int cmd_spotlog(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "dump") != 0) {
        printf("usage: spotlog dump\n");
        return 1;
    }

    char header[BLACKBOX_CSV_LINE_MAX];
    if (blackbox_csv_header(header, sizeof(header)) > 0U) {
        printf("%s\n", header);
    }

    dump_state state = {0};
    blackbox_status status = blackbox_read_all(dump_slot, &state);
    if (status != BLACKBOX_OK) {
        printf("spotlog dump: read failed (status %d)\n", status);
        return 1;
    }
    return 0;
}

static esp_err_t register_spotlog(void)
{
    const esp_console_cmd_t cmd = {
        .command = "spotlog",
        .help = "Dump the spot-lock blackbox as denormalised CSV: spotlog dump",
        .hint = NULL,
        .func = &cmd_spotlog,
    };
    return esp_console_cmd_register(&cmd);
}

/* `params get`: print the active spot-lock regulator settings, one per line. */
static int params_get(void)
{
    settings_params params;
    control_loop_get_active_params(&params);

    char buf[PARAMS_CMD_GET_MAX];
    if (params_cmd_format_get(&params, buf, sizeof(buf)) == 0U) {
        printf("params get: format failed\n");
        return 1;
    }
    printf("%s", buf);
    return 0;
}

/* Report a rejected `params set` and return the console error code. */
static int params_set_reject(params_cmd_set_outcome outcome, const char *field,
                             const char *value)
{
    switch (outcome) {
    case PARAMS_CMD_SET_ERR_UNKNOWN_FIELD:
        printf("params set: unknown field '%s'\n", field);
        break;
    case PARAMS_CMD_SET_ERR_BAD_VALUE:
        printf("params set: bad value '%s' (expect a 0..65535 integer)\n", value);
        break;
    case PARAMS_CMD_SET_ERR_OUT_OF_RANGE:
        printf("params set: '%s' out of range for %s\n", value, field);
        break;
    default:
        printf("params set: invalid arguments\n");
        break;
    }
    return 1;
}

/* `params set <field> <value>`: validate via the SI-6 path and stage. The loop
 * applies the staged params only while DISARMED (single-writer); we report
 * whether the change is live now or waits for disarm. */
static int params_set(const char *field, const char *value)
{
    settings_params current;
    control_loop_get_active_params(&current);

    settings_params staged;
    params_cmd_set_outcome outcome =
        params_cmd_decide_set(&current, field, value, &staged);
    if (outcome != PARAMS_CMD_SET_ACCEPT) {
        return params_set_reject(outcome, field, value);
    }

    if (control_loop_post_pending(&staged) != ESP_OK) {
        printf("params set: staging failed\n");
        return 1;
    }

    control_loop_snapshot snap;
    control_loop_get_snapshot(&snap);
    const char *when = (params_cmd_apply_when(snap.state) == PARAMS_CMD_APPLIED)
                           ? "applied (DISARMED)"
                           : "staged (applies on disarm)";
    printf("params set: %s=%s %s\n", field, value, when);
    return 0;
}

static int cmd_params(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "get") == 0) {
        return params_get();
    }
    if (argc >= 4 && strcmp(argv[1], "set") == 0) {
        return params_set(argv[2], argv[3]);
    }
    printf("usage: params get | params set <field> <value>\n");
    printf("  fields: deadband_m max_throttle_pct throttle_gain servo_gain\n");
    return 1;
}

static esp_err_t register_params(void)
{
    const esp_console_cmd_t cmd = {
        .command = "params",
        .help = "Read/tune spot-lock settings: params get | params set <field> <value>",
        .hint = NULL,
        .func = &cmd_params,
    };
    return esp_console_cmd_register(&cmd);
}

esp_err_t usb_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "kayak>";
    repl_config.task_priority = USB_CONSOLE_TASK_PRIO;
    repl_config.task_stack_size = USB_CONSOLE_TASK_STACK;

    esp_console_dev_usb_serial_jtag_config_t dev_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err =
        esp_console_new_repl_usb_serial_jtag(&dev_config, &repl_config, &repl);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(esp_console_register_help_command());
    err = register_spotlog();
    if (err != ESP_OK) {
        return err;
    }
    err = register_params();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "USB console up (prio %d): spotlog dump, params get/set",
             USB_CONSOLE_TASK_PRIO);
    return esp_console_start_repl(repl);
}
