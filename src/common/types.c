/* SPDX-License-Identifier: MIT */
#include "oru/types.h"

const char *oru_state_str(oru_state_t s)
{
    switch (s) {
    case ORU_STATE_INIT:        return "INIT";
    case ORU_STATE_SYNC:        return "SYNC";
    case ORU_STATE_CONFIG:      return "CONFIG";
    case ORU_STATE_OPERATIONAL: return "OPERATIONAL";
    case ORU_STATE_FAULT:       return "FAULT";
    default:                    return "UNKNOWN";
    }
}

const char *oru_status_str(oru_status_t s)
{
    switch (s) {
    case ORU_OK:           return "OK";
    case ORU_ERR:          return "ERR";
    case ORU_ERR_PARAM:    return "ERR_PARAM";
    case ORU_ERR_HW:       return "ERR_HW";
    case ORU_ERR_TIMEOUT:  return "ERR_TIMEOUT";
    case ORU_ERR_PROTO:    return "ERR_PROTO";
    case ORU_ERR_NOTSUP:   return "ERR_NOTSUP";
    default:               return "ERR_?";
    }
}
