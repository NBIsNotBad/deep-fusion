/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <array>
#include <memory>
#include <vector>

namespace deepfusion {

typedef float f32;
typedef int32_t s32;
typedef int8_t s8;
typedef uint8_t u8;

// Disable the copy and assignment operator for a class.
#ifndef DISABLE_COPY_AND_ASSIGN
#define DISABLE_COPY_AND_ASSIGN(classname)          \
private:                                            \
  classname(const classname &) = delete;            \
  classname(const classname &&) = delete;           \
  classname &operator=(const classname &) = delete; \
  classname &operator=(const classname &&) = delete
#endif

struct opdesc {
  int tmp;
};

enum round_mode {
  nearest = 0,
  down,
};

struct memory {
public:
  enum format {
    format_undef = 0,
    x,
    nchw,
    oihw = nchw,
    nhwc,
    OIhw4i16o4i,
    gOIhw4i16o4i,
  };
  typedef std::vector<int> dims;
  typedef std::array<int, 2> pair_dims;
  typedef std::array<int, 4> nchw_dims;

  enum dtype {
    undef = 0,
    f32,
    s32,
    s8,
    u8,
  };

  // TODO: enable more format init
  // explicit memory(const dims& dm, const format fmt, const dtype dt, int
  // alignment = 64);
  explicit memory(const nchw_dims &dm,
                  const format fmt,
                  const dtype dt,
                  int alignment = 4096);
  ~memory();
  size_t size();
  size_t buffer_size();
  dims actual_dims() { return dims_; }
  nchw_dims std_dims() { return std_dims_; }  // nchw or oihw
  dtype data_type() { return dt_; }
  format dim_format() { return fmt_; }
  void *data() { return data_; }

private:
  void allocate_buffer(int alignment);
  void *data_;
  dims dims_;
  nchw_dims std_dims_;  // nchw or oihw
  format fmt_;
  dtype dt_;

  DISABLE_COPY_AND_ASSIGN(memory);
};

class op {
public:
  explicit op() {}
  virtual void submit();

protected:
  virtual void infer() = 0;
  virtual const char *name() = 0;
  DISABLE_COPY_AND_ASSIGN(op);
};

std::unique_ptr<op> concat(const std::vector<std::unique_ptr<memory>> &srcs,
                           std::unique_ptr<memory> &dst,
                           bool post_relu = false);

// only conv
std::unique_ptr<op> conv(const std::unique_ptr<memory> &src,
                         const std::unique_ptr<memory> &wei,
                         const std::unique_ptr<memory> &bia,
                         std::array<int, 2> sz_stride,
                         std::array<int, 2> sz_padding,
                         std::unique_ptr<memory> &dst,
                         bool conv0_relu = false,
                         std::vector<float> conv0_scales = {1.f},
                         round_mode conv0_round_mode = round_mode::nearest);

// conv and fuse conv1x1_relu
std::unique_ptr<op> conv(const std::unique_ptr<memory> &src,
                         const std::unique_ptr<memory> &wei,
                         const std::unique_ptr<memory> &bia,
                         std::array<int, 2> sz_stride,
                         std::array<int, 2> sz_padding,
                         const std::unique_ptr<memory> &wei1x1,
                         const std::unique_ptr<memory> &bia1x1,
                         std::unique_ptr<memory> &dst,
                         bool conv0_relu = false,
                         std::vector<float> conv0_scales = {1.f},
                         round_mode conv0_round_mode = round_mode::nearest,
                         bool conv1_relu = false,
                         std::vector<float> conv1_scales = {1.f},
                         round_mode conv1_round_mode = round_mode::nearest);
}
