add_library(gridex-domain STATIC
    UseCases/Connection/TestConnection.cpp
    UseCases/Connection/ConnectDatabase.cpp
    UseCases/Connection/DeleteConnection.cpp
)

target_include_directories(gridex-domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(gridex-domain
    PUBLIC  gridex-core
    PRIVATE gridex-data
)
