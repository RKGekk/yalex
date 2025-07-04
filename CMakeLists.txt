cmake_minimum_required(VERSION 3.25)

project(project64 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(SRC_DIR "src")
set(MYTHON_SOURCES "${SRC_DIR}/lexer.h" "${SRC_DIR}/lexer.cpp" "${SRC_DIR}/runtime.h" "${SRC_DIR}/runtime.cpp" "${SRC_DIR}/statement.h" "${SRC_DIR}/statement.cpp" "${SRC_DIR}/parse.h" "${SRC_DIR}/parse.cpp")
set(APP_SOURCES "${SRC_DIR}/main.cpp" "${SRC_DIR}/lexer_test_open.cpp" "${SRC_DIR}/statement_test.cpp" "${SRC_DIR}/parse_test.cpp" "${SRC_DIR}/runtime_tests.cpp" "${SRC_DIR}/test_runner_p.h" ${MYTHON_SOURCES})

add_executable(project64 ${APP_SOURCES})