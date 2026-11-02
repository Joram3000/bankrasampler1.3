// transfer_mode.cpp — switching to the WiFi transfer app (see transfer_mode.h)
#include "transfer_mode.h"

#include <Arduino.h>
#include "esp_ota_ops.h"

namespace {

const esp_partition_t* transferPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "transfer");
}

} // namespace

bool transferAppInstalled() {
  const esp_partition_t* p = transferPartition();
  esp_app_desc_t desc;
  return p && esp_ota_get_partition_description(p, &desc) == ESP_OK;
}

bool rebootIntoTransferApp() {
  const esp_partition_t* p = transferPartition();
  if (!p || esp_ota_set_boot_partition(p) != ESP_OK) {
    Serial.println(F("[XFER] Transfer app missing or invalid — not rebooting"));
    return false;
  }
  Serial.println(F("[XFER] Rebooting into the transfer app..."));
  delay(200);
  ESP.restart();
  return true;
}
