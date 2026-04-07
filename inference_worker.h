#include "model_inference.h"
#include "redis_client.h"
#include <nlohmann/json.hpp>
#include <thread>

using json = nlohmann::json;

void start_inference_worker(ModelInference* classifier, ModelInference* generator) {
    RedisClient redis;
    std::cout << "[Worker] Inference Worker started, waiting for tasks..." << std::endl;

    while (true) {
        // 1. 从 Redis 队列中阻塞式获取任务
        std::string task_str = redis.pop_task("inference_tasks");
        if (task_str.empty()) continue;

        try {
            auto task_json = json::parse(task_str);
            std::string task_id = task_json["task_id"];
            std::vector<float> data = task_json["data"];

            // 2. 执行模型推理 (CPU/GPU 密集型操作在此处运行)
            std::vector<float> prob = classifier->predict(data, 64, 3);
            int label = std::max_element(prob.begin(), prob.end()) - prob.begin();
            
            // 3. 调用生成模型
            std::vector<float> gen_data = generator->predict(data, 64, 3);

            // 4. 将结果写回 Redis
            json result_json;
            result_json["status"] = "completed";
            result_json["label"] = label;
            result_json["gen_data"] = gen_data;
            redis.set_result(task_id, result_json.dump());
            
            std::cout << "[Worker] Task " << task_id << " processed, label: " << label << std::endl;
        } catch (...) {
            // 异常处理：设置错误状态
        }
    }
}