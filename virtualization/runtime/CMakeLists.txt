add_library(tinygpu_runtime STATIC
    src/runtime.cpp
)

target_compile_features(tinygpu_runtime PUBLIC cxx_std_17)
target_include_directories(tinygpu_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../driver
)

add_executable(tinygpu-runtime-matadd
    examples/matadd.cpp
)
target_compile_features(tinygpu-runtime-matadd PRIVATE cxx_std_17)
target_link_libraries(tinygpu-runtime-matadd PRIVATE tinygpu_runtime)

if(BUILD_TESTING)
    add_executable(tinygpu-runtime-tests
        tests/runtime_tests.cpp
    )
    target_compile_features(tinygpu-runtime-tests PRIVATE cxx_std_17)
    target_link_libraries(tinygpu-runtime-tests PRIVATE tinygpu_runtime)
    add_test(
        NAME tinygpu-runtime-tests
        COMMAND $<TARGET_FILE:tinygpu-runtime-tests>
                ${CMAKE_CURRENT_SOURCE_DIR}/../../sim_rtl/matadd.bin
    )
endif()
