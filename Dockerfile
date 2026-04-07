# 1. 基础镜像
FROM ubuntu:20.04

# 2. 作者
LABEL maintainer="WangWei"

# 3. 环境变量
ENV DEBIAN_FRONTEND=noninteractive

# ================= 核心修改在这里 =================
# 4. 换源！把官方源替换成阿里云源，飞一般的速度
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 5. 安装依赖 (现在会非常快了)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libmysqlclient-dev \
    mysql-client \
    && rm -rf /var/lib/apt/lists/*
# =================================================

# 6. 工作目录
WORKDIR /app

# 7. 复制代码
COPY . /app

# 8. 编译
RUN mkdir build && \
    cd build && \
    cmake .. && \
    make

# 9. 暴露端口
EXPOSE 9006

# 10. 启动命令
CMD ["sh", "-c", "cd build && cp server .. && cd .. && ./server"]