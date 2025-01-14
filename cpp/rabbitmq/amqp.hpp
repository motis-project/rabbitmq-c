#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

#include "boost/asio/deadline_timer.hpp"
#include "boost/asio/io_service.hpp"

#include "rabbitmq-c/amqp.h"
#include "rabbitmq-c/ssl_socket.h"

#include "utl/verify.h"

#include "rabbitmq/login.hpp"
#include "rabbitmq/stream.hpp"

namespace amqp {

struct msg {
  uint64_t delivery_tag_;
  std::optional<std::string> content_type_;
  std::string exchange_, routing_key_, content_;
  std::optional<std::int64_t> stream_offset_;
};

template <typename Context, typename... Args>
void throw_if_error(int x, Context&& context, Args&&... args) {
  if (x < 0) {
    throw utl::fail(
        "{}: {}",
        fmt::format(fmt::runtime(context), std::forward<Args>(args)...),
        amqp_error_string2(x));
  }
}

template <typename Context, typename... Args>
void throw_if_error(amqp_rpc_reply_t x, Context&& context, Args&&... args) {
  switch (x.reply_type) {
    case AMQP_RESPONSE_NORMAL:
      return;

    case AMQP_RESPONSE_NONE:
      throw utl::fail(
          "{}: missing RPC reply type!",
          fmt::format(fmt::runtime(context), std::forward<Args>(args)...));

    case AMQP_RESPONSE_LIBRARY_EXCEPTION:
      throw utl::fail(
          "{}: {}",
          fmt::format(fmt::runtime(context), std::forward<Args>(args)...),
          amqp_error_string2(x.library_error));

    case AMQP_RESPONSE_SERVER_EXCEPTION:
      switch (x.reply.id) {
        case AMQP_CONNECTION_CLOSE_METHOD: {
          auto const* m =
              static_cast<amqp_connection_close_t*>(x.reply.decoded);
          throw utl::fail(
              "{}: server connection error {}, message: {}",
              fmt::format(fmt::runtime(context), std::forward<Args>(args)...),
              m->reply_code,
              std::string_view{static_cast<char const*>(m->reply_text.bytes),
                               m->reply_text.len});
        }

        case AMQP_CHANNEL_CLOSE_METHOD: {
          auto const* m = static_cast<amqp_channel_close_t*>(x.reply.decoded);
          throw utl::fail(
              "{}: server channel error {}, message: {}\n",
              fmt::format(fmt::runtime(context), std::forward<Args>(args)...),
              m->reply_code,
              std::string_view{static_cast<char const*>(m->reply_text.bytes),
                               m->reply_text.len});
        }

        default:
          throw utl::fail(
              "{}: unknown server error, method id 0x{0:x}\n",
              fmt::format(fmt::runtime(context), std::forward<Args>(args)...),
              x.reply.id);
      }
  }
}

inline amqp_bytes_t_ amqp_bytes_from_str(std::string const& s) {
  return {s.length(), const_cast<char*>(&s[0])};
}

struct con {
  static constexpr auto const kChannel = 1;

  explicit con(login const* login, std::function<void(std::string const&)>* log)
      : login_{login}, log_{log} {
    utl::verify(conn_ != nullptr, "RabbitMQ: connection creation failed");
    utl::verify(s_ != nullptr, "RabbitMQ: socket creation failed");
  }

  ~con() { close(); }

  con(con const&) = delete;

  con(con&& o) noexcept
      : login_{o.login_},
        log_{o.log_},
        conn_{std::exchange(o.conn_, conn_)},
        s_{std::exchange(o.s_, s_)},
        channel_open_{std::exchange(o.channel_open_, channel_open_)} {}

  con& operator=(con const&) = delete;

  con& operator=(con&& o) noexcept {
    login_ = o.login_;
    log_ = o.log_;
    std::swap(conn_, o.conn_);
    std::swap(s_, o.s_);
    std::swap(channel_open_, o.channel_open_);
    return *this;
  }

  void connect() {
    utl::verify(s_ != nullptr, "RabbitMQ SSL socket creation failed");
    amqp_ssl_socket_set_verify_peer(s_, 0);
    amqp_ssl_socket_set_verify_hostname(s_, 0);

    if (!login_->ca_.empty()) {
      throw_if_error(amqp_ssl_socket_set_cacert(s_, login_->ca_.c_str()),
                     "Rabbit MQ setting ca={}", login_->ca_);
    }
    if (!login_->cert_.empty() && !login_->key_.empty()) {
      throw_if_error(amqp_ssl_socket_set_key(s_, login_->cert_.c_str(),
                                             login_->key_.c_str()),
                     "RabbitMQ setting cert={} key={}", login_->cert_,
                     login_->key_);
    }

    auto tval = timeval{};
    tval.tv_sec = static_cast<long>(login_->timeout_);
    throw_if_error(amqp_socket_open_noblock(s_, login_->host_.c_str(),
                                            login_->port_, &tval),
                   "RabbitMQ socket open");
  }

  void do_login() const {
    throw_if_error(amqp_login(conn_, login_->vhost_.c_str(), 0, 131072,
                              login_->heartbeat_, AMQP_SASL_METHOD_PLAIN,
                              login_->user_.c_str(), login_->pw_.c_str()),
                   "RabbitMQ logging in");
  }

