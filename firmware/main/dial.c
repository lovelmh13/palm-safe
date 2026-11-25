// 职责：把转动量换算成刻度，判断拨号方向、停稳和三道锁销。
#include "dial.h"
#include <math.h>

/** @brief 重置拨盘与本轮校准数据。 */
void dial_start(struct dial *dial)
{
    *dial = (struct dial){.phase = ZEROING};
}

/** @brief 返回预设密码的相应数字。 */
int dial_target(const struct dial *dial)
{
    static const int code[] = {20, 65, 10}; /**< 三道锁销的固定密码。 */
    return code[dial->step < 3 ? dial->step : 2];
}

/** @brief 第一、三位顺转，第二位逆转。 */
bool dial_is_clockwise(const struct dial *dial)
{
    return dial->step != 1;
}

/** @brief 根据一帧新采样更新拨盘，采样中断不能补算位移或停留时间。 */
void dial_sample(struct dial *dial, float rate, float seconds)
{
    if (!isfinite(rate) || !isfinite(seconds) || seconds <= 0 || seconds > 0.15f) {
        if (dial->phase == ZEROING) { dial->bias = 0; }
        dial->elapsed = 0;
        return;
    }
    if (dial->phase == ZEROING) {
        if (fabsf(rate) > 3.0f) {
            dial->elapsed = 0;
            dial->bias = 0;
            return;
        }
        dial->elapsed += seconds;
        dial->bias += (rate - dial->bias) * seconds / dial->elapsed;
        if (dial->elapsed >= 0.65f) {
            dial->phase = ORIENTING;
            dial->elapsed = 0;
        }
        return;
    }
    if (dial->phase != ORIENTING && dial->phase != DIALING) { return; }
    float speed = rate - dial->bias; /**< 去除芯片偏差后的角速度。 */
    bool is_still = fabsf(speed) < 1.4f; /**< 静止死区过滤手抖与噪声。 */
    if (dial->phase == ORIENTING) {
        if (!is_still) { dial->orientation += speed * seconds; }
        if (fabsf(dial->orientation) >= 5) {
            dial->number = fabsf(dial->orientation) / 1.2f;
            dial->approach = dial->number;
            dial->orientation = copysignf(1, dial->orientation);
            dial->phase = DIALING;
        }
        return;
    }
    if (!is_still) {
        float change = speed * seconds * dial->orientation / 1.2f; /**< 手腕每转 1.2 度走一格。 */
        dial->number = fmodf(dial->number + change + 100, 100);
        float travel = dial_is_clockwise(dial) ? change : -change; /**< 当前步骤要求方向上的位移。 */
        dial->approach = fminf(3, fmaxf(0, dial->approach + travel));
    }
    if (fabsf(dial->number - dial_target(dial)) > 1.05f || dial->approach < 1.5f) {
        dial->elapsed = 0;
        return;
    }
    // 依据指针实际走过的距离判断停稳，手腕短暂抖动仍可累计时间。
    if (dial->elapsed == 0 || fabsf(dial->number - dial->hold_number) > 0.65f) {
        dial->hold_number = dial->number;
        dial->elapsed = 0;
    }
    dial->elapsed += seconds;
    if (dial->elapsed >= 0.55f) {
        dial->number = dial_target(dial);
        dial->step++;
        dial->elapsed = 0;
        dial->approach = 0;
        if (dial->step == 3) { dial->phase = UNLOCKED; }
    }
}
