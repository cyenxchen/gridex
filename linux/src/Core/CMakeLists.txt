add_library(gridex-core INTERFACE)

target_include_directories(gridex-core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(gridex-core INTERFACE cxx_std_20)
