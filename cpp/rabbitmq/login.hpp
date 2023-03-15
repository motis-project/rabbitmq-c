#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "cista/reflection/comparable.h"

namespace amqp {

struct login {
  CISTA_COMPARABLE()

  bool empty() const { return *this == login{}; }
  bool valid() const {
    if (empty()) {
      return true;
    }
    return !host_.empty() && port_ != 0U && !vhost_.empty() && !user_.empty() &&
           !pw_.empty() &&
           ((cert_.empty() && key_.empty()) ||
            (!cert_.empty() && !key_.empty()));
  }

  std::string host_;
  unsigned port_{0U};
  std::string vhost_{"ribasis"}, queue_;
  std::string user_, pw_;
  std::string ca_{}, cert_{}, key_{};
  unsigned timeout_{10U};
  std::uint16_t prefetch_count_{0U};

  // for streams: if numeric_stream_offset_ is set, it is used
  // otherwise if stream_offset_ is not empty, it is used
  std::string stream_offset_;
  std::optional<std::int64_t> numeric_stream_offset_;
};

}  // namespace amqp
