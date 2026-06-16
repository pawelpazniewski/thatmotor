#include "params_api.h"

#include "api_contract.h"
#include "control_loop.h"
#include "params_decide.h"
#include "params_json.h"
#include "settings_validate.h"
#include "state_machine.h"

/* Buffer for a serialised params object embedded in a success envelope. The
 * full envelope buffer (body) is larger; this holds just the data fragment. */
#define PARAMS_JSON_MAX 768

static params_api_response make_error(api_error_code code, const char *message,
                                      int http_status, char *body,
                                      size_t body_size)
{
    params_api_response resp = {.http_status = http_status};
    resp.body_len = api_build_error(code, message, body, body_size);
    return resp;
}

params_api_response params_api_handle_get(char *body, size_t body_size)
{
    settings_params params;
    control_loop_get_active_params(&params);

    char data[PARAMS_JSON_MAX];
    if (params_json_serialize(&params, data, sizeof(data)) == 0U) {
        return make_error(API_ERR_INTERNAL, NULL, 500, body, body_size);
    }
    params_api_response resp = {.http_status = 200};
    resp.body_len = api_build_success(data, body, body_size);
    return resp;
}

/* Current controller state from the lossy snapshot. The authoritative re-check
 * happens in the loop at apply time (TOCTOU); this is the boundary gate input. */
static sm_state controller_state(void)
{
    control_loop_snapshot snap;
    control_loop_get_snapshot(&snap);
    return snap.state;
}

/* Re-validate a parsed candidate field-by-field at the API boundary. The
 * candidate is rejected if any field was out of range or a cross-field
 * invariant failed (settings_validate reports it as not fully valid). */
static bool candidate_is_valid(const settings_params *candidate,
                               settings_params *out)
{
    settings_validation_result vr = settings_validate(candidate, true, out);
    return vr.settings_valid;
}

/* Stage validated params and render the 200 success envelope (or 500 on a
 * serialise/mailbox failure). Reached only after params_decide_write accepts. */
static params_api_response stage_and_render(const settings_params *validated,
                                            char *body, size_t body_size)
{
    if (control_loop_post_pending(validated) != ESP_OK) {
        return make_error(API_ERR_INTERNAL, NULL, 500, body, body_size);
    }
    char data[PARAMS_JSON_MAX];
    if (params_json_serialize(validated, data, sizeof(data)) == 0U) {
        return make_error(API_ERR_INTERNAL, NULL, 500, body, body_size);
    }
    params_api_response resp = {.http_status = 200};
    resp.body_len = api_build_success(data, body, body_size);
    return resp;
}

params_api_response params_api_handle_post(const char *request_json, char *body,
                                           size_t body_size)
{
    /* Seed from the current active params so a partial POST updates a subset. */
    settings_params candidate;
    control_loop_get_active_params(&candidate);

    if (!params_json_parse(request_json, &candidate)) {
        return make_error(API_ERR_BAD_REQUEST, NULL, 400, body, body_size);
    }

    /* Re-validate every field server-side, then delegate the accept/reject gate
     * (DISARMED + valid) to the pure decision core. */
    settings_params validated;
    bool fields_valid = candidate_is_valid(&candidate, &validated);
    params_write_outcome outcome =
        params_decide_write(controller_state(), fields_valid);

    if (outcome.decision != PARAMS_WRITE_ACCEPT) {
        return make_error(outcome.code, NULL, outcome.http_status, body, body_size);
    }
    return stage_and_render(&validated, body, body_size);
}
