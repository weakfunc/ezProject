# main 组件注册
# SRCS    —— 参与编译的源文件列表
# INCLUDE_DIRS —— 头文件搜索目录（"." 表示 main/ 目录自身）
set(srcs "main.c"
         "gatt_svr.c")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS ".")
