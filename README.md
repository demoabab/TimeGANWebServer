# CTAM-TimeGAN Server: 水下柔性机械臂智能故障诊断与数据生成系统

> 将本人研究成果 **CTAM-TimeGAN**（通道-时序注意力机制生成对抗网络）无缝部署到生产级 C++ Web 服务器。
> 论文地址：https://doi.org/10.1016/j.neucom.2026.132667

> **一键上传传感器数据 → 自动故障分类 → 调用专属生成模型 → 输出高保真时序样本**，彻底解决水下柔性机械臂故障诊断中的**数据严重不平衡**问题。

---

## 项目背景

水下柔性机械臂在深海勘探、样本采集等任务中极易发生故障，但真实故障数据极难获取，导致传统诊断模型因类别严重不平衡（健康样本远多于故障样本）而失效。

本项目的核心创新——**CTAM-TimeGAN**（已发表于 *Neurocomputing*）——通过**通道-时序双重注意力机制**，在时序生成对抗网络中同时聚焦关键传感器通道与故障演化时间点，生成与真实故障信号高度一致的高质量时序数据。

该服务器将研究成果**工程化落地**，实现**实时诊断 + 按需生成**的闭环解决方案。

---

## 核心特性

- **高精度故障分类**
  内置 CNN-LSTM 分类器，从原始压力信号中自动提取时空特征，准确识别 4种工况 × 2种负载（共8类）：
  - 正常 (Normal)、泵堵塞 (Pump Blockage)、轻微泄漏 (Minor Leakage)、严重泄漏 (Heavy Leakage)
  - 分别对应 0g 与 100g 两种载荷

- **条件时序数据生成**
  分类器判定类别后，自动调用该类专属的 CTAM-TimeGAN 生成器（共8个独立模型），生成任意数量的合成故障信号，用于数据增强、离线分析或模型再训练。

- **高性能异步 Web 服务**
  - Epoll + 非阻塞 I/O + Reactor/Proactor 双模式
  - 时间轮定时器自动管理空闲连接
  - 线程池 + MySQL 连接池充分利用多核 CPU
  - mmap + writev 零拷贝技术优化静态文件传输
  - 压测验证：1000 并发稳定运行，QPS 2800+，零崩溃

- **Redis 会话管理**
  基于 Cookie 的登录验证，Session 持久化至 Redis 并支持 TTL 自动过期，解决进程重启丢失登录态问题。

- **ONNX Runtime 推理引擎**
  从 LibTorch 迁移至 ONNX Runtime，消除 2GB+ 运行时依赖，推理体积降至 ~200MB，单次推理约 5ms。

- **Docker 一键部署**
  `docker compose up -d` 即可启动 MySQL + Redis + Server 完整服务栈。

---

## 技术栈

| 模块      | 技术选型                                     |
| --------- | -------------------------------------------- |
| I/O 模型  | Epoll (ET/LT) + 非阻塞 Socket                |
| 并发模型  | Reactor / Proactor 双模式，线程池            |
| HTTP 协议 | 手写状态机解析，GET/POST/302 重定向          |
| 模型推理  | ONNX Runtime (CPU)，管理 1 分类器 + 8 生成器 |
| 会话管理  | Redis 存储 Session + TTL 自动过期            |
| 数据库    | MySQL 连接池 (RAII + 信号量)                 |
| 日志      | 异步日志系统 (双缓冲区 + 条件变量)，按天滚动 |
| 容器化    | Docker + Docker Compose                      |
| 测试      | wrk 压力测试，覆盖静态文件与推理接口         |

---

## 系统架构

```
[用户] ──(CSV/JSON)──► [C++ Web Server] ──┬─► [Redis 会话验证]
                                           │
                                           ├─► [CNN-LSTM 分类器]
                                           │        │
                                           │   ┌────┴────┐
                                           │   ▼         ▼
                                           │ [类别 0~7]  [生成器 0~7]
                                           │   │              │
                                           │   └──────┬───────┘
                                           │          ▼
                                           └──► [返回 CSV 样本 / JSON 结果]
```

