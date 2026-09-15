/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"

static lv_style_t style_bullet;
static lv_obj_t *scale1;
static const lv_font_t *font_normal = &lv_font_montserrat_14;

static lv_obj_t *create_scale_box(lv_obj_t *parent, const char *text1, const char *text2, const char *text3)
{
    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_center(scale);
    lv_obj_set_size(scale, 300, 300);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_label_show(scale, false);
    lv_scale_set_post_draw(scale, true);
    lv_obj_set_width(scale, LV_PCT(100));
    lv_obj_set_style_pad_all(scale, 30, 0);

    lv_obj_t *bullet1 = lv_obj_create(parent);
    lv_obj_set_size(bullet1, 13, 13);
    lv_obj_remove_style(bullet1, NULL, LV_PART_SCROLLBAR);
    lv_obj_add_style(bullet1, &style_bullet, 0);
    lv_obj_set_style_bg_color(bullet1, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_t *label1 = lv_label_create(parent);
    lv_label_set_text(label1, text1);

    lv_obj_t *bullet2 = lv_obj_create(parent);
    lv_obj_set_size(bullet2, 13, 13);
    lv_obj_remove_style(bullet2, NULL, LV_PART_SCROLLBAR);
    lv_obj_add_style(bullet2, &style_bullet, 0);
    lv_obj_set_style_bg_color(bullet2, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_t *label2 = lv_label_create(parent);
    lv_label_set_text(label2, text2);

    lv_obj_t *bullet3 = lv_obj_create(parent);
    lv_obj_set_size(bullet3, 13, 13);
    lv_obj_remove_style(bullet3,  NULL, LV_PART_SCROLLBAR);
    lv_obj_add_style(bullet3, &style_bullet, 0);
    lv_obj_set_style_bg_color(bullet3, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_t *label3 = lv_label_create(parent);
    lv_label_set_text(label3, text3);

    static int32_t grid_col_dsc[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t grid_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(parent, grid_col_dsc, grid_row_dsc);
    lv_obj_set_grid_cell(scale, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 1, 1);
    lv_obj_set_grid_cell(bullet1, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 2, 1);
    lv_obj_set_grid_cell(bullet2, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 3, 1);
    lv_obj_set_grid_cell(bullet3, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 4, 1);
    lv_obj_set_grid_cell(label1, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 2, 1);
    lv_obj_set_grid_cell(label2, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 3, 1);
    lv_obj_set_grid_cell(label3, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 4, 1);
    return scale;
}

static void scale1_indic1_anim_cb(void *var, int32_t v)
{
    lv_arc_set_value(var, v);

    lv_obj_t *card = lv_obj_get_parent(scale1);
    lv_obj_t *label = lv_obj_get_child(card, -5);
    lv_label_set_text_fmt(label, "Revenue: %"LV_PRId32" %%", v);
}

static void scale1_indic2_anim_cb(void *var, int32_t v)
{
    lv_arc_set_value(var, v);

    lv_obj_t *card = lv_obj_get_parent(scale1);
    lv_obj_t *label = lv_obj_get_child(card, -3);
    lv_label_set_text_fmt(label, "Sales: %"LV_PRId32" %%", v);
}

static void scale1_indic3_anim_cb(void *var, int32_t v)
{
    lv_arc_set_value(var, v);

    lv_obj_t *card = lv_obj_get_parent(scale1);
    lv_obj_t *label = lv_obj_get_child(card, -1);
    lv_label_set_text_fmt(label, "Costs: %"LV_PRId32" %%", v);
}

#include <math.h>
#include "sdkconfig.h"
#include "lvgl.h"

LV_IMG_DECLARE(esp_logo)
LV_IMG_DECLARE(esp_text)
LV_IMG_DECLARE(xf_eye)

#define SCREEN_W 222
#define SCREEN_H 480

#define EYE_W 260
#define EYE_H 180

#define EYE_RANDOM_MOVE_PERIOD_MS 300
#define EYE_COMMAND_X_OFFSET 80

static lv_obj_t *img_logo = NULL;
static lv_obj_t *eye_img = NULL;
static lv_obj_t *img_text = NULL;

static volatile int now_x = 0, now_y = 0;

#include <math.h>

int eye_anim_time(int cur_x, int cur_y, int tar_x, int tar_y)
{
    int dx = tar_x - cur_x;
    int dy = tar_y - cur_y;

    float distance = sqrtf(dx * dx + dy * dy);

    // ====== 生理参数 ======
    const float T_MIN = 25.0f;   // 最小跳动时间 25ms
    const float K     = 4.0f;    // sqrt 缩放系数

    float t = T_MIN + K * sqrtf(distance);

    // 限制最大时间
    if (t > 140.0f)
        t = 140.0f;

    return (int)t;
}

// 更新眼睛位置的回调
static void eye_anim_x_cb(void *obj, int32_t v)
{
    lv_obj_t *eye = (lv_obj_t *)obj;

    now_x = v;
    lv_obj_set_pos(eye, now_x, now_y);
}

static void eye_anim_y_cb(void *obj, int32_t v)
{
    lv_obj_t *eye = (lv_obj_t *)obj;

    now_y = v;
    lv_obj_set_pos(eye, now_x, now_y);
}

#include "xf_lvgl_port.h"
LV_IMG_DECLARE(xf_calib)

static lv_anim_t anim_x;
static lv_anim_t anim_y;
static lv_timer_t *eye_random_timer = NULL;

static void eye_anim_data_init(void)
{
    //初始化动画参数
    lv_anim_init(&anim_x);
    lv_anim_set_var(&anim_x, eye_img);
    lv_anim_set_exec_cb(&anim_x, eye_anim_x_cb);
    lv_anim_set_repeat_count(&anim_x, 0);
    lv_anim_set_path_cb(&anim_x, lv_anim_path_ease_out);  // 使用加速后减速路径

    lv_anim_init(&anim_y);
    lv_anim_set_var(&anim_y, eye_img);
    lv_anim_set_exec_cb(&anim_y, eye_anim_y_cb);
    lv_anim_set_repeat_count(&anim_y, 0);
    lv_anim_set_path_cb(&anim_y, lv_anim_path_ease_out);  // 使用加速后减速路径
}

/**
 * @brief 在已持有 LVGL 锁的上下文中移动眼睛
 *
 * @param [in] x - 目标左上角横坐标，单位像素.
 * @param [in] y - 目标左上角纵坐标，单位像素.
 */
static void eye_move_to(int32_t x, int32_t y)
{
    if (eye_img == NULL) {
        return;
    }

    int32_t anim_time = eye_anim_time(now_x, now_y, x, y);

    lv_anim_del(eye_img, eye_anim_x_cb);
    lv_anim_del(eye_img, eye_anim_y_cb);

    lv_anim_set_values(&anim_x, now_x, x);
    lv_anim_set_time(&anim_x, anim_time);       // 一次水平移动耗时

    lv_anim_set_values(&anim_y, now_y, y);
    lv_anim_set_time(&anim_y, anim_time);       // 一次垂直移动耗时

    lv_anim_start(&anim_x);
    lv_anim_start(&anim_y);
}

void eye_test_pos(uint8_t x, uint8_t y)
{
    xf_lvgl_port_lock(0);
    /* 指令坐标只在入口转换一次，动画起点和终点统一使用屏幕坐标。 */
    eye_move_to((int32_t)x - EYE_COMMAND_X_OFFSET, y);
    xf_lvgl_port_unlock();
}

/**
 * @brief 定时选取随机目标并启动眼睛动画
 *
 * @param [in] timer - LVGL 测试定时器.
 */
static void eye_random_move_timer_cb(lv_timer_t *timer)
{
    int32_t x_span = SCREEN_W - (int32_t)xf_eye.header.w;
    int32_t y_span = SCREEN_H - (int32_t)xf_eye.header.h;

    /* 图片较小时保持完整可见，较大时仅在超出屏幕的范围内移动。 */
    int32_t x = LV_MIN(x_span, 0) + (int32_t)lv_rand(0, (uint32_t)LV_ABS(x_span));
    int32_t y = LV_MIN(y_span, 0) + (int32_t)lv_rand(0, (uint32_t)LV_ABS(y_span));

    /* 定时器由持有 LVGL 锁的任务执行，无需再次加锁。 */
    eye_move_to(x, y);
}

/**
 * @brief 在界面初始化时启动持续随机移动测试，调用时需持有 LVGL 锁
 */
static void eye_random_move_start(void)
{
    if (eye_random_timer == NULL) {
        eye_random_timer = lv_timer_create(eye_random_move_timer_cb, EYE_RANDOM_MOVE_PERIOD_MS, NULL);
    }

    if (eye_random_timer == NULL) {
        return;
    }

    lv_timer_ready(eye_random_timer);
}

static lv_obj_t *eye_calib_obj = NULL;
void eye_calib(uint8_t calib_area)
{
    xf_lvgl_port_lock(0);

    switch (calib_area) {
    case 0:
        lv_obj_set_pos(eye_calib_obj, 0, 0);
        break;

    case 2:
        lv_obj_set_pos(eye_calib_obj, (SCREEN_W - 50) / 2, (SCREEN_H - 50) / 2);
        break;

    case 3:
        lv_obj_set_pos(eye_calib_obj, (SCREEN_W - 50) / 2, 0);
        break;

    case 4:
        lv_obj_set_pos(eye_calib_obj, (SCREEN_W - 50) / 2, SCREEN_H);
        break;

    case 5:
        lv_obj_set_pos(eye_calib_obj, SCREEN_W, (SCREEN_H - 50) / 2);
        break;

    case 6:
        lv_obj_set_pos(eye_calib_obj, 0, (SCREEN_H - 50) / 2);
        break;

    default:
        break;
    }

    xf_lvgl_port_unlock();
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);       // 屏幕不滚动
    lv_obj_set_style_pad_all(scr, 0, 0);                 // 去掉 padding
    lv_obj_set_style_border_width(scr, 0, 0);            // 去掉边框

    // Create image
    eye_img = lv_image_create(scr);
    lv_image_set_src(eye_img, &xf_eye);

    // 初始位置贴顶中间
    lv_obj_set_pos(eye_img, 0, 0);
    eye_anim_data_init();
    eye_random_move_start();

    //eye_calib_obj = lv_image_create(scr);
    //lv_image_set_src(eye_calib_obj, &xf_calib);

    // 创建水平动画
    /* static lv_anim_t anim_h;
    lv_anim_init(&anim_h);
    lv_anim_set_var(&anim_h, eye_img);
    lv_anim_set_exec_cb(&anim_h, eye_anim_cb);
    lv_anim_set_values(&anim_h, 50, SCREEN_H - EYE_H - 50);
    lv_anim_set_time(&anim_h, 300);       // 一次水平移动耗时
    lv_anim_set_playback_time(&anim_h, 300); // 回退动画耗时
    lv_anim_set_repeat_count(&anim_h, LV_ANIM_REPEAT_INFINITE); // 无限循环
    lv_anim_set_path_cb(&anim_h, lv_anim_path_ease_in_out);  // 使用线性路径（平滑移动）
    lv_anim_start(&anim_h); */


    /* // init default theme
    lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), LV_THEME_DEFAULT_DARK,
                          font_normal);
    // bullet style
    lv_style_init(&style_bullet);
    lv_style_set_border_width(&style_bullet, 0);
    lv_style_set_radius(&style_bullet, LV_RADIUS_CIRCLE);

    lv_obj_t *parent = lv_display_get_screen_active(disp);

    // create scale widget
    scale1 = create_scale_box(parent, "Revenue", "Sales", "Costs");

    // create arc indicators
    lv_obj_t *arc;
    arc = lv_arc_create(scale1);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_style(arc, NULL, LV_PART_MAIN);
    lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
    lv_obj_set_style_arc_opa(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_values(&a, 20, 100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, scale1_indic1_anim_cb);
    lv_anim_set_var(&a, arc);
    lv_anim_set_duration(&a, 4100);
    lv_anim_set_playback_duration(&a, 2700);
    lv_anim_start(&a);

    arc = lv_arc_create(scale1);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
    lv_obj_set_style_margin_all(arc, 20, 0);
    lv_obj_set_style_arc_opa(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);

    lv_anim_set_exec_cb(&a, scale1_indic2_anim_cb);
    lv_anim_set_var(&a, arc);
    lv_anim_set_duration(&a, 2600);
    lv_anim_set_playback_duration(&a, 3200);
    lv_anim_start(&a);

    arc = lv_arc_create(scale1);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
    lv_obj_set_style_margin_all(arc, 40, 0);
    lv_obj_set_style_arc_opa(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);

    lv_anim_set_exec_cb(&a, scale1_indic3_anim_cb);
    lv_anim_set_var(&a, arc);
    lv_anim_set_duration(&a, 2800);
    lv_anim_set_playback_duration(&a, 1800);
    lv_anim_start(&a); */
}
