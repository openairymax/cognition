# SPDX-License-Identifier: AGPL-3.0-or-later OR Apache-2.0
#
# Copyright (c) 2025-2026 SPHARX Ltd. All Rights Reserved.
#
# airy-cogflow 用户态构建入口
#
# [SC] 契约头只从 kernel/include/uapi 读取，且用 -idirafter 排在系统目录
# 之后，使 linux/types.h 仍取自系统 UAPI。复杂度评估与模式判定复用
# thinkdual 的 td 静态库，本模块不另立阈值与策略解析。

cmake_minimum_required(VERSION 3.20)
project(airy_cogflow VERSION 0.1.1 LANGUAGES C)

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

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../thinkdual"
                 "${CMAKE_CURRENT_BINARY_DIR}/thinkdual")

add_library(cf STATIC cf_plan.c cf_event.c cf_loop.c)
target_include_directories(cf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cf PUBLIC td)

add_executable(airy-cogflow cf_main.c)
target_link_libraries(airy-cogflow PRIVATE cf)
install(TARGETS airy-cogflow RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})

include(CTest)
if(BUILD_TESTING)
  add_executable(cf_test tests/cf_test.c)
  target_link_libraries(cf_test PRIVATE cf)
  target_compile_definitions(cf_test PRIVATE
    CF_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME cogflow COMMAND cf_test)
endif()