```
┌─────────────────────────────────────────────────────────┐
│                      main thread                         │
│  ┌──────────┐  ┌──────────┐  ┌────────────────────┐    │
│  │ Epoll    │  │ Time     │  │ Signal Handler     │    │
│  │ Event    │──│ Wheel    │──│ (SIGALRM/SIGTERM)  │    │
│  │ Loop     │  │ Timer    │  └────────────────────┘    │
│  └────┬─────┘  └──────────┘                              │
│       │ EPOLLIN/EPOLLOUT                                  │
│  ┌────▼──────────────────────────────────────────────┐  │
│  │                   Thread Pool                       │  │
│  │   Worker#1  Worker#2  Worker#3  ...  Worker#8      │  │
│  └────┬──────────┬──────────┬──────────┬──────────────┘  │
│       │          │          │          │                  │
│  ┌────▼──────────▼──────────▼──────────▼──────────────┐  │
│  │                HTTP State Machine                    │  │
│  │     Request Line → Headers → Body → do_request()    │  │
│  └────┬───────────┬──────────┬─────────────────────────┘  │
│       │           │          │                             │
│  ┌────▼────┐ ┌───▼───┐ ┌───▼───────────┐                 │
│  │  MySQL  │ │ Redis │ │ ONNX Runtime  │                 │
│  │  Pool   │ │Session│ │   9 Models    │                 │
│  └─────────┘ └───────┘ └───────────────┘                 │
└─────────────────────────────────────────────────────────┘
```

---

## 性能测试

测试环境：Ubuntu 22.04, Intel Xeon E5-2678 v3, MySQL 5.7, Redis 6.0

### 静态文件 (GET /diagnose.html, mmap + writev)

| 并发连接 | QPS  | 平均延迟 | P99 延迟 | 吞吐量    | 超时 |
| -------- | ---- | -------- | -------- | --------- | ---- |
| 100      | 2952 | 32ms     | 252ms    | 26.3 MB/s | 41   |
| 500      | 2819 | 54ms     | 409ms    | 25.1 MB/s | 302  |
| 1000     | 2841 | 57ms     | 452ms    | 25.3 MB/s | 675  |

### 推理接口 (POST /diagnose, ONNX Runtime CPU)

| 并发连接 | QPS  | 平均延迟 | P99 延迟 |
| -------- | ---- | -------- | -------- |
| 100      | 1714 | 54ms     | 119ms    |
| 500      | 1630 | 272ms    | 449ms    |

> 详细压测报告见 [bench/README.md](bench/README.md)

---

## API 接口

### POST `/diagnose` —— 故障诊断与数据生成

**场景一：上传 CSV 样本 → 分类 → 生成同类样本**

```json
{
    "csv_data": "0.123,0.456,0.789\n0.234,0.567,0.890\n...（共64行，每行3个数值）",
    "num_samples": 10
}
```

**场景二：直接生成样本（无需上传，快速数据增强）**

```json
{
    "num_samples": 5
}
```

响应：`Content-Type: text/csv`，直接下载生成数据。

### POST `/2` —— 登录

### POST `/3` —— 注册

### GET/POST `/4` —— 登出

---

## 快速开始

### 环境要求

- Ubuntu 20.04+ / Debian 11+
- CMake 3.10+, g++ (支持 C++17)
- MySQL 5.7+, Redis 6.0+
- ONNX Runtime 1.18+

### 安装依赖

```bash
sudo apt update
sudo apt install build-essential cmake libmysqlclient-dev redis-server libhiredis-dev
```

### 安装 ONNX Runtime

```bash
# 方式一：手动下载
wget https://github.com/microsoft/onnxruntime/releases/download/v1.18.0/onnxruntime-linux-x64-1.18.0.tgz
tar -xzf onnxruntime-linux-x64-1.18.0.tgz -C ~/projects/CTAM-TimeGAN-Server/
mv onnxruntime-linux-x64-1.18.0 onnxruntime

# 方式二：pip 安装后指定路径
pip install onnxruntime
cmake -DONNXRUNTIME_ROOT=$(python -c "import onnxruntime; import os; print(os.path.dirname(onnxruntime.__file__))") ..
```

