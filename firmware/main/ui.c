// 职责：绘制黄铜密码盘、三道锁销和开锁动作，开锁后点击可重玩。
#include "ui.h"
#include "motion.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>

LV_FONT_DECLARE(safe_font);
/** 表盘底色、黄铜刻度、文字、灰绿次要刻度与开锁颜色。 */
enum { FACE = 0x111d20, BRASS = 0xe6ba76, IVORY = 0xf8f0d9, STEEL = 0x71847f, OPEN = 0x91dcc0 };
/** 上次显示的快照及开锁动画的起始时间与进度。 */
static struct dial view;
static uint32_t opened_at;
static float opening;
/** 整屏画布、当前刻度、操作提示、停稳提示及十个刻度数字。 */
static lv_obj_t *face, *number, *instruction, *hint, *marks[10];

/** @brief 画短线，用于刻度、指针和锁舌。 */
static void line(lv_layer_t *layer, int x, int y, int end_x, int end_y, int width, unsigned color)
{
    lv_draw_line_dsc_t pen; /**< 线段颜色和端点。 */
    lv_draw_line_dsc_init(&pen);
    pen.p1 = (lv_point_precise_t){x, y};
    pen.p2 = (lv_point_precise_t){end_x, end_y};
    pen.width = width;
    pen.color = lv_color_hex(color);
    pen.round_start = pen.round_end = true;
    lv_draw_line(layer, &pen);
}

/** @brief 绘制面板圆环、填色锁销或外壳。 */
static void shape(lv_layer_t *layer, int x, int y, int w, int h, int radius, unsigned fill, int border, unsigned edge)
{
    lv_draw_rect_dsc_t metal; /**< 填充、边线和边角。 */
    lv_draw_rect_dsc_init(&metal);
    metal.bg_color = lv_color_hex(fill);
    metal.bg_opa = LV_OPA_COVER;
    metal.border_width = border;
    metal.border_color = lv_color_hex(edge);
    metal.radius = radius;
    lv_area_t area = {x, y, x+w-1, y+h-1}; /**< 物理像素区域。 */
    lv_draw_rect(layer, &metal, &area);
}

/** @brief 绘制底盘、随动作转动的一百个刻度，以及收回的锁舌。 */
static void draw_face(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event); /**< 本帧使用的绘制层。 */
    shape(layer, 5, 5, 456, 456, 228, 0x1f3031, 3, 0x907855);
    shape(layer, 17, 17, 432, 432, 216, FACE, 2, 0x60706a);
    shape(layer, 84, 84, 298, 298, 149, 0x19292a, 2, 0x6b604d);
    for (int i = 0; i < 100; i++) {
        float angle = (i - view.number) * 0.062831853f; /**< 当前刻度相对十二点的角度。 */
        int radius = i % 5 == 0 ? 184 : 196; /**< 五格一道长刻度。 */
        float x = sinf(angle), y = -cosf(angle); /**< 单位圆上的横纵坐标。 */
        line(layer, 233 + radius*x, 233 + radius*y, 233 + 205*x, 233 + 205*y,
             i % 5 == 0 ? 3 : 2, i % 5 == 0 ? BRASS : STEEL);
    }
    line(layer, 225, 13, 233, 25, 4, IVORY);
    line(layer, 233, 25, 241, 13, 4, IVORY);
    // 上方锁舌从锁座中向左收回，右侧留出可见空隙。
    shape(layer, 197, 130, 43, 19, 4, 0x314442, 1, STEEL);
    shape(layer, 254, 128, 15, 23, 3, 0x314442, 2, STEEL);
    shape(layer, 202, 135, 59 - (int)(opening * 27), 9, 3, view.phase == UNLOCKED ? OPEN : BRASS, 0, 0);
    for (int i = 0; i < 3; i++) {
        bool is_set = i < view.step; /**< 已经打开的锁销。 */
        int x = 200 + 24*i; /**< 三个进度标记的左边界。 */
        shape(layer, x, 306, 18, 6, 3, is_set ? OPEN : 0x364b49, 0, 0);
        if (i == view.step && view.phase == DIALING) {
            int hold = 2 + (int)(16 * fminf(1, view.elapsed / 0.55f)); /**< 当前停留进度。 */
            shape(layer, x, 306, hold, 6, 3, BRASS, 0, 0);
        }
    }
}

