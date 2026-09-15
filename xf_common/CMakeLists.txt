# 公共main组件
idf_component_register(SRCS             "./main/entry.c"
                        INCLUDE_DIRS    "./main"
                        PRIV_REQUIRES   common
                        )