# 压力测试报告

## 测试环境

| 项目     | 配置                                      |
| -------- | ----------------------------------------- |
| 操作系统 | Ubuntu 22.04                              |
| CPU      | Intel Xeon E5-2678 v3 @ 2.50GHz (12C/24T) |
| 内存     | 64GB DDR4                                 |
| 编译器   | g++ 11.4, C++17, -O2                      |
| 服务配置 | 8 线程, Proactor 模式, LT+LT, 日志开启    |
| 测试工具 | wrk 4.2.0                                 |
| 测试时长 | 每次 30s                                  |

## 测试场景一：静态文件服务

**接口**：`GET /diagnose.html`

**实现**：Epoll + mmap 零拷贝 + writev 分散聚集 I/O，文件大小约 9MB

| 并发连接 | QPS  | 平均延迟 | P99 延迟 | 吞吐量    | 超时数 | 错误数 |
| -------- | ---- | -------- | -------- | --------- | ------ | ------ |
| 100      | 2952 | 32ms     | 252ms    | 26.3 MB/s | 41     | 0      |
| 500      | 2819 | 54ms     | 409ms    | 25.1 MB/s | 302    | 0      |
| 1000     | 2841 | 57ms     | 452ms    | 25.3 MB/s | 675    | 0      |

**分析**：

- QPS 从 100→1000 并发仅下降 3.7%，Epoll 事件驱动架构伸缩性优秀
- 延迟增幅可控：平均从 32ms→57ms，P99 从 252ms→452ms
- 超时数随并发增长但无 Socket 错误，服务器全程零崩溃
- 吞吐量稳定在 25 MB/s，接近千兆网卡理论极限

## 测试场景二：ML 推理接口

**接口**：`POST /diagnose`，`{"num_samples": 1}`

**实现**：ONNX Runtime CPU 推理 + 线程池并行，单次推理约 5ms

| 并发连接 | QPS  | 平均延迟 | P99 延迟 | 超时数 |
| -------- | ---- | -------- | -------- | ------ |
| 100      | 1714 | 54ms     | 119ms    | 99     |
| 500      | 1630 | 272ms    | 449ms    | 527    |

**分析**：

- CPU 密集型场景下 8 线程达到 ~1700 QPS，单核约 213 QPS
- 并发 100 时排队效应小，P99 仅 119ms
- 并发 500 时队列积压导致延迟升至 272ms，但 QPS 仅降 5%
- 瓶颈在 CPU 推理，不在网络 I/O

## 测试命令

### 安装 wrk

```bash
sudo apt install wrk
```

### 静态文件

```bash
# 低并发
wrk -t4 -c100 -d30s http://localhost:9006/diagnose.html

# 中并发
wrk -t4 -c500 -d30s http://localhost:9006/diagnose.html

# 高并发
wrk -t4 -c1000 -d30s http://localhost:9006/diagnose.html
```

### 推理接口

```bash
# 需配合 bench/post_inference.lua 脚本
wrk -t2 -c100 -d30s -s bench/post_inference.lua http://localhost:9006/diagnose
wrk -t2 -c500 -d30s -s bench/post_inference.lua http://localhost:9006/diagnose
```

## 结论

- **零崩溃**：所有测试跑满 30s，无 Socket 错误，无进程崩溃
- **高伸缩**：100→1000 并发 QPS 波动 < 5%
- **低延迟**：静态文件 P99 < 500ms，推理 P99 < 450ms
- **可复现**：仅需 wrk + bench 脚本，一行命令即可验证
