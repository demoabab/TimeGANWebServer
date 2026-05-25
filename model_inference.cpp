#include "model_inference.h"
#include <iostream>
#include <cstring>

Ort::Env ModelInference::s_env(ORT_LOGGING_LEVEL_WARNING, "TimeGANServer");

ModelInference::ModelInference(const std::string& model_path) {
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

    try {
        m_session.reset(new Ort::Session(s_env, model_path.c_str(), opts));
    } catch (const Ort::Exception& e) {
        std::cerr << "Error loading model " << model_path << ": "
                  << e.what() << std::endl;
        throw;
    }

    m_memory_info.reset(new Ort::MemoryInfo(
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)));

    std::cout << "Model loaded to CPU" << std::endl;
}

ModelInference::~ModelInference() {
    std::cout << "Destructing ModelInference at " << this << std::endl;
}

std::vector<float> ModelInference::predict(const std::vector<float>& input_data,
                                           int seq_len, int Z_dim) {
    std::vector<int64_t> input_shape = {1, seq_len, Z_dim};
    size_t input_count = 1 * seq_len * Z_dim;

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        *m_memory_info,
        const_cast<float*>(input_data.data()),
        input_count,
        input_shape.data(),
        input_shape.size());

    Ort::AllocatorWithDefaultOptions allocator;
    auto input_name_alloc = m_session->GetInputNameAllocated(0, allocator);
    auto output_name_alloc = m_session->GetOutputNameAllocated(0, allocator);

    const char* input_names[] = {input_name_alloc.get()};
    const char* output_names[] = {output_name_alloc.get()};

    Ort::RunOptions run_opts;
    auto outputs = m_session->Run(
        run_opts,
        input_names, &input_tensor, 1,
        output_names, 1);

    auto& output_tensor = outputs.front();
    auto info = output_tensor.GetTensorTypeAndShapeInfo();
    size_t output_count = info.GetElementCount();
    output_dim_ = info.GetShape().back();

    float* data_ptr = output_tensor.template GetTensorMutableData<float>();
    std::vector<float> result(data_ptr, data_ptr + output_count);

    return result;
}
