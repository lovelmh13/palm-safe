# 职责：注册保险箱玩法、圆盘界面、字体和六轴采集。
idf_component_register(SRCS "main.c" "dial.c" "motion.c" "ui.c" "safe_font.c"
    INCLUDE_DIRS "." REQUIRES esp_timer esp_driver_i2c)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=cpp)
