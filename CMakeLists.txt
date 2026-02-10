cmake_minimum_required(VERSION 3.14)
project(backtester LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

set(BACKTEST_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(BACKTEST_STRATEGIES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/strategies)

add_executable(backtester
  src/main.cpp
  src/data_source.cpp
  src/simulator.cpp
  src/backtester.cpp
  src/report.cpp
  strategies/example_sma_strategy.cpp
  strategies/ctm_strategy_simple.cpp
  strategies/orb_strategy.cpp
  strategies/one_point_oh_strategy.cpp
)
target_include_directories(backtester PRIVATE
  ${BACKTEST_INCLUDE_DIR}
  ${BACKTEST_STRATEGIES_DIR}
)

# Test runner (no external deps)
add_executable(test_runner tests/test_runner.cpp
  src/data_source.cpp
  src/simulator.cpp
)
target_include_directories(test_runner PRIVATE
  ${BACKTEST_INCLUDE_DIR}
)

enable_testing()
add_test(NAME test_runner COMMAND test_runner)

# Optional: copy sample data to build dir for default run
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
  add_custom_command(TARGET backtester POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/data
      $<TARGET_FILE_DIR:backtester>/data
    COMMENT "Copy data to build dir"
  )
endif()
