-- wrk 脚本：POST 推理请求（真实负载）
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

-- 随机 num_samples 1-3，模拟真实用户行为
counter = 0
request = function()
    counter = counter + 1
    return wrk.format(nil, nil, nil, '{"num_samples": 1}')
end
