#include "usb_console.h"

#include <stdio.h>
#include <string.h>

#include "blackbox.h"
#include "blackbox_csv.h"
#include "blackbox_record.h"
#include "esp_console.h"
#include "esp_log.h"

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

    ESP_LOGI(TAG, "USB console up (prio %d): spotlog dump", USB_CONSOLE_TASK_PRIO);
    return esp_console_start_repl(repl);
}
