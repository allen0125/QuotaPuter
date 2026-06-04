// provisioner.h — USB-Serial-JTAG credential provisioning protocol (PRD §7.3).
//
// A background task reads newline-delimited JSON commands from the Cardputer's
// native USB port and writes JSON replies prefixed with "#QP " so the PC tool
// (tools/quota_config.py) can pick them out from ordinary log output. Secrets
// flow device-inward only and are never echoed back.
//
// Protocol (one JSON object per line):
//   {"cmd":"hello"}
//   {"cmd":"list"}
//   {"cmd":"set_wifi","ssid":"...","password":"..."}
//   {"cmd":"add_provider","id":"minimax_cn","mode":"direct","secret":"...","enabled":true}
//   {"cmd":"add_provider","id":"openai","mode":"relay","relay_url":"...","relay_token":"...","enabled":true}
//   {"cmd":"remove_provider","id":"..."}
//   {"cmd":"factory_reset"}
// Replies: #QP {"ok":true,...} or #QP {"ok":false,"error":"..."}
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the provisioning task. Call once after secret_store/providers are ready.
esp_err_t provisioner_start(void);

#ifdef __cplusplus
}
#endif
