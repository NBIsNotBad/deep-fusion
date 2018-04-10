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
  if (fuse_conv1x1_) {
    infer_conv0conv1();
  } else {
    infer_conv0();
  }
}

template <typename dst_data_t>
void op_conv<dst_data_t>::infer_conv0() {
  using namespace util;
  const auto &jcp = kernel_->jcp;
  assert(jcp.nb_oc % jcp.nb_oc_blocking == 0);
  auto bias_data = reinterpret_cast<const char *>(bia_data_);

#pragma omp parallel
  {
    int ithr = omp_get_thread_num(), nthr = omp_get_num_threads();
    int oc_chunks = jcp.nb_oc / jcp.nb_oc_blocking;
    int ic_chunks = jcp.nb_ic / jcp.nb_ic_blocking;

    int start{0}, end{0};
    int work_amount = jcp.bs * jcp.gp * oc_chunks * jcp.oh;
    balance211(work_amount, nthr, ithr, start, end);

    jit::jit_conv_call_s p = {0};
    auto ws_l = ws_ + ithr * ws_per_thread_;
    size_t src_h_stride = jcp.iw * jcp.ic;
    size_t dst_h_stride = jcp.ow * jcp.oc;
    size_t wht_h_stride = jcp.kw;
    size_t wht_ic_stride = jcp.kh * jcp.kw;

    int n{0}, g{0}, occ{0}, oh_s{0};
    if (jcp.loop_order == loop_cgn) {  // this is default
      nd_iterator_init(
          start, occ, oc_chunks, g, jcp.gp, n, jcp.bs, oh_s, jcp.oh);
    } else if (jcp.loop_order == loop_gnc) {
      nd_iterator_init(
          start, g, jcp.gp, n, jcp.bs, occ, oc_chunks, oh_s, jcp.oh);
    } else if (jcp.loop_order == loop_ngc) {
      nd_iterator_init(
          start, n, jcp.bs, g, jcp.gp, occ, oc_chunks, oh_s, jcp.oh);
    } else {
      assert(!"unsupported loop order");
    }

    while (start < end) {
      int ocb = occ * jcp.nb_oc_blocking;
      int g_oc = (g * jcp.nb_oc + ocb) * jcp.oc_block;
      int g_ic = g * jcp.nb_ic * jcp.oc_block;

      int work_rem = end - start;
      int ih_s = -jcp.t_pad + oh_s * jcp.sh;
      int oh_e = oh_s + work_rem > jcp.oh ? jcp.oh : oh_s + work_rem;

      auto bias_w = bias_data
                        ? bias_data + ((size_t)g_oc * jcp.oc / jcp.gp *
                                       jcp.typesize_conv0_bia)
                        : 0;
      auto dst_w = dst_data_ + (size_t)n * jcp.oc * jcp.oh * jcp.ow +
                   g_oc * jcp.oh * jcp.ow + oh_s * jcp.ow;
      auto src_w = src_data_ + (size_t)n * jcp.ic * jcp.ih * jcp.iw +
                   g_ic * jcp.ih * jcp.iw + ih_s * jcp.iw;
      auto wht_w =
          wei_data_ + (jcp.gp > 1 ? ((size_t)g * jcp.oc * jcp.ic * jcp.kh *
                                         jcp.kw / jcp.gp +
                                     ocb * jcp.ic * jcp.kh * jcp.kw / jcp.gp)
                                  : ((size_t)ocb * jcp.ic * jcp.kh * jcp.kw));
      auto scales =
          conv0_scales_data_ ? conv0_scales_data_ + g_oc : conv0_scales_data_;

      for (int icc = 0; icc < ic_chunks; ++icc) {
        auto src_c = src_w;
        auto dst_c = dst_w;
        auto ws_c = ws_l;
        int icb = icc * jcp.nb_ic_blocking;
        for (int oj = oh_s, ij = ih_s; oj < oh_e; ++oj, ij += jcp.sh) {
          int i_t_overflow = -std::min(0, ij);
          int i_b_overflow = std::max(jcp.ih, ij + jcp.kh) - jcp.ih;
          int kh_padding = std::max(0, jcp.kh - i_t_overflow - i_b_overflow);

          p.src = src_c + i_t_overflow * src_h_stride;
          p.wei = wht_w + i_t_overflow * wht_h_stride;
          p.bia = bias_w;
          p.acc_s32 = ws_c;
          p.channel = icb;
          p.kh_padding = kh_padding;
          p.scales = scales;
          p.dst = dst_c;
          kernel_->jit_ker_(&p);

          src_c += src_h_stride * jcp.sh;
          dst_c += dst_h_stride;
          ws_c += jcp.ow * jcp.oc_block * jcp.nb_oc_blocking;
        }
        src_w += jcp.ic_block * jcp.nb_ic_blocking;
        wht_w += wht_ic_stride * jcp.nb_ic_blocking;
      }

      if (jcp.loop_order == loop_cgn) {
        nd_iterator_jump(
            start, end, occ, oc_chunks, g, jcp.gp, n, jcp.bs, oh_s, jcp.oh);
      } else if (jcp.loop_order == loop_gnc) {
        nd_iterator_jump(
            start, end, g, jcp.gp, n, jcp.bs, occ, oc_chunks, oh_s, jcp.oh);
      } else if (jcp.loop_order == loop_ngc) {
        nd_iterator_jump(
            start, end, n, jcp.bs, g, jcp.gp, occ, oc_chunks, oh_s, jcp.oh);
      } else {
        assert(!"unsupported loop order");
      }
    }
  }
}

template <typename dst_data_t>
void op_conv<dst_data_t>::infer_conv0conv1() {
  using namespace util;
  const auto &jcp = kernel_->jcp;
  ;
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
