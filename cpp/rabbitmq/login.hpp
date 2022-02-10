#pragma once

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
    return !host_.empty() && port_ != 0U && !exchange_.empty() &&
           !vhost_.empty() && !routing_key_.empty() && !user_.empty() &&
           !pw_.empty() &&
           ((cert_.empty() && key_.empty()) ||
            (!cert_.empty() && !key_.empty()));
  }

  std::string host_;
  unsigned port_{0U};
  std::string exchange_{"amq.direct"}, vhost_{"ribasis"}, routing_key_, queue_;
  std::string user_, pw_;
  std::string ca_{"cacert.pem"}, cert_{"cert.pem"}, key_{"key.pem"};
  unsigned timeout_{10U};
};

}  // namespace amqp