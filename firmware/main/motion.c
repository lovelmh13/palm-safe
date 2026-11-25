// 职责：在独立任务采集屏幕法线角速度，发布拨盘快照并处理重连。
#include "motion.h"
#include <stdatomic.h>
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** 临界区仅保护状态复制，绝不在其中访问总线。 */
static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
/** 界面读取的最新状态，零初始化对应等待连接。 */
static struct dial visible;
/** 触屏发出的新一轮请求。 */
static atomic_bool restart_requested;
/** QMI8658 标准寄存器；WHO_AM_I 应为 0x05。 */
enum { QMI_WHO = 0x00, QMI_CTRL1 = 0x02, QMI_CTRL2 = 0x03, QMI_CTRL3 = 0x04, QMI_CTRL5 = 0x06,
       QMI_CTRL7 = 0x08, QMI_STATUS = 0x2e, QMI_GYRO_Z = 0x3f, QMI_RESET = 0x60 };

/** @brief 向六轴芯片写入单个寄存器。 */
static esp_err_t qmi_write(i2c_master_dev_handle_t device, uint8_t address, uint8_t value)
{
    uint8_t bytes[] = {address, value}; /**< 地址与寄存器值。 */
    return i2c_master_transmit(device, bytes, sizeof(bytes), 100);
}
/** @brief 连续读取芯片寄存器，使用总线驱动自带的事务互斥。 */
static esp_err_t qmi_read(i2c_master_dev_handle_t device, uint8_t address, uint8_t *bytes, size_t count)
{
    return i2c_master_transmit_receive(device, &address, 1, bytes, count, 100);
}
/** @brief 检查两种标准地址，启用小端连续读取、±4g 和 ±512 度/秒。 */
static bool qmi_open(i2c_master_dev_handle_t *device)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle(); /**< 共享现有触摸总线。 */
    if (bus == NULL) { return false; }
    const uint8_t addresses[] = {0x6b, 0x6a}; /**< 引脚配置决定的两个合法地址。 */
    for (unsigned int index = 0; index < 2; index++) {
        i2c_device_config_t config = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addresses[index], .scl_speed_hz = 400000};
        if (i2c_master_bus_add_device(bus, &config, device) != ESP_OK) { continue; }
        uint8_t identity = 0; /**< 防止对错误器件写配置。 */
        if (qmi_read(*device, QMI_WHO, &identity, 1) == ESP_OK && identity == 0x05) {
            if (qmi_write(*device, QMI_RESET, 0xb0) != ESP_OK) { break; }
            vTaskDelay(pdMS_TO_TICKS(30));
            if (qmi_write(*device, QMI_CTRL1, 0x40) != ESP_OK || qmi_write(*device, QMI_CTRL7, 0) != ESP_OK ||
                qmi_write(*device, QMI_CTRL2, 0x16) != ESP_OK || qmi_write(*device, QMI_CTRL3, 0x56) != ESP_OK ||
                qmi_write(*device, QMI_CTRL5, 0) != ESP_OK || qmi_write(*device, QMI_CTRL7, 3) != ESP_OK) { break; }
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI("掌心保险箱", "QMI8658 六轴采集已启动，地址 0x%02x", addresses[index]);
            return true;
        }
        i2c_master_bus_rm_device(*device);
        *device = NULL;
    }
    if (*device != NULL) { i2c_master_bus_rm_device(*device); *device = NULL; }
    return false;
}
/** @brief 解码连续小端 16 位有符号采样。 */
static int16_t signed_word(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}


/** @brief 复制状态给屏幕线程。 */
static void publish(const struct dial *dial)
{
    portENTER_CRITICAL(&state_lock);
    visible = *dial;
    portEXIT_CRITICAL(&state_lock);
}

/** @brief 获取本帧可显示的完整状态。 */
struct dial motion_snapshot(void)
{
    struct dial snapshot; /**< 锁内复制，锁外绘制。 */
    portENTER_CRITICAL(&state_lock);
    snapshot = visible;
    portEXIT_CRITICAL(&state_lock);
    return snapshot;
}

/** @brief 发送重玩请求，统一由采集线程重置状态。 */
void motion_restart(void)
{
    atomic_store(&restart_requested, true);
}

/** @brief 固定间隔轮询新数据；掉线时停止判定，重连后重新调零。 */
static void read_motion(void *argument)
{
    (void)argument;
    struct dial dial = {0}; /**< 只有采集任务可修改的玩法状态。 */
    i2c_master_dev_handle_t device = NULL; /**< 当前六轴句柄。 */
    int64_t last = 0; /**< 上次有效采样的微秒时间戳。 */
    while (true) {
        if (device == NULL) {
            if (!qmi_open(&device)) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            dial_start(&dial);
            last = esp_timer_get_time();
        }
        if (atomic_exchange(&restart_requested, false)) { dial_start(&dial); }
        uint8_t status = 0, bytes[2]; /**< 数据就绪位及小端角速度。 */
        esp_err_t result = qmi_read(device, QMI_STATUS, &status, 1);
        if (result == ESP_OK && (status & 2)) {
            result = qmi_read(device, QMI_GYRO_Z, bytes, sizeof(bytes));
            if (result == ESP_OK) {
                int64_t now = esp_timer_get_time(); /**< 本次成功读数的时间。 */
                dial_sample(&dial, signed_word(bytes) / 64.0f, (now - last) / 1e6f);
                last = now;
                publish(&dial);
            }
        }
        if (result != ESP_OK || esp_timer_get_time() - last > 500000) {
            dial.phase = OFFLINE;
            publish(&dial);
            i2c_master_bus_rm_device(device);
            device = NULL;
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/** @brief 在显示初始化后启动共享总线上的传感器。 */
bool motion_start(void)
{
    return bsp_i2c_init() == ESP_OK &&
           xTaskCreate(read_motion, "safe_motion", 4096, NULL, 4, NULL) == pdPASS;
}
