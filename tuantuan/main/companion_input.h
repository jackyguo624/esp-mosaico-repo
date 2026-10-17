// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_err.h"
#include "esp_gsp.h"
// Register the standard USB pointer service during startup. The GSP handle
// must remain alive until Iris stops; Tuantuan owns it for the boot lifetime.
esp_err_t companion_input_register(esp_gsp_handle_t ui);
