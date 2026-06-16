#include "params_decide.h"

params_write_outcome params_decide_write(sm_state state, bool fields_valid)
{
    if (state != SM_STATE_DISARMED) {
        return (params_write_outcome){
            .decision = PARAMS_WRITE_REJECT_NOT_DISARMED,
            .code = API_ERR_NOT_DISARMED,
            .http_status = 409,
        };
    }
    if (!fields_valid) {
        return (params_write_outcome){
            .decision = PARAMS_WRITE_REJECT_INVALID,
            .code = API_ERR_VALIDATION_FAILED,
            .http_status = 400,
        };
    }
    return (params_write_outcome){
        .decision = PARAMS_WRITE_ACCEPT,
        .code = API_OK,
        .http_status = 200,
    };
}