  void open_queue(stream_options const& stream_opt) {
    // Open channel.
    log_->operator()("opening channel");
    amqp_channel_open(conn_, kChannel);
    throw_if_error(amqp_get_rpc_reply(conn_), "RabbitMQ: Opening channel");
    channel_open_ = true;

    // Set QoS.
    if (login_->prefetch_count_ != 0) {
      log_->operator()("basic qos");
      amqp_basic_qos(conn_, kChannel, 0, login_->prefetch_count_, false);
      throw_if_error(amqp_get_rpc_reply(conn_), "RabbitMQ: QoS");
    }

    // Consume.
    log_->operator()("basic consume");
    auto const queue_name_bytes = amqp_bytes_from_str(login_->queue_);

    // For RabbitMQ Streams: Add x-stream-offset consumer argument
    auto args = amqp_empty_table;
    auto arg = amqp_table_entry_t{};
    if (stream_opt.numeric_stream_offset_) {
      arg.key = amqp_cstring_bytes("x-stream-offset");
      arg.value.kind = AMQP_FIELD_KIND_I64;
      arg.value.value.i64 = stream_opt.numeric_stream_offset_.value();

      args.num_entries = 1;
      args.entries = &arg;
    } else if (!stream_opt.stream_offset_.empty()) {
      arg.key = amqp_cstring_bytes("x-stream-offset");
      arg.value.kind = AMQP_FIELD_KIND_UTF8;
      arg.value.value.bytes =
          amqp_cstring_bytes(stream_opt.stream_offset_.c_str());

      args.num_entries = 1;
      args.entries = &arg;
    }

    amqp_basic_consume(conn_, kChannel, queue_name_bytes, amqp_empty_bytes, 0,
                       0, 0, args);
    log_->operator()("basic consume get rpc reply");
    throw_if_error(amqp_get_rpc_reply(conn_), "Consuming");
  }

  void receive(std::function<void(msg const)> const& cb) const {
    amqp_maybe_release_buffers(conn_);

    auto envelope = amqp_envelope_t{};
    auto const res = amqp_consume_message(conn_, &envelope, NULL, 0);

    if (res.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
      throw utl::fail("RabbitMQ library exception: {}",
                      amqp_error_string2(res.library_error));
    }

    utl::verify(res.reply_type == AMQP_RESPONSE_NORMAL,
                "unexpected message type {}", static_cast<int>(res.reply_type));

    auto const& headers = envelope.message.properties.headers;
    auto stream_offset = std::optional<std::int64_t>{};
    for (auto i = 0; i < headers.num_entries; ++i) {
      auto const& header = headers.entries[i];
      auto const key = std::string_view{
          static_cast<char const*>(header.key.bytes), header.key.len};
      if (key == "x-stream-offset") {
        if (header.value.kind == AMQP_FIELD_KIND_I64) {
          stream_offset = header.value.value.i64;
        }
      }
    }

    cb(msg{envelope.delivery_tag,
           (envelope.message.properties._flags &
            AMQP_BASIC_CONTENT_TYPE_FLAG) != 0U
               ? std::make_optional<std::string>(
                     static_cast<char*>(
                         envelope.message.properties.content_type.bytes),
                     envelope.message.properties.content_type.len)
               : std::nullopt,
           std::string{static_cast<char*>(envelope.exchange.bytes),
                       envelope.exchange.len},
           std::string{static_cast<char*>(envelope.routing_key.bytes),
                       envelope.routing_key.len},
           std::string{static_cast<char*>(envelope.message.body.bytes),
                       envelope.message.body.len},
           stream_offset});

    auto const ack_result =
        amqp_basic_ack(conn_, kChannel, envelope.delivery_tag, 0);
    utl::verify(ack_result == 0, "RabbitMQ: could not send ack");

    amqp_destroy_envelope(&envelope);
  }

  void close() const {
    if (channel_open_) {
      amqp_channel_close(conn_, kChannel, AMQP_REPLY_SUCCESS);
    }
    amqp_connection_close(conn_, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn_);
  }

  login const* login_{nullptr};
  std::function<void(std::string const&)>* log_{nullptr};
  amqp_connection_state_t conn_{amqp_new_connection()};
  amqp_socket_t* s_{amqp_ssl_socket_new(conn_)};
  bool channel_open_{false};
};

struct ssl_connection {
  explicit ssl_connection(login const* login,
                          std::function<void(std::string const&)> log,
                          get_stream_options_fn_t get_stream_options = nullptr)
      : login_{login},
        log_{std::move(log)},
        get_stream_options_{std::move(get_stream_options)} {}

  void run(std::function<void(msg const&)>&& cb) {
    t_ = std::thread{[&, cb = std::move(cb)]() {
      log_("starting loop");
      while (!stopped_) {
        try {
          log_("connecting");
          con_.connect();

          log_("login");
          con_.do_login();

          log_("opening queue");
          auto const stream_opt = get_stream_options_ != nullptr
                                      ? get_stream_options_()
                                      : stream_options{};
          con_.open_queue(stream_opt);

          log_("receive loop");
          while (!stopped_) {
            con_.receive(cb);
          }
        } catch (std::exception const& e) {
          log_(fmt::format("error: {}, reconnecting ...", e.what()));
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
          con_ = con{login_, &log_};
        }
      }
    }};
  }

  void stop() {
    stopped_ = true;
    t_.join();
  }

  std::atomic_bool stopped_{false};
  std::thread t_;
  login const* login_{nullptr};
  std::function<void(std::string const&)> log_;
  get_stream_options_fn_t get_stream_options_{nullptr};
  con con_{login_, &log_};
};

}  // namespace amqp
