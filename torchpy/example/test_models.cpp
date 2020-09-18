#include <gtest/gtest.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <torch/csrc/jit/python/pybind_utils.h>
#include <torch/python.h>
#include <torch/script.h>
#include <torch/torch.h>
#include <torchpy.h>

void compare_torchpy_jit(const char* model_filename, at::Tensor input) {
  // Test
  auto model_id = torchpy::load(model_filename);
  at::Tensor output = torchpy::forward(model_id, input);

  // Reference
  auto ref_model = torch::jit::load(model_filename);
  std::vector<torch::jit::IValue> ref_inputs;
  ref_inputs.push_back(torch::jit::IValue(input));
  at::Tensor ref_output = ref_model.forward(ref_inputs).toTensor();

  ASSERT_TRUE(ref_output.equal(output));
}

TEST(TorchpyTest, SimpleModel) {
  compare_torchpy_jit(
      "torchpy/example/simple.pt", torch::ones(at::IntArrayRef({10, 20})));
}

TEST(TorchpyTest, DISABLED_ResNet) {
  // Loader is broken
  compare_torchpy_jit(
      "torchpy/example/resnet.pt",
      torch::ones(at::IntArrayRef({1, 3, 224, 224})));
}
