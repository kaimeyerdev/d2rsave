cmake_minimum_required(VERSION 3.25)

add_executable(rename_d2r_legacy rename_d2r.c)
set_target_properties(rename_d2r_legacy PROPERTIES
    OUTPUT_NAME rename_d2r
    C_STANDARD 11
)
target_compile_options(rename_d2r_legacy PRIVATE -Wall -Wextra)
