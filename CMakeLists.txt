cmake_minimum_required(VERSION 3.16)

project(edge_gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)

# Platform-independent core: CRC, protocol codec, streaming frame parser,
# logger. Compiled into both the gateway and the host unit tests, so the
# parser is regression-tested by `ctest` before it ever sees real hardware.
add_library(gateway_core
    src/Crc16.cpp
    src/Protocol.cpp
    src/FrameParser.cpp
    src/Logger.cpp
)
target_include_directories(gateway_core PUBLIC include)
target_link_libraries(gateway_core PUBLIC Threads::Threads)

add_executable(edge_gateway
    src/main.cpp
    src/SerialPort.cpp
    src/DataProcessor.cpp
    src/TcpServer.cpp
)
target_include_directories(edge_gateway PRIVATE include)
target_link_libraries(edge_gateway PRIVATE gateway_core)

enable_testing()

add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE gateway_core)
add_test(NAME test_protocol COMMAND test_protocol)

add_executable(test_parser tests/test_parser.cpp)
target_link_libraries(test_parser PRIVATE gateway_core)
add_test(NAME test_parser COMMAND test_parser)
