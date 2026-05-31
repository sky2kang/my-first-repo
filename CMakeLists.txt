cmake_minimum_required(VERSION 3.13)
project(zynq_adrv9025_p5g_oru C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# Build flavor
#   default            -> host / simulation (HAL_SIM)
#   -DORU_TARGET=ON    -> Zynq aarch64 target (HAL_TARGET, real drivers)
# -----------------------------------------------------------------------------
option(ORU_TARGET "Build for the Zynq target with real RF drivers" OFF)
option(ORU_BUILD_TESTS "Build unit tests" ON)

add_compile_options(-Wall -Wextra -Wshadow)

if(ORU_TARGET)
    add_compile_definitions(HAL_TARGET=1)
    message(STATUS "ORU build flavor: TARGET (HAL_TARGET)")
else()
    add_compile_definitions(HAL_SIM=1)
    message(STATUS "ORU build flavor: HOST/SIM (HAL_SIM)")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# Core library shared by the app and the tests.
add_library(oru_core STATIC
    src/common/log.c
    src/common/types.c
    src/common/config.c
    src/hal/adrv9025/adrv9025.c
    src/hal/adrv9025/jesd204.c
    src/fronthaul/ecpri.c
    src/fronthaul/oran_cplane.c
    src/fronthaul/oran_uplane.c
    src/fronthaul/bfp.c
    src/fronthaul/mulaw.c
    src/fronthaul/modcomp.c
    src/fronthaul/fh_sched.c
    src/fronthaul/datapath.c
    src/fronthaul/slot_loop.c
    src/fronthaul/cu_match.c
    src/fronthaul/comp_bench.c
    src/fronthaul/fronthaul.c
    src/splane/splane.c
    src/mplane/mplane.c
    src/mplane/yang.c
)
target_include_directories(oru_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/hal/adrv9025
)
# µ-law companding uses libm (log1p/pow).
target_link_libraries(oru_core PUBLIC m)

add_subdirectory(src/app)

if(ORU_BUILD_TESTS AND NOT ORU_TARGET)
    enable_testing()
    add_subdirectory(tests)
endif()
