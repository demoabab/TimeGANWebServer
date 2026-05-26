-- wrk 脚本：POST JSON 请求（探测环境基准）
wrk.method = "POST"
wrk.body = '{"num_samples": 1}'
wrk.headers["Content-Type"] = "application/json"
