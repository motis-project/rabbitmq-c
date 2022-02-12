cmake_minimum_required(VERSION 3.12)

include(cmake/pkg.cmake)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(RMQ_SOVERSION_CURRENT   9)
set(RMQ_SOVERSION_REVISION  1)
set(RMQ_SOVERSION_AGE       5)

include(VersionFunctions)
get_library_version(RMQ_VERSION)
compute_soversion(${RMQ_SOVERSION_CURRENT} ${RMQ_SOVERSION_REVISION} ${RMQ_SOVERSION_AGE} RMQ_SOVERSION)

project(rabbitmq-c
  VERSION ${RMQ_VERSION}
  DESCRIPTION "C RabbitMQ AMQP client library"
  LANGUAGES C)

configure_file(cmake/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if (WIN32)
  set(AMQP_SSL_SRCS ${AMQP_SSL_SRCS} librabbitmq/win32/threads.c)
  set(SSL_INCLUDE_DIRS librabbitmq/win32)
else()
  set(SSL_INCLUDE_DIRS librabbitmq/unix)
endif()

set(RMQ_SOURCES
  librabbitmq/amqp_api.c
  librabbitmq/amqp_connection.c
  librabbitmq/amqp_consumer.c
  librabbitmq/amqp_framing.c
  librabbitmq/amqp_mem.c
  librabbitmq/amqp_socket.c
  librabbitmq/amqp_table.c
  librabbitmq/amqp_tcp_socket.c
  librabbitmq/amqp_time.c
  librabbitmq/amqp_url.c
  librabbitmq/amqp_openssl.c
  librabbitmq/amqp_openssl_bio.c
  ${AMQP_SSL_SRCS}
)

add_library(rabbitmq STATIC)

include(GenerateExportHeader)
generate_export_header(rabbitmq
  BASE_NAME AMQP
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/rabbitmq-c/export.h
  STATIC_DEFINE AMQP_STATIC
  INCLUDE_GUARD_NAME RABBITMQ_C_EXPORT_H)

target_sources(rabbitmq PRIVATE ${RMQ_SOURCES})
target_include_directories(rabbitmq
  PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}/include
    include
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SSL_INCLUDE_DIRS}
)

target_compile_definitions(rabbitmq
  PUBLIC -DAMQP_STATIC
  PRIVATE -DHAVE_CONFIG_H
)

target_link_libraries(rabbitmq
  PRIVATE
    ssl
    crypto
    ${CMAKE_THREAD_LIBS_INIT}
  PUBLIC
    cista
    utl
)

if (MSVC)
  target_link_libraries(rabbitmq PRIVATE ws2_32)
endif()

if(UNIX AND NOT APPLE)
  target_link_libraries(rabbitmq PRIVATE rt)
endif()

add_library(rabbitmq-cpp INTERFACE)
target_link_libraries(rabbitmq-cpp INTERFACE rabbitmq)
target_include_directories(rabbitmq-cpp INTERFACE cpp)
target_compile_features(rabbitmq INTERFACE cxx_std_17)