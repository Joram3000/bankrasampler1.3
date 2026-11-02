// Bankra transfer app — a separate firmware in the "transfer" app partition
// (see partitions.csv) that puts the SD card behind a web page over WiFi:
// connect to the BANKRAKAKA-xxxxxx network and open http://bankra.local
//
// It is kept out of the sampler firmware because linking the WiFi stack costs
// ~20 KB of static RAM even when WiFi is never started, which the BT stack
// can't spare. The sampler's "WiFi upload" menu item boots this app once
// (src/transfer_mode.cpp); it points the boot partition straight back at the
// sampler, so any reset — the settings switch, "Klaar" on the page, a crash
// or a power cycle — returns to normal operation.
//
// Flash once, and again only when this app changes:
//   pio run -e transfer -t upload
#include <Arduino.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstdarg>
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "soc/rtc_cntl_reg.h"

#include "config/pins.h"
#include "config/screen.h"
#include "storage/sample_map.h" // SAMPLE_MAP_MAX / SAMPLE_NAME_MAX
#include "transfer_page.h"

namespace {

// WPA2 needs at least 8 characters. It is shown on the display, so it is no
// secret — it only keeps passers-by out while the transfer app is running.
constexpr const char* kApPassword = "bankra";
// mDNS name: the page is at http://bankra.local
constexpr const char* kHostName = "bankra";
// Uploads are written here and renamed once complete, so an interrupted
// upload never leaves a truncated sample behind (not a .wav, so never listed).
constexpr const char* kUploadTempPath = "/upload.tmp";
// Larger than the 4 KB stdio default: fewer, longer SD card writes.
constexpr size_t   kUploadBufferBytes = 16 * 1024;
constexpr uint32_t kSwitchDebounceMs  = 50;
constexpr uint32_t kRebootDelayMs     = 500; // lets the HTTP response go out first
constexpr uint32_t kUploadDrawMs      = 500; // display refresh during an upload

DISPLAY_U8G2_CLASS display(DISPLAY_U8G2_CTOR_ARGS);
WebServer server(80);

char     apName[24];
char     statusLine[32] = "Wacht op verbinding";
bool     statusDirty = true;
uint32_t lastDrawMs  = 0;
uint32_t rebootAt    = 0; // non-zero = reboot scheduled
bool     sdOk        = false;
bool     apUp        = false;
int      switchAtBoot  = 0;
uint32_t switchMovedAt = 0;
int      lastStations  = -1;

File   uploadFile;
String uploadName;
String uploadError; // non-empty = upload rejected; remaining chunks are skipped
bool   uploadSaved = false;

void setStatus(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(statusLine, sizeof(statusLine), fmt, args);
  va_end(args);
  statusDirty = true;
}

void drawScreen() {
  char line[40];
  display.clearBuffer();
  display.setFont(u8g2_font_5x7_tr);
  display.drawBox(0, 0, DISPLAY_WIDTH, 9);
  display.setDrawColor(0);
  display.drawStr(2, 7, "WiFi upload");
  display.setDrawColor(1);
  snprintf(line, sizeof(line), "Net: %s", apName);
  display.drawStr(0, 18, line);
  snprintf(line, sizeof(line), "Pw:  %s", kApPassword);
  display.drawStr(0, 27, line);
  snprintf(line, sizeof(line), "http://%s.local", kHostName);
  display.drawStr(0, 36, line);
  snprintf(line, sizeof(line), "of http://%s", WiFi.softAPIP().toString().c_str());
  display.drawStr(0, 45, line);
  display.drawStr(0, 54, statusLine);
  display.drawStr(0, 63, "Schakelaar om = klaar");
  display.sendBuffer();

  statusDirty = false;
  lastDrawMs  = millis();
}

// Any reset from here on — even a crash — boots the sampler again.
void bootSamplerNext() {
  const esp_partition_t* sampler = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (!sampler || esp_ota_set_boot_partition(sampler) != ESP_OK)
    Serial.println(F("[XFER] Could not select the sampler for the next boot"));
}

bool initSd() {
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS_PIN);
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (SD.begin(SD_CS_PIN, SPI, 25000000UL)) return true;
    delay(100);
  }
  return false;
}

