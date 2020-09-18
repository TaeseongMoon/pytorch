#pragma once
#include <ATen/ATen.h>
#include <iostream>

namespace torchpy {

void init();
void finalize();

size_t load(const char* filename, bool hermetic=false);
at::Tensor forward(size_t model_id, at::Tensor input);
}
