// 职责：启动微雪圆屏、陀螺仪采样与保险箱界面。
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "motion.h"
#include "ui.h"

/** @brief 固件入口。 */
void app_main(void)
{
    if (bsp_display_start() == NULL) {
        ESP_LOGE("掌心保险箱", "圆屏启动失败");
        return;
    }
    if (!motion_start()) { ESP_LOGE("掌心保险箱", "采集任务启动失败"); }
    bsp_display_lock(-1);
    ui_start();
    bsp_display_brightness_set(70);
    bsp_display_unlock();
}
