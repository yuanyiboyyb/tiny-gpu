find_package(GTest REQUIRED)

include(GoogleTest)

add_executable(tinygpu-model-tests
    model_tests.cpp
)

target_compile_features(tinygpu-model-tests PRIVATE cxx_std_17)
target_link_libraries(tinygpu-model-tests
    PRIVATE
        tinygpu_verilated
        GTest::gtest_main
)

gtest_discover_tests(tinygpu-model-tests)
