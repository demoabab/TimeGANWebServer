# 1. 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 项目名称
project(TinyWebServer)

# 3. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ---------- ONNX Runtime 配置 ----------
# 设置 ONNX Runtime 路径（默认项目根目录下的 onnxruntime/）
if(NOT DEFINED ONNXRUNTIME_ROOT)
    set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime")
endif()
set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")
find_library(ONNXRUNTIME_LIB onnxruntime
    PATHS "${ONNXRUNTIME_ROOT}/lib"
    NO_DEFAULT_PATH)
if(NOT ONNXRUNTIME_LIB)
    find_library(ONNXRUNTIME_LIB onnxruntime REQUIRED)
endif()
message(STATUS "ONNX Runtime lib: ${ONNXRUNTIME_LIB}")
# --------------------------------------

# 4. 查找必要的库
# 4.1 查找线程库
find_package(Threads REQUIRED)

# 4.2 查找 MySQL 库
set(MYSQL_LIB mysqlclient)

# 4.3 查找 hiredis 库
find_library(HIREDIS_LIB hiredis REQUIRED)

# 5. 包含头文件路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lock
    ${CMAKE_CURRENT_SOURCE_DIR}/threadpool
    ${CMAKE_CURRENT_SOURCE_DIR}/timer
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/CGImysql
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

# 6. 定义源文件
set(SOURCE_FILES
    main.cpp
    webserver.cpp
    config.cpp
    timer/lst_timer.cpp
    http/http_conn.cpp
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
    model_inference.cpp   # 新增
    redis_manager.cpp     # Redis 会话管理
)

# 7. 生成可执行文件
add_executable(server ${SOURCE_FILES})

# 8. 链接库
target_link_libraries(server PRIVATE
    Threads::Threads
    ${MYSQL_LIB}
    ${HIREDIS_LIB}
    ${ONNXRUNTIME_LIB}
)