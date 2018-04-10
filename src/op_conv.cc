/*******************************************************************************
 * Copyright 2018 Tensor Tang. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*******************************************************************************/
#include "op_conv.h"
#include "util_jitinfer.h"

namespace jitinfer {

template <typename dst_data_t>
void op_conv<dst_data_t>::infer() {
  using namespace util;
  const auto &jcp = kernel_->jcp;
  if (fuse_conv1x1_) {
    ;
  } else {
    ;
  }
}

template <typename dst_data_t>
bool op_conv<dst_data_t>::init_conf(jit::jit_conv_conf_t &conf,
                                    const std::unique_ptr<memory> &src,
                                    const std::unique_ptr<memory> &wei,
                                    const std::unique_ptr<memory> &bia,
                                    int ngroups,
                                    std::array<int, 2> sz_stride,
                                    std::array<int, 2> sz_padding,
                                    std::unique_ptr<memory> &dst,
                                    std::vector<float> conv0_scales,
                                    std::vector<float> conv1_scales,
                                    const std::unique_ptr<memory> &wei1x1,
                                    const std::unique_ptr<memory> &bia1x1,
                                    bool conv0_relu,
                                    bool conv1_relu,
                                    round_mode conv0_round_mode,
                                    round_mode conv1_round_mode) {
  using namespace util;
  // check data type
  if (dst->data_type() != type2dtype<dst_data_t>::dtype) {
    info("Dst data type do not match");
    return false;
  }

  // check image size and channels
  constexpr int C = 1, H = 2, W = 3;  // channel, height, width
  auto src_dims = src->std_dims();    // nchw
  auto wei_dims = wei->std_dims();    // oihw
  auto dst_dims = dst->std_dims();    // nchw
  for (size_t i = 0; i < 2; ++i) {
    if (dst_dims[i + 2] !=
        conv_output_size(
            src_dims[i + 2], wei_dims[i + 2], sz_stride[i], sz_padding[i])) {
      info("Output image size do not match: %d", i);
      return false;
    }
  }
  if (src_dims[0] != dst_dims[0]) {
    info("Batch size do not equal");
    return false;
  }
  if (src_dims[C] != wei_dims[C]) {
    info("Input channel do not match");
    return false;
  }
  if (wei1x1 == nullptr) {
    check_eq(fuse_conv1x1_, false);
    if (dst_dims[C] != wei_dims[0]) {
      info("Output channel do not match");
      return false;
    }
    if (bia != nullptr && bia->std_dims()[0] != wei_dims[0]) {
      info("Bias channel do not match");
      return false;
    }
    if (!one_of(conv0_scales.size(), 1UL, size_t(dst_dims[C]))) {
      return false;
    }
  } else {
    check_eq(fuse_conv1x1_, true);
    auto wei1x1_dims = wei1x1->std_dims();  // oihw
    if (wei1x1_dims[C] != wei_dims[0]) {
      info("Conv0 output channel do not match");
      return false;
    }
    if (dst_dims[C] != wei1x1_dims[0]) {
      info("Conv1x1 output channel do not match");
      return false;
    }
    if (wei1x1_dims[H] != 1 || wei1x1_dims[W] != 1) {
      info("Fused conv must be 1x1 kernel");
      return false;
    }
    if (bia1x1 != nullptr && bia1x1->std_dims()[0] != dst_dims[C]) {
      info("Bias channel do not match");
      return false;
    }
    if (!all_true(one_of(conv0_scales.size(), 1UL, size_t(wei1x1_dims[1])),
                  one_of(conv1_scales.size(), 1UL, size_t(wei1x1_dims[0])))) {
      return false;
    }
  }

  check_eq(ngroups, 1);  // only verified gp==1 yet
  return jit::jit_conv_kernel::init_conf(conf,
                                         src,
                                         wei,
                                         bia,
                                         ngroups,
                                         sz_stride,
                                         sz_padding,
                                         dst,
                                         conv0_scales,
                                         conv1_scales,
                                         wei1x1,
                                         bia1x1,
                                         conv0_relu,
                                         conv1_relu,
                                         conv0_round_mode,
                                         conv1_round_mode);
}

template class op_conv<f32>;
template class op_conv<s32>;
template class op_conv<s8>;
template class op_conv<u8>;
}
