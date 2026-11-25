// 职责：复现数字显示为 20 时因手抖无法进位，并检查连续转动和错向停留。
#include "dial.h"
#include <math.h>
#include <stdio.h>

/** @brief 以十毫秒间隔模拟给定角速度。 */
static void turn(struct dial *dial, int samples, float speed)
{
    for (int i = 0; i < samples; i++) { dial_sample(dial, speed, 0.01f); }
}

/** @brief 从静止校准开始，通过正式输入路径验证三个停留场景。 */
int main(void)
{
    struct dial dial; /**< 模拟用户手中的拨盘。 */
    dial_start(&dial);
    turn(&dial, 75, 0);
    turn(&dial, 20, 120);
    for (int i = 0; i < 70 && dial.step == 0; i++) {
        // 每五帧换向，指针仅来回移动四分之一格。
        dial_sample(&dial, i % 10 < 5 ? 6 : -6, 0.01f);
        if (lroundf(dial.number) != 20) { return 1; }
    }
    if (dial.step != 1) { puts("失败：中央一直显示 20，停留七百毫秒仍未进位"); return 1; }
    turn(&dial, 45, 120);
    turn(&dial, 80, 0);
    if (dial.step != 1) { puts("失败：反向到达 65 也进位"); return 1; }
    turn(&dial, 97, -120);
    turn(&dial, 120, -6);
    if (dial.step != 1) { puts("失败：连续转动经过 65 也进位"); return 1; }
    puts("通过：轻微手抖可确认，错向停留、持续转过目标均不进位");
    return 0;
}
