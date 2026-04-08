# CTAM‑TimeGAN Server: 水下柔性机械臂智能故障诊断与数据生成系统

> 将本人研究成果**CTAM‑TimeGAN**（通道‑时序注意力机制生成对抗网络）无缝部署到生产级C++ Web服务器。
> 论文地址： https://doi.org/10.1016/j.neucom.2026.132667

> **一键上传传感器数据 → 自动故障分类 → 调用专属生成模型 → 输出高保真时序样本**，彻底解决水下柔性机械臂故障诊断中的**数据严重不平衡**问题。

---

## 📌 项目背景

水下柔性机械臂在深海勘探、样本采集等任务中极易发生故障，但真实故障数据极难获取，导致传统诊断模型因类别严重不平衡（健康样本远多于故障样本）而失效。

本项目的核心创新——**CTAM‑TimeGAN**（已发表于*Neurocomputing*）——通过**通道‑时序双重注意力机制**，在时序生成对抗网络中同时聚焦关键传感器通道与故障演化时间点，生成与真实故障信号高度一致的高质量时序数据。  
该服务器将这一研究成果**工程化落地**，实现**实时诊断 + 按需生成**的闭环解决方案。

---

## ✨ 核心特性

- 🧠 **高精度故障分类**  
  内置 **CNN‑LSTM** 分类器，能够从原始压力信号中自动提取时空特征，准确识别 **4种工况 × 2种负载**（共8类）：
  - 正常 (Normal)
  - 泵堵塞 (Pump Blockage)
  - 轻微泄漏 (Minor Leakage)
  - 严重泄漏 (Heavy Leakage)  
  - 分别对应 **0g** 与 **100g** 两种载荷

- 🎛️ **条件时序数据生成**  
  分类器判定类别后，自动调用该类专属的 **CTAM‑TimeGAN 生成器**（共8个独立模型），生成任意数量的**合成故障信号**，用于数据增强、离线分析或模型再训练。

- ⚡ **高性能异步Web服务**  
  - **Reactor/Proactor 双模式**支持高并发  
  - **基于时间轮的高效定时器**，自动管理空闲连接  
  - **线程池 + 数据库连接池**，充分利用多核CPU  
  - **epoll + 非阻塞I/O**，单机可稳定处理数千并发请求

- 🔐 **会话管理与权限控制**  
  基于 `session_id` Cookie 的用户登录验证，支持注册/登录，防止未授权访问诊断接口。

- 📦 **一键部署体验**  
  所有模型（`classifier.pt` + 8个 `generator_*.pt`）预训练完毕，只需放置于指定目录即可开箱即用。

---

## 🏗️ 系统架构

```
[用户] ──(CSV/JSON)──► [C++ Web Server] ──┬─► [会话验证] ──► [CNN‑LSTM 分类器] ──┬─► [类别 0~7]
                                            │                                      │
                                            └──────────────────────────────────────┘
                                                                                   │
                                                                      ┌────────────┴────────────┐
                                                                      ▼                         ▼
                                                              [生成器 0] ... [生成器 7]   返回 CSV 样本
                                                                      │                         │
                                                                      └────────────┬────────────┘
                                                                                   ▼
                                                                            [用户下载]
```

- **Web 服务器**：基于 C++ 重构的高性能服务器（原 TinyWebServer 增强版）
- **推理引擎**：LibTorch (PyTorch C++ 接口) 加载 TorchScript 模型
- **数据库**：MySQL 存储用户凭证
- **时序数据处理**：64 个时间步 × 3 个传感器通道（压力信号）

---

## 🛠️ 编译与运行

### 1. 环境依赖

| 组件         | 版本/要求                     |
| ------------ | ----------------------------- |
| 操作系统     | Linux (Ubuntu 20.04+ 推荐)    |
| C++ 编译器   | GCC 9+ 或 Clang 10+           |
| CMake        | 3.10+                         |
| LibTorch     | 2.0+ (CPU 或 CUDA 版本均可)    |
| MySQL        | 5.7+ (需创建 `tinywebserver` 数据库及 `user` 表) |
| nlohmann/json| 已包含在代码中（header‑only）   |

### 2. 下载 LibTorch