// A plain file name in the SD root: no path, nothing that walks out of it.
bool isRootFileName(const String& name) {
  return name.length() > 0 && name != "." && name != ".." &&
         name.indexOf('/') < 0 && name.indexOf('\\') < 0;
}

// Uploads are stricter: printable ASCII only (the display font has nothing
// else), nothing FAT rejects, not hidden, and short enough for the sample
// list. The page cleans names up before sending; this is the backstop.
bool isValidUploadName(const String& name) {
  if (!isRootFileName(name) || name[0] == '.') return false;
  if (name.length() >= static_cast<unsigned>(SAMPLE_NAME_MAX)) return false;
  if (name.equalsIgnoreCase(kUploadTempPath + 1)) return false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    if (c < 0x20 || c > 0x7E || strchr(":*?\"<>|", c)) return false;
  }
  return true;
}

void appendJsonString(String& out, const char* s) {
  out += '"';
  for (; *s; ++s) {
    const char c = *s;
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", c);
      out += esc;
    } else {
      out += c;
    }
  }
  out += '"';
}

// GET /list — every file in the SD root plus card space, as JSON.
void handleList() {
  String json;
  json.reserve(2048);
  json += "{\"files\":[";
  bool first = true;
  File root = SD.open("/");
  if (root) {
    while (File entry = root.openNextFile()) {
      if (!entry.isDirectory()) {
        if (!first) json += ',';
        first = false;
        json += "{\"name\":";
        appendJsonString(json, entry.name());
        json += ",\"size\":";
        json += static_cast<uint32_t>(entry.size());
        json += '}';
      }
      entry.close();
    }
    root.close();
  }
  const uint64_t total = SD.totalBytes();
  char tail[112];
  snprintf(tail, sizeof(tail),
           "],\"totalKb\":%lu,\"freeKb\":%lu,\"maxSamples\":%d,\"maxName\":%d}",
           static_cast<unsigned long>(total / 1024),
           static_cast<unsigned long>((total - SD.usedBytes()) / 1024),
           SAMPLE_MAP_MAX, SAMPLE_NAME_MAX - 1);
  json += tail;
  server.send(200, "application/json", json);
}

// POST /upload (multipart) — called once per chunk while the body arrives.
// handleClient() blocks for the whole upload, so this also keeps the display
// up to date.
void handleUploadChunk() {
  HTTPUpload& up = server.upload();
  switch (up.status) {
  case UPLOAD_FILE_START:
    uploadName  = up.filename;
    uploadError = "";
    uploadSaved = false;
    if (!isValidUploadName(uploadName)) {
      uploadError = "Ongeldige bestandsnaam";
      break;
    }
    if (SD.exists(kUploadTempPath)) SD.remove(kUploadTempPath);
    uploadFile = SD.open(kUploadTempPath, FILE_WRITE);
    if (!uploadFile) {
      uploadError = "Kan niet schrijven naar de SD-kaart";
      break;
    }
    uploadFile.setBufferSize(kUploadBufferBytes);
    setStatus("> %s", uploadName.c_str());
    drawScreen();
    break;

  case UPLOAD_FILE_WRITE:
    if (!uploadFile) break;
    if (uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      uploadFile.close();
      SD.remove(kUploadTempPath);
      uploadError = "SD-kaart vol of schrijffout";
    } else if (millis() - lastDrawMs >= kUploadDrawMs) {
      setStatus("> %luK %s", static_cast<unsigned long>(up.totalSize / 1024), uploadName.c_str());
      drawScreen();
    }
    break;

  case UPLOAD_FILE_END: {
    if (!uploadFile) break;
    uploadFile.close();
    const String target = "/" + uploadName;
    if (SD.exists(target)) SD.remove(target);
    if (SD.rename(kUploadTempPath, target)) {
      uploadSaved = true;
      setStatus("Opgeslagen: %s", uploadName.c_str());
      Serial.printf("[XFER] Saved %s (%u bytes)\n", target.c_str(), (unsigned)up.totalSize);
    } else {
      SD.remove(kUploadTempPath);
      uploadError = "Opslaan mislukt";
    }
    break;
  }

  case UPLOAD_FILE_ABORTED:
    if (uploadFile) uploadFile.close();
    if (SD.exists(kUploadTempPath)) SD.remove(kUploadTempPath);
    uploadError = "Upload afgebroken";
    break;
  }
  if (uploadError.length() > 0) setStatus("Fout: %s", uploadName.c_str());
}

