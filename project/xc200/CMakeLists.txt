set(embedded_images)
file(GLOB_RECURSE embedded_images images/*.c)

set(LV_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/inc/lv_conf.h")

file(GLOB_RECURSE APP_SOURCES
     ${CMAKE_CURRENT_LIST_DIR}/src/*.c)

idf_component_register(SRCS ${APP_SOURCES}
                            ${embedded_images}
                        PRIV_REQUIRES esp_lcd esp_driver_gpio spiffs esp_psram esp_timer lvgl common
                        INCLUDE_DIRS "./inc/")

idf_build_set_property(COMPILE_DEFINITIONS "LV_CONF_PATH=\"${LV_CONF_PATH}\"" APPEND)