/** @brief 在圆盘上创建居中的静态或动态文字。 */
static lv_obj_t *label(const lv_font_t *font, unsigned color, const char *text, int y)
{
    lv_obj_t *item = lv_label_create(face); /**< 新文字对象。 */
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(color), 0);
    lv_label_set_text(item, text);
    lv_obj_align(item, LV_ALIGN_CENTER, 0, y);
    return item;
}

/** @brief 仅在文案有变化时更新。 */
static void set_text(lv_obj_t *item, const char *text)
{
    if (lv_strcmp(lv_label_get_text(item), text) != 0) { lv_label_set_text(item, text); }
}

/** @brief 将一份采样快照渲染为操作提示、刻度位置和开锁动画。 */
static void refresh(lv_timer_t *timer)
{
    (void)timer;
    struct dial next = motion_snapshot(); /**< 最近一次完整采样。 */
    if (next.phase == UNLOCKED && view.phase != UNLOCKED) { opened_at = lv_tick_get(); }
    float slide = next.phase == UNLOCKED ? fminf(1, lv_tick_elaps(opened_at)/420.0f) : 0; /**< 锁舌移动比例。 */
    bool has_change = view.phase != next.phase || view.step != next.step || view.number != next.number ||
                      (int)(view.elapsed*30) != (int)(next.elapsed*30) || slide != opening;
    view = next;
    opening = slide;
    char text[48]; /**< 当前数字与目标提示的短暂格式化缓冲。 */
    snprintf(text, sizeof(text), "%02d", (int)lroundf(view.number) % 100);
    set_text(number, text);
    if (view.phase == DIALING) {
        snprintf(text, sizeof(text), "%s  %02d", dial_is_clockwise(&view) ? "顺转至" : "逆转至", dial_target(&view));
        set_text(instruction, text);
        if (fabsf(view.number - dial_target(&view)) <= 1.05f && view.approach < 1.5f) {
            // 到了数字但最后几格转反时，给出可直接照做的回拨位置。
            if (dial_is_clockwise(&view)) {
                snprintf(text, sizeof(text), "逆转至 %02d 再顺转", dial_target(&view) - 5);
            } else {
                snprintf(text, sizeof(text), "顺转至 %02d 再逆转", dial_target(&view) + 5);
            }
            set_text(hint, text);
        } else {
            set_text(hint, view.elapsed > 0 ? "保持静止" : "对准后停稳");
        }
    } else if (view.phase == UNLOCKED) {
        set_text(instruction, "保险箱已打开");
        set_text(hint, "轻点再玩");
    } else {
        set_text(instruction, view.phase == ZEROING ? "请握稳圆盘" : view.phase == ORIENTING ? "顺时针轻转" : "等待传感器");
        set_text(hint, view.phase == ORIENTING ? "转动整块屏幕" : view.phase == ZEROING ? "正在校准" : "正在连接");
    }
    if (has_change) {
        for (int i = 0; i < 10; i++) {
            float angle = (i*10 - view.number) * 0.062831853f; /**< 标注对应每十格的位置。 */
            lv_obj_align(marks[i], LV_ALIGN_CENTER, lroundf(167*sinf(angle)), -lroundf(167*cosf(angle)));
        }
        lv_obj_invalidate(face);
    }
}

/** @brief 点击已开锁的圆盘开始下一轮。 */
static void replay(lv_event_t *event)
{
    (void)event;
    if (view.phase == UNLOCKED && opening >= 1) { motion_restart(); }
}

/** @brief 创建显示对象并启动三十帧每秒的界面刷新。 */
void ui_start(void)
{
    face = lv_screen_active();
    lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(face, lv_color_hex(FACE), 0);
    lv_obj_set_style_pad_all(face, 0, 0);
    lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face, draw_face, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(face, replay, LV_EVENT_CLICKED, NULL);
    for (int i = 0; i < 10; i++) {
        char mark[3]; /**< 两位刻度编号。 */
        snprintf(mark, sizeof(mark), "%02d", i*10);
        marks[i] = label(&lv_font_montserrat_14, BRASS, mark, 0);
        float angle = i * 0.62831853f; /**< 初始零位的刻度角。 */
        lv_obj_align(marks[i], LV_ALIGN_CENTER, lroundf(167*sinf(angle)), -lroundf(167*cosf(angle)));
    }
    number = label(&lv_font_montserrat_48, IVORY, "00", -32);
    instruction = label(&safe_font, BRASS, "请握稳圆盘", 19);
    hint = label(&safe_font, 0xacc3b8, "正在校准", 103);
    refresh(NULL);
    lv_timer_create(refresh, 33, NULL);
}
