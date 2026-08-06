add_library(tinygpu-compiler
    src/assembler.cpp
    src/ast.cpp
    src/codegen.cpp
    src/lexer.cpp
    src/parser.cpp
)

target_compile_features(tinygpu-compiler PUBLIC cxx_std_17)
target_include_directories(tinygpu-compiler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(tinygpu-frontend
    src/main.cpp
)

target_compile_features(tinygpu-frontend PRIVATE cxx_std_17)
target_link_libraries(tinygpu-frontend PRIVATE tinygpu-compiler)

add_executable(tinygpu-as
    src/assembler_main.cpp
)

target_compile_features(tinygpu-as PRIVATE cxx_std_17)
target_link_libraries(tinygpu-as PRIVATE tinygpu-compiler)

add_executable(tinygpu-cc
    src/compiler_main.cpp
)

target_compile_features(tinygpu-cc PRIVATE cxx_std_17)
target_link_libraries(tinygpu-cc PRIVATE tinygpu-compiler)

if(BUILD_TESTING)
    add_executable(tinygpu-compiler-tests
        tests/frontend_tests.cpp
    )

    target_compile_features(tinygpu-compiler-tests PRIVATE cxx_std_17)
    target_link_libraries(tinygpu-compiler-tests PRIVATE tinygpu-compiler)

    add_test(NAME tinygpu-compiler-tests COMMAND $<TARGET_FILE:tinygpu-compiler-tests>)

    add_executable(tinygpu-assembler-tests
        tests/assembler_tests.cpp
    )

    target_compile_features(tinygpu-assembler-tests PRIVATE cxx_std_17)
    target_link_libraries(tinygpu-assembler-tests PRIVATE tinygpu-compiler)

    add_test(NAME tinygpu-assembler-tests COMMAND $<TARGET_FILE:tinygpu-assembler-tests>)

    add_executable(tinygpu-codegen-tests
        tests/codegen_tests.cpp
    )

    target_compile_features(tinygpu-codegen-tests PRIVATE cxx_std_17)
    target_link_libraries(tinygpu-codegen-tests PRIVATE tinygpu-compiler)

    add_test(NAME tinygpu-codegen-tests COMMAND $<TARGET_FILE:tinygpu-codegen-tests>)

    add_test(
        NAME tinygpu-matadd-source
        COMMAND $<TARGET_FILE:tinygpu-cc>
                ${CMAKE_CURRENT_SOURCE_DIR}/../../sim_rtl/matadd.tu
                -o ${CMAKE_CURRENT_BINARY_DIR}/matadd.bin
    )
endif()