### 配置 MySQL

```sql
CREATE DATABASE tinywebserver;
USE tinywebserver;
CREATE TABLE user(username char(50) NULL, passwd char(50) NULL);
INSERT INTO user VALUES ('admin', 'admin123');
```

### 编译 & 运行

```bash
cd CTAM-TimeGAN-Server
mkdir build && cd build
cmake ..
make -j$(nproc)

# 将 9 个 ONNX 模型放入 build/models/
export LD_LIBRARY_PATH=$PWD/../onnxruntime/lib:$LD_LIBRARY_PATH
sudo service mysql start
redis-server --daemonize yes

./server
```

浏览器访问 `http://服务器IP:9006`

### Docker 一键部署

```bash
docker compose up -d
```

详见 [docker-compose.yml](docker-compose.yml)

---

## 命令行参数

```
./server [-p port] [-H mysql_host] [-l log] [-m trig] [-o linger]
         [-s sql_num] [-t threads] [-c close_log] [-a actor]
         [-R redis_host] [-r redis_port] [-w redis_password] [-T session_ttl]
```

| 参数 | 说明                            | 默认值    |
| ---- | ------------------------------- | --------- |
| -p   | 服务端口                        | 9006      |
| -H   | MySQL 主机地址                  | 127.0.0.1 |
| -s   | 数据库连接池大小                | 8         |
| -t   | 线程池大小                      | 8         |
| -a   | 并发模型 (0:Proactor 1:Reactor) | 0         |
| -l   | 日志模式 (0同步 1异步)          | 0         |
| -m   | 触发模式 (0~3)                  | 0         |
| -R   | Redis 主机                      | 127.0.0.1 |
| -r   | Redis 端口                      | 6379      |
| -w   | Redis 密码                      | 无        |
| -T   | 会话过期时间(秒)                | 3600      |

### 模型转换 (.pt → .onnx)

```python
import torch

def export_model(pt_path, onnx_path):
    model = torch.jit.load(pt_path)
    dummy = torch.randn(1, 64, 3)
    torch.onnx.export(model, dummy, onnx_path,
                      input_names=["input"], output_names=["output"])

export_model("classifier.pt", "classifier.onnx")
for i in range(8):
    export_model(f"generator_{i}.pt", f"generator_{i}.onnx")
```

---

## 文档索引

| 文档                                           | 内容                        |
| ---------------------------------------------- | --------------------------- |
| [bench/README.md](bench/README.md)             | 压力测试完整报告            |
| [docs/ONNX_RUNTIME.md](docs/ONNX_RUNTIME.md)   | ONNX Runtime 迁移与架构设计 |
| [docs/REDIS_SESSION.md](docs/REDIS_SESSION.md) | Redis 会话管理设计与实现    |

---

## 使用示例 (curl)

```bash
# 1. 登录获取 session
curl -X POST http://localhost:9006/2 \
     -H "Content-Type: application/x-www-form-urlencoded" \
     -d "user=admin&password=admin123" \
     -c cookies.txt -L

# 2. 直接生成 10 个样本
curl -X POST http://localhost:9006/diagnose \
     -H "Content-Type: application/json" \
     -b cookies.txt \
     -d '{"num_samples": 10}' \
     --output synthetic.csv

# 3. 登出
curl -X POST http://localhost:9006/4 -b cookies.txt -L
```

---

## 论文引用

如果您在学术工作中使用本服务器或相关模型，请引用：

https://doi.org/10.1016/j.neucom.2026.132667

---

## 项目资助

- 青岛市自然科学基金 (24-4-4-zrjj-168-jch)
- 山东省自然科学基金 (ZR2025MS860)
- 黑龙江省自然科学基金 (YQ2024E036)
- 中央高校基本科研业务费专项资金 (3072025ZX2604)
- 泰山学者计划 (tsqn202312317)

---

## 许可证

MIT License

## 联系方式

**项目维护**：Wei Wang (heu_wangwei@163.com)
