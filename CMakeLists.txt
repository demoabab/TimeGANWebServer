# 1. 设置 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 项目名称
project(TinyWebServer)

# 3. 设置 C++ 标准 (LibTorch 需要 C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ---------- 新增：LibTorch 配置 ----------
# 设置 LibTorch 路径（假设 libtorch 文件夹位于项目根目录下）
set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libtorch")
find_package(Torch REQUIRED)
# --------------------------------------

# 4. 查找必要的库
# 4.1 查找线程库
find_package(Threads REQUIRED)

# 4.2 查找 MySQL 库
set(MYSQL_LIB mysqlclient)

# 5. 包含头文件路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lock
    ${CMAKE_CURRENT_SOURCE_DIR}/threadpool
    ${CMAKE_CURRENT_SOURCE_DIR}/timer
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/CGImysql
    ${TORCH_INCLUDE_DIRS}   # 新增：LibTorch 头文件
)

# 6. 定义源文件（添加 model_inference.cpp）
set(SOURCE_FILES
    main.cpp
    webserver.cpp
    config.cpp
    timer/lst_timer.cpp
    http/http_conn.cpp
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
    model_inference.cpp   # 新增
)

# 7. 生成可执行文件
add_executable(server ${SOURCE_FILES})

# 8. 链接库（添加 Torch 库和 hiredis）
target_link_libraries(server PRIVATE 
    Threads::Threads 
    ${MYSQL_LIB} 
    ${TORCH_LIBRARIES}
)