// POST /upload — runs after the whole body has been through handleUploadChunk.
void handleUploadDone() {
  if (uploadSaved) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain",
                uploadError.length() > 0 ? uploadError : String("Geen bestand ontvangen"));
  }
  uploadSaved = false;
  uploadError = "";
}

// POST /delete?name=...
void handleDelete() {
  const String name = server.arg("name");
  if (!isRootFileName(name) || !SD.remove("/" + name)) {
    server.send(404, "text/plain", "Niet gevonden");
    return;
  }
  setStatus("Verwijderd: %s", name.c_str());
  server.send(200, "text/plain", "OK");
}

// GET /download?name=...
void handleDownload() {
  const String name = server.arg("name");
  File f = isRootFileName(name) ? SD.open("/" + name, FILE_READ) : File();
  if (!f || f.isDirectory()) {
    server.send(404, "text/plain", "Niet gevonden");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

} // namespace

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // same supply as the sampler: WiFi start current peak
  Serial.begin(115200);
  Serial.println(F("[XFER] Transfer app"));
  bootSamplerNext();

  display.setBusClock(DISPLAY_I2C_CLOCK_HZ);
  display.begin();

  // Same suffix as the Bluetooth name, so both are recognisable as this unit.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(apName, sizeof(apName), "BANKRAKAKA-%02X%02X%02X", mac[3], mac[4], mac[5]);

  sdOk = initSd();

  WiFi.persistent(false); // keep the AP config out of NVS flash
  WiFi.mode(WIFI_AP);
  apUp = WiFi.softAP(apName, kApPassword);
  if (apUp) {
    // The computer is next to the device, so full TX power is not needed —
    // and its current spikes are what browned out the board during BT init.
    WiFi.setTxPower(WIFI_POWER_11dBm);
    MDNS.begin(kHostName);
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", kTransferPage); });
  server.on("/list", HTTP_GET, handleList);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/reboot", HTTP_POST, [] {
    server.send(200, "text/plain", "OK");
    rebootAt = millis() + kRebootDelayMs;
  });
  server.onNotFound([] { server.send(404, "text/plain", "Niet gevonden"); });
  server.begin();

  if (!sdOk)      setStatus("Fout: geen SD-kaart");
  else if (!apUp) setStatus("Fout: WiFi start mislukt");

  // Flipping the settings switch either way ends the transfer app.
  pinMode(SWITCH_PIN_SETTINGS_MODE, INPUT);
  switchAtBoot = digitalRead(SWITCH_PIN_SETTINGS_MODE);

  Serial.printf("[XFER] AP '%s' at %s (http://%s.local), SD %s, heap=%u\n",
                apName, WiFi.softAPIP().toString().c_str(), kHostName,
                sdOk ? "ok" : "FAILED", ESP.getFreeHeap());
  drawScreen();
}

void loop() {
  server.handleClient();
  const uint32_t now = millis();

  // Connection status, unless setup() reported an error there instead.
  const int stations = WiFi.softAPgetStationNum();
  if (sdOk && apUp && stations != lastStations) {
    lastStations = stations;
    setStatus(stations > 0 ? "Verbonden" : "Wacht op verbinding");
  }

  if (digitalRead(SWITCH_PIN_SETTINGS_MODE) != switchAtBoot) {
    if (!switchMovedAt) switchMovedAt = now;
    else if (now - switchMovedAt >= kSwitchDebounceMs) rebootAt = now;
  } else {
    switchMovedAt = 0;
  }

  if (rebootAt && now >= rebootAt) {
    setStatus("Herstarten...");
    drawScreen();
    Serial.println(F("[XFER] Done — rebooting into the sampler"));
    ESP.restart();
  }

  if (statusDirty) drawScreen();
  delay(2);
}
