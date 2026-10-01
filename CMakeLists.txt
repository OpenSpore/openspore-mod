cmake_minimum_required(VERSION 3.16)
project(openspore-mod CXX)

# The ModAPI mod itself (OpenSporeMP.vcxproj) is built with Visual Studio only, because
# the Spore ModAPI SDK is MSVC-only. This CMake file builds the portable parts:
#   * osmp_net     - wire protocol, UDP session, interpolation, entity registry
#   * osmp_tests   - unit tests (run on Linux/CI or Windows)
#   * osmp_netlab  - command-line transport tester (host/join without the game)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(osmp_net STATIC
    src/net/Protocol.cpp
    src/net/UdpSocket.cpp
    src/net/Session.cpp
    src/net/Interpolation.cpp
    src/game/Replication.cpp)
target_include_directories(osmp_net PUBLIC src)
if(WIN32)
    target_link_libraries(osmp_net PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(osmp_net PRIVATE /W4)
else()
    target_compile_options(osmp_net PRIVATE -Wall -Wextra)
endif()

option(OSMP_BUILD_TESTS "Build unit tests" ON)
if(OSMP_BUILD_TESTS)
    enable_testing()
    add_executable(osmp_tests tests/test_main.cpp tests/test_protocol.cpp tests/test_interp.cpp tests/test_session.cpp tests/test_replication.cpp)
    target_link_libraries(osmp_tests osmp_net)
    add_test(NAME osmp_tests COMMAND osmp_tests)
endif()

add_executable(osmp_netlab tools/netlab.cpp)
target_link_libraries(osmp_netlab osmp_net)

# Lab DLL (raw addresses, no ModAPI) + injector: Windows only, MSVC or mingw. 32-bit required.
if(WIN32)
    add_library(osmp_lab SHARED src/lab/LabMain.cpp src/lab/RawEngine.cpp src/game/GhostLab.cpp)
    target_link_libraries(osmp_lab osmp_net)
    set_target_properties(osmp_lab PROPERTIES PREFIX "")
    add_executable(osmp_inject src/lab/inject.cpp)
    set_target_properties(osmp_inject PROPERTIES OUTPUT_NAME inject)
    if(MINGW)
        target_link_options(osmp_lab PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_options(osmp_inject PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_options(osmp_netlab PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
else()
    # GhostLab is portable; keep it compiled on Linux too so refactors cannot silently break it.
    target_sources(osmp_net PRIVATE src/game/GhostLab.cpp)
endif()
