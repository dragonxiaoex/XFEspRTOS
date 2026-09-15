# 公共main组件
idf_component_register(SRCS             "bsp/xf_uart_bsp.c"
                                        "utils/xf_utils.c"
                                        "protocol/xf_mcu_protocol.c"
                        INCLUDE_DIRS    "bsp"
                                        "private"
                                        "utils"
                                        "protocol"
                        REQUIRES        esp_driver_uart
                        )

# 公共日志使用当前产品的配置头文件，并将该路径提供给依赖公共组件的代码。
# 公共 menuconfig 没有产品上下文，仅配置组件，不编译产品固件。
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND XF_PROJECT_DIR)
    target_include_directories(${COMPONENT_LIB} PUBLIC "${XF_PROJECT_DIR}/inc")
endif()
