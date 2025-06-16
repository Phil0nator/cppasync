add_executable(ex1 ex1.cpp)
add_executable(ex2 ex2.cpp)



# Ensure examples use the same C++ standard
set_target_properties(ex1 ex2 PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Add debug symbols for all build types
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-g)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/Zi)
    add_link_options(/DEBUG)
endif()

target_include_directories(ex1 PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(ex2 PRIVATE ${CMAKE_SOURCE_DIR}/include)
