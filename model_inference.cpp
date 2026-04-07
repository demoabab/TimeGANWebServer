#include "model_inference.h"
#include <iostream>

ModelInference::ModelInference(const std::string& model_path) : device_(torch::kCPU) {
    fflush(stdout);
    try {
        module_ = torch::jit::load(model_path);
        module_.eval();
        module_.to(device_);   // 强制移到 CPU
    } catch (const c10::Error& e) {
        std::cerr << "Error loading model: " << e.what() << std::endl;
        throw;
    }
    std::cout << "Model loaded to CPU" << std::endl;
}

ModelInference::~ModelInference() {
    std::cout << "Destructing ModelInference at " << this << std::endl;
}

std::vector<float> ModelInference::predict(const std::vector<float>& input_data, int seq_len, int Z_dim) {
    fflush(stderr);
    torch::Tensor input_tensor = torch::from_blob(
        const_cast<float*>(input_data.data()),
        {1, seq_len, Z_dim},
        torch::kFloat32
    ).clone();
    input_tensor = input_tensor.to(device_);

    torch::NoGradGuard no_grad;
    std::vector<torch::jit::IValue> inputs;
    inputs.push_back(input_tensor);
    torch::Tensor output = module_.forward(inputs).toTensor();

    output = output.cpu();

    float* data_ptr = output.data_ptr<float>();
    std::vector<float> result(data_ptr, data_ptr + output.numel());
    output_dim_ = output.size(-1);
    return result;
}