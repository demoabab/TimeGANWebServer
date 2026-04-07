#ifndef MODEL_INFERENCE_H
#define MODEL_INFERENCE_H

#include <torch/script.h>
#include <vector>
#include <string>

class ModelInference {
public:
    // 构造函数：加载模型
    ModelInference(const std::string& model_path);
    
    // 析构函数（可选，用于调试）
    ~ModelInference();

    // 禁用拷贝构造和拷贝赋值（关键！）
    ModelInference(const ModelInference&) = delete;
    ModelInference& operator=(const ModelInference&) = delete;

    // 允许移动构造和移动赋值（可选，但建议声明）
    ModelInference(ModelInference&& other) noexcept = default;
    ModelInference& operator=(ModelInference&& other) noexcept = default;

    // 推理接口：输入传感器数据（扁平数组），返回诊断结果
    std::vector<float> predict(const std::vector<float>& input_data, int seq_len, int Z_dim);
    
    // 获取输出特征维度（可选）
    int get_output_dim() const { return output_dim_; }

private:
    torch::jit::Module module_;   // 注意：这里改为 torch::jit::Module（去掉 script::，但你的写法也可以）
    torch::Device device_;        // 存储设备类型（CPU/GPU）
    int output_dim_ = 0;
};

#endif