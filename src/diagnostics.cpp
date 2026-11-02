// diagnostics.cpp — heap/stack report over serial (see diagnostics.h)
#include "diagnostics.h"

#include <Arduino.h>
#include "esp_core_dump.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio/bluetooth.h"

struct HeapMark {
    const char *stage;
    uint32_t    freeBytes;
    uint32_t    largestBlock;
};
static constexpr int kMaxMarks = 12;
static HeapMark marks[kMaxMarks];
static int      markCount = 0;

static esp_reset_reason_t resetReason   = ESP_RST_UNKNOWN;
static size_t             coredumpBytes = 0;

// Tasks to report, by the name FreeRTOS stores (15 chars max). This ESP-IDF
// build can't list all tasks; names missing from a build are skipped.
static const char *const kTaskNames[] = {
    "loopTask", "audioTask", "inputTask", "ScopeDisplayU8G", "BtAppT",
    "BTC_TASK", "BTU_TASK", "hciT", "btController",
    "esp_timer", "Tmr Svc", "ipc0", "ipc1",
};
static constexpr int kTaskSlots = sizeof(kTaskNames) / sizeof(kTaskNames[0]) + 2; // + idle tasks

static const char *resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON:   return "power-on / reset button";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default:                return "unknown";
    }
}

void initDiagnostics() {
    resetReason = esp_reset_reason();
    // Reads flash, so only here: later it would stall the audio task's core.
    size_t addr = 0, size = 0;
    if (esp_core_dump_image_get(&addr, &size) == ESP_OK) coredumpBytes = size;

    Serial.printf("[DIAG] Last reset: %s. Send 'd' for a heap/stack report.\n",
                  resetReasonName(resetReason));
    if (coredumpBytes)
        Serial.printf("[DIAG] Coredump of an earlier crash in flash (%u B)\n",
                      (unsigned)coredumpBytes);
}

void diagMark(const char *stage) {
    if (markCount >= kMaxMarks) return;
    marks[markCount++] = {stage, ESP.getFreeHeap(), ESP.getMaxAllocHeap()};
}

struct TaskStack {
    const char  *name;
    TaskHandle_t handle;
    UBaseType_t  freeBytes;
};

static void addTask(TaskStack *tasks, int &count, const char *name, TaskHandle_t handle) {
    if (!handle) return;
    tasks[count++] = {name, handle, uxTaskGetStackHighWaterMark(handle)};
}

static void printDiagnostics() {
    // Snapshot first: printing uses heap and loopTask stack itself.
    uint32_t freeNow = ESP.getFreeHeap();
    uint32_t freeMin = ESP.getMinFreeHeap();
    uint32_t largest = ESP.getMaxAllocHeap();
    TaskStack tasks[kTaskSlots];
    int taskCount = 0;
    for (const char *name : kTaskNames) addTask(tasks, taskCount, name, xTaskGetHandle(name));
    addTask(tasks, taskCount, "IDLE0", xTaskGetIdleTaskHandleForCPU(0));
    addTask(tasks, taskCount, "IDLE1", xTaskGetIdleTaskHandleForCPU(1));

    Serial.printf("\n[DIAG] Uptime %lu s, last reset: %s\n",
                  (unsigned long)(millis() / 1000), resetReasonName(resetReason));
    if (coredumpBytes)
        Serial.printf("[DIAG] Coredump of an earlier crash in flash (%u B)\n",
                      (unsigned)coredumpBytes);

    Serial.println(F("[DIAG] Heap after each boot stage (free / largest block):"));
    for (int i = 0; i < markCount; ++i)
        Serial.printf("  %-10s %6u / %6u B\n",
                      marks[i].stage, marks[i].freeBytes, marks[i].largestBlock);
    Serial.printf("[DIAG] Heap now %u B free, lowest %u B, largest block %u B\n",
                  freeNow, freeMin, largest);

    Serial.println(F("[DIAG] Unused stack, lowest since boot (task, core, prio):"));
    for (int i = 0; i < taskCount; ++i) {
        BaseType_t affinity = xTaskGetAffinity(tasks[i].handle);
        Serial.printf("  %-16s %2d %2u %6u B\n", tasks[i].name,
                      affinity == tskNO_AFFINITY ? -1 : (int)affinity,
                      (unsigned)uxTaskPriorityGet(tasks[i].handle),
                      (unsigned)tasks[i].freeBytes);
    }

#ifdef BLUETOOTH_MODE
    printBtStatus();
#endif
}

void pollDiagnostics() {
    while (Serial.available() > 0) {
        if (Serial.read() == 'd') printDiagnostics();
    }
}
