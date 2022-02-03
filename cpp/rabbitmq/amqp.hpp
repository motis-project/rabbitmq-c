#pragma once

#include <chrono>

#include "rabbitmq-c/amqp.h"
#include "rabbitmq-c/ssl_socket.h"

#include "utl/verify.h"

namespace amqp {

struct ssl_connection {
  ssl_connection(std::string const& host, uint16_t const port,
                 std::string const& cert, std::string const& key,
                 std::chrono::seconds const& timeout) {
    utl::verify(s_ != nullptr, "AMQP SSL socket creation failed");
    amqp_ssl_socket_set_verify_peer(s_, 0);
    amqp_ssl_socket_set_verify_hostname(s_, 0);

    auto const set_key_result =
        amqp_ssl_socket_set_key(s_, cert.c_str(), key.c_str());
    utl::verify(set_key_result == AMQP_STATUS_OK, "AMQP SSL bad cert/key");

    auto const tval = timeval{.tv_sec = timeout.count(), .tv_usec = 0U};
    auto const open_result =
        amqp_socket_open_noblock(s_, host.c_str(), port, &tval);
    utl::verify(open_result == AMQP_STATUS_OK, "AMQP open socket failed");
  }

  ~ssl_connection() {
    amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn_);
  }

  amqp_connection_state_t conn_{amqp_new_connection()};
  amqp_socket_t* s_{amqp_ssl_socket_new(conn_)};
};

}  // namespace amqp