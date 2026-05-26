# ============ 编译阶段 ============
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV ONNXRUNTIME_VERSION=1.18.0

RUN apt update && apt install -y \
    build-essential cmake \
    libmysqlclient-dev \
    libhiredis-dev \
    wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 下载并安装 ONNX Runtime
RUN wget -q https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz \
    && tar -xzf onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz \
    && mv onnxruntime-linux-x64-${ONNXRUNTIME_VERSION} /opt/onnxruntime \
    && rm onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz

WORKDIR /src
COPY . .

RUN mkdir -p build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release \
             -DONNXRUNTIME_ROOT=/opt/onnxruntime \
             .. \
    && make -j$(nproc)

# ============ 运行阶段 ============
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    libmysqlclient21 \
    libhiredis0.14 \
    && rm -rf /var/lib/apt/lists/*

# 复制 ONNX Runtime 运行时库
COPY --from=builder /opt/onnxruntime/lib/libonnxruntime.so* /usr/local/lib/
RUN ldconfig

WORKDIR /app

# 复制可执行文件
COPY --from=builder /src/build/server /app/server

# 复制静态资源
COPY --from=builder /src/build/root /app/root
RUN mkdir -p /app/models

EXPOSE 9006

CMD ["./server", "-H", "mysql", "-R", "redis"]
