# SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
#
# Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
#
# airy-thinkdual 用户态构建入口
#
# [SC] 契约头只从 kernel/include/uapi 读取，且用 -idirafter 排在系统目录
# 之后，使 linux/types.h 仍取自系统 UAPI，避免内核树 types.h 与 libc 的
# loff_t/blkcnt_t 冲突。源码树与 RPM 构建根下该相对路径都成立。

cmake_minimum_required(VERSION 3.20)
project(airy_thinkdual VERSION 0.1.1 LANGUAGES C)

include(GNUInstallDirs)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(AIRY_UAPI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/include/uapi"
    CACHE PATH "[SC] 共享契约头目录")
if(NOT EXISTS "${AIRY_UAPI_DIR}/linux/airymax/cognition_types.h")
  message(FATAL_ERROR "找不到 [SC] 契约头: ${AIRY_UAPI_DIR}")
endif()

add_compile_options(-Wall -Wextra -Werror -fstack-protector-strong)
add_compile_options(-idirafter "${AIRY_UAPI_DIR}")

add_library(td STATIC td_score.c td_policy.c td_state.c)
target_include_directories(td PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(airy-thinkdual td_main.c)
target_link_libraries(airy-thinkdual PRIVATE td)
install(TARGETS airy-thinkdual RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

include(CTest)
if(BUILD_TESTING)
  add_executable(td_test tests/td_test.c)
  target_link_libraries(td_test PRIVATE td)
  target_compile_definitions(td_test PRIVATE
    TD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME thinkdual COMMAND td_test)
endif()
