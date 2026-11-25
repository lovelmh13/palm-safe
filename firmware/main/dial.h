// 职责：声明保险箱的拨盘状态与按时间推进的玩法接口。
#pragma once
#include <stdbool.h>

/** 设备连接、调零、方向识别、拨号和开锁阶段。 */
enum dial_phase { OFFLINE, ZEROING, ORIENTING, DIALING, UNLOCKED };
/** 采集线程持有的完整状态，界面只读取副本。 */
struct dial {
    enum dial_phase phase; /**< 当前操作阶段。 */
    int step;              /**< 已打开的锁销数。 */
    float number;          /**< 表盘刻度，范围为零至一百，不含一百。 */
    float bias;            /**< 静止角速度的平均值，单位为度每秒。 */
    float elapsed;         /**< 当前校准或停稳累计秒数。 */
    float hold_number;     /**< 本次停稳计时开始时的刻度，用于区分微抖和持续拨动。 */
    float orientation;     /**< 首次顺转测得的符号，识别前累计角度。 */
    float approach;        /**< 沿正确方向最后连续接近的刻度数。 */
};
/** @brief 开始新一轮，重新测量陀螺仪静止偏差。 */
void dial_start(struct dial *dial);
/** @brief 消费一次屏幕法线角速度及真实采样间隔。 */
void dial_sample(struct dial *dial, float rate, float seconds);
/** @brief 读取当前目标数字，开锁后返回最后一位。 */
int dial_target(const struct dial *dial);
/** @brief 返回当前关要求的转动方向。 */
bool dial_is_clockwise(const struct dial *dial);
