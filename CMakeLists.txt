cmake_minimum_required(VERSION 3.25)

project(project64 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(SRC_DIR "src")
set(MYTHON_SOURCES "${SRC_DIR}/lexer.h" "${SRC_DIR}/lexer.cpp")
set(APP_SOURCES "${SRC_DIR}/main.cpp" "${SRC_DIR}/lexer_test_open.cpp" "${SRC_DIR}/test_runner_p.h" ${MYTHON_SOURCES})

add_executable(project64 ${APP_SOURCES})