```bash
# CPU 版本示例
wget https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-2.0.1%2Bcpu.zip
unzip libtorch-cxx11-abi-shared-with-deps-2.0.1+cpu.zip -d .
```

将解压后的 `libtorch` 文件夹放在项目根目录（与 `CMakeLists.txt` 同级）。

### 3. 编译

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=`pwd`/../libtorch
make -j$(nproc)
```

### 4. 准备模型文件

将训练好的 `classifier.pt` 和 `generator_0.pt` ~ `generator_7.pt` 放入 `build/models/` 目录。  
> 注：模型导出为 TorchScript 格式的命令可参考论文代码仓库（将在附录提供）。

### 5. 配置 MySQL

```sql
CREATE DATABASE tinywebserver;
USE tinywebserver;
CREATE TABLE user (
    username VARCHAR(50) PRIMARY KEY,
    passwd   VARCHAR(50) NOT NULL
);
INSERT INTO user VALUES ('admin', 'admin123');  -- 测试账号
```

修改 `main.cpp` 中的数据库用户名/密码（默认 `root/root`）。

### 6. 运行服务器

```bash
cd build
./server -p 9006 -t 8 -s 8 -a 1
```

| 参数 | 含义                     | 默认值 |
|------|--------------------------|--------|
| `-p` | 监听端口                 | 9006   |
| `-t` | 线程池大小               | 8      |
| `-s` | 数据库连接池大小         | 8      |
| `-a` | 并发模型 (0=Proactor,1=Reactor) | 1 |

---

## 📡 API 使用说明

### POST `/diagnose` —— 故障诊断与数据生成

#### 请求格式：`Content-Type: application/json`

#### 场景一：用户上传 CSV 样本 → 服务器分类 → 生成同类样本

```json
{
    "csv_data": "0.123,0.456,0.789\n0.234,0.567,0.890\n...（共64行，每行3个数值）",
    "num_samples": 10
}
```

- `csv_data`：字符串，每行代表一个时间步的三个传感器读数，共 **64 行**。
- `num_samples`：整数，希望生成的新样本数量（每个样本也是 64×3 时序）。

**响应**：`Content-Type: text/csv`，直接下载 `result.csv`，内容为 `num_samples × 64` 行数据。

#### 场景二：直接生成样本（无需上传，用于快速数据增强）

```json
{
    "num_samples": 5
}
```

此时服务器使用默认生成器（类别0，即 Normal_0g）生成数据。响应同上为 CSV 文件。

#### 错误响应示例

```json
{
    "status": "error",
    "message": "CSV 中没有完整样本（至少需要64行）"
}
```

---

## 🧪 使用示例（curl）

```bash
# 1. 登录获取 session (通过网页 /log.html 或直接 POST 登录接口)
#    浏览器登录后自动设置 Cookie，以下为携带 Cookie 的调用

# 2. 上传 CSV 文件内容并生成 20 个新样本
curl -X POST http://localhost:9006/diagnose \
     -H "Content-Type: application/json" \
     -b "session_id=your_token_here" \
     -d '{
           "csv_data": "0.1,0.2,0.3\n0.4,0.5,0.6\n...（共64行）",
           "num_samples": 20
         }' \
     --output generated_samples.csv

# 3. 直接生成 10 个样本（无需上传）
curl -X POST http://localhost:9006/diagnose \
     -H "Content-Type: application/json" \
     -b "session_id=your_token_here" \
     -d '{"num_samples": 10}' \
     --output synthetic.csv
```

---

## 📄 论文引用

如果您在学术工作中使用本服务器或相关模型，请引用以下论文：

https://doi.org/10.1016/j.neucom.2026.132667


---

## 📜 许可证

本项目基于 **MIT License** 开源，欢迎学术研究和工业应用。  
模型权重文件及论文代码可于 [GitHub Repository] 获取（链接待公开）。

---

## 🙏 致谢

本工作受以下项目资助：  
- 青岛市自然科学基金 (24-4-4-zrjj-168-jch)  
- 山东省自然科学基金 (ZR2025MS860)  
- 黑龙江省自然科学基金 (YQ2024E036)  
- 中央高校基本科研业务费专项资金 (3072025ZX2604)  
- 泰山学者计划 (tsqn202312317)

---

## 📬 联系方式
**项目维护**：Wei Wang (wangwei@hrbeu.edu.cn)
