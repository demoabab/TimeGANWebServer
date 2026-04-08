# `http_conn.cpp` – 水下柔性机械臂智能故障诊断的高性能推理服务核心

> 基于 **CTAM-TimeGAN** 生成对抗网络的水下柔性机械臂故障诊断服务器核心模块。  
> 模块负责：HTTP 协议解析、用户会话管理、分类与生成模型协同推理、CSV 数据流处理。

---

## 📌 项目背景

水下柔性机械臂在深海作业中极易发生**泵堵塞、轻微泄漏、严重泄漏**等故障，同时不同负载（0g / 100g）下故障特征分布差异巨大，导致传统监督学习面临严重的**数据不平衡**问题。  
本研究提出 **CTAM-TimeGAN** 框架，利用时序生成对抗网络为 8 种工况（4 类故障 × 2 种负载）分别训练生成模型，并结合 **CNN‑LSTM 分类器**实现“诊断→生成→数据增强”闭环。

`http_conn.cpp` 是整个 C++ 推理服务器的**核心业务逻辑层**，它连接了 Web 请求、数据库认证、深度学习模型（LibTorch）和文件服务。

---

## 🚀 核心功能

| 功能模块 | 描述 |
|---------|------|
| **会话管理** | 基于 `session_id` Cookie 的轻量级用户认证，支持登录状态保持 |
| **故障诊断接口** | `POST /diagnose` – 接收用户上传的 CSV 传感数据（64×3），调用 CNN‑LSTM 分类器预测工况（0~7），自动调用对应 CTAM‑TimeGAN 生成器生成指定数量的新样本 |
| **直接生成接口** | 无 CSV 上传时，使用默认生成器（0g+正常）直接生成样本，用于数据扩增或模拟 |
| **静态文件服务** | 提供登录、注册、诊断前端页面（`diagnose.html`）及资源文件 |
| **登录 / 注册** | 基于 MySQL 的用户表验证，注册成功自动创建 session |

---

## 🧠 模型推理链路

```
用户 CSV (64×3 时间步 × 3特征)
    ↓
分类器 (CNN‑LSTM) → 预测工况类别 [0..7]
    ↓
根据类别索引选择生成器 (generator_0.pt ~ generator_7.pt)
    ↓
生成器输入随机噪声 (64×3) → 输出 64×3 的修复/增强数据
    ↓
打包为 CSV 文件流返回给用户
```

- **分类器模型**：`models/classifier.pt`，输入 64×3，输出 8 类概率。
- **生成器模型**：`models/generator_{0..7}.pt`，每个模型对应一种工况（故障类型+负载组合）。
- 推理引擎：**LibTorch**（PyTorch C++ 前端），纯 CPU 执行（可扩展 GPU）。

---

## 🛠️ 技术栈

| 组件 | 技术 |
|------|------|
| HTTP 解析 | 手写状态机（支持 GET/POST、Header、Content-Length） |
| 并发模型 | Reactor + 线程池（主线程读写，工作线程执行 `process()`） |
| 数据库 | MySQL + 连接池（存储用户信息） |
| 深度学习 | LibTorch 1.x（`torch::jit::Module`） |
| JSON 处理 | nlohmann/json |
| 构建工具 | CMake + g++ |
| 系统 | Linux (epoll) |

---

## 📂 关键数据结构与接口

### `class SessionManager`
- 静态成员 `sessions_` 存储 `token → username`。
- `create_session()`：生成 32 位十六进制随机 token。
- `verify_session()`：从 Cookie 中提取 token 并验证。

### `http_conn` 核心方法
```cpp
HTTP_CODE process_read();      // 解析 HTTP 请求行/头/体
HTTP_CODE do_request();        // 路由 + 业务逻辑
bool process_write(HTTP_CODE); // 构造响应（JSON / CSV / HTML）
```

### 诊断接口请求示例（JSON）
```json
{
  "csv_data": "0.1,0.2,0.3\n0.4,0.5,0.6\n...（64行）",
  "num_samples": 5
}
```
响应：`Content-Type: text/csv`，附件下载 `result.csv`。

---

## ⚙️ 配置与初始化

### 模型路径约定
```cpp
void init_models() {
    classifier = new ModelInference("models/classifier.pt");
    for (int i = 0; i < 8; ++i) {
        std::string path = "models/generator_" + std::to_string(i) + ".pt";
        generators[i] = new ModelInference(path);
    }
}
```
请确保可执行文件同级目录下存在 `models/` 文件夹，并包含上述 9 个模型文件。

### 数据库表结构（简化）
```sql
CREATE TABLE user (
    username VARCHAR(50) PRIMARY KEY,
    passwd   VARCHAR(50) NOT NULL
);
```

---

## 🧪 部署与测试

### 1. 编译
```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/libtorch
make -j4
```

### 2. 运行
```bash
./webserver
```
默认监听端口 `9006`（可在 `config.h` 调整）。

### 3. 测试诊断接口
```bash
# 上传 CSV 样本并生成 3 个新样本
curl -X POST http://localhost:9006/diagnose \
  -H "Content-Type: application/json" \
  -d '{"csv_data":"0.1,0.2,0.3\n0.4,0.5,0.6\n...（64行完整数据）", "num_samples":3}' \
  --output generated_samples.csv
```

### 4. 测试直接生成（无 CSV）
```bash
curl -X POST http://localhost:9006/diagnose \
  -H "Content-Type: application/json" \
  -d '{"num_samples":10}' \
  --output synthetic_data.csv
```

### 5. 访问 Web 界面
浏览器打开 `http://localhost:9006/`，先注册/登录，然后上传 CSV 或直接生成。

---

## ⚡ 性能与优化点

- **零拷贝响应**：对于 CSV 生成结果，使用 `writev` 分散发送，避免数据拷贝到 `m_write_buf`。
- **模型预热**：全局静态模型指针，所有连接共享，避免重复加载。
- **非阻塞 I/O**：结合 epoll ET 模式，单线程可支撑数千并发。
- **连接定时器**：基于时间轮实现的空闲超时关闭，防止资源泄露。

---

## 🔮 后续扩展方向

- 支持 GPU 推理（将 `device_` 改为 `torch::kCUDA`）
- 增加 gRPC 接口以支持更高效的服务间调用
- 生成结果存入 MySQL / MongoDB，构建数据增强闭环
- 添加 Prometheus 监控指标（请求耗时、模型调用次数）

