find_package(verilator REQUIRED HINTS $ENV{VERILATOR_ROOT} /usr/share/verilator)

set(TINYGPU_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(TINYGPU_RTL_DIR "${TINYGPU_REPO_ROOT}/rtl")

set(TINYGPU_RTL_SOURCES
    "${TINYGPU_RTL_DIR}/branch_stack_pkg.sv"
    "${TINYGPU_RTL_DIR}/pipeline/branch_stack.sv"
    "${TINYGPU_RTL_DIR}/gpu.sv"
    "${TINYGPU_RTL_DIR}/core.sv"
    "${TINYGPU_RTL_DIR}/dcr.sv"
    "${TINYGPU_RTL_DIR}/dispatch.sv"
    "${TINYGPU_RTL_DIR}/memory_controller.sv"
    "${TINYGPU_RTL_DIR}/pipeline/register_file.sv"
    "${TINYGPU_RTL_DIR}/pipeline/if_stage.sv"
    "${TINYGPU_RTL_DIR}/pipeline/id_stage.sv"
    "${TINYGPU_RTL_DIR}/pipeline/ex_stage.sv"
    "${TINYGPU_RTL_DIR}/pipeline/mem_stage.sv"
    "${TINYGPU_RTL_DIR}/pipeline/wb_stage.sv"
)

add_library(tinygpu_verilated STATIC
    src/verilated_gpu.cpp
)

target_compile_features(tinygpu_verilated PUBLIC cxx_std_17)

target_include_directories(tinygpu_verilated
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
)

verilate(tinygpu_verilated
    TOP_MODULE gpu
    PREFIX Vgpu
    SOURCES ${TINYGPU_RTL_SOURCES}
    VERILATOR_ARGS
        -Wall
        -Wno-fatal
        --trace
        --trace-structs
)

set(TINYGPU_MODEL_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/model_main.cpp")
if(EXISTS "${TINYGPU_MODEL_MAIN}")
    add_executable(tinygpu-model
        "${TINYGPU_MODEL_MAIN}"
    )

    target_compile_features(tinygpu-model PRIVATE cxx_std_17)
    target_link_libraries(tinygpu-model PRIVATE tinygpu_verilated)
endif()
