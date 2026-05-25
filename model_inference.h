#ifndef MODEL_INFERENCE_H
#define MODEL_INFERENCE_H

#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>

class ModelInference {
public:
    ModelInference(const std::string& model_path);
    ~ModelInference();

    ModelInference(const ModelInference&) = delete;
    ModelInference& operator=(const ModelInference&) = delete;
    ModelInference(ModelInference&& other) noexcept = default;
    ModelInference& operator=(ModelInference&& other) noexcept = default;

    std::vector<float> predict(const std::vector<float>& input_data, int seq_len, int Z_dim);
    int get_output_dim() const { return output_dim_; }

private:
    static Ort::Env s_env;           // 全局共享
    std::unique_ptr<Ort::Session> m_session;
    std::unique_ptr<Ort::MemoryInfo> m_memory_info;
    int output_dim_ = 0;
};

#endif
