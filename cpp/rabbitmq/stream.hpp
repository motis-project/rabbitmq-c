#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "cista/reflection/comparable.h"

namespace amqp {

struct stream_options {
  CISTA_COMPARABLE()

  // if numeric_stream_offset_ is set, it is used
  // otherwise if stream_offset_ is not empty, it is used
  std::string stream_offset_;
  std::optional<std::int64_t> numeric_stream_offset_;
};

using get_stream_options_fn_t = std::function<stream_options()>;

}  // namespace amqp
