extern "C" void unity_run_menu(void);
extern "C" void unity_run_all_tests(void);

#include <cstdint>

#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr uint32_t kUnityTaskStackBytes = 24U * 1024U;
constexpr UBaseType_t kUnityTaskPriority = 5U;

void UnityTask(void*) {
    (void)esp_task_wdt_deinit();
    unity_run_all_tests();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // namespace

extern "C" void app_main(void) {
    const BaseType_t created = xTaskCreatePinnedToCore(&UnityTask, "unity",
                                                       kUnityTaskStackBytes, nullptr,
                                                       kUnityTaskPriority, nullptr,
                                                       tskNO_AFFINITY);
    configASSERT(created == pdPASS);
}
