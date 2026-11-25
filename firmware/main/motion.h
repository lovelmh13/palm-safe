// 职责：提供采集线程与圆屏界面之间的状态和重玩请求。
#pragma once
#include "dial.h"
/** @brief 启动六轴采样任务。 */
bool motion_start(void);
/** @brief 复制线程安全的拨盘快照。 */
struct dial motion_snapshot(void);
/** @brief 请求开始新一轮。 */
void motion_restart(void);
