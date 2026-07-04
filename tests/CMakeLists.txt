cmake_minimum_required(VERSION 3.25)

add_executable(d2rsave_tests
    test_bitreader.cpp
    test_save.cpp
    test_refdb.cpp
    test_character.cpp
    test_items.cpp
    test_stash.cpp
    test_sunder.cpp
)
target_link_libraries(d2rsave_tests PRIVATE
    d2rsave_core
    Catch2::Catch2WithMain
)
target_compile_definitions(d2rsave_tests PRIVATE
    D2R_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/D2R_Saves"
    D2R_TEST_REFERENCE_DB="${D2R_REFERENCE_DB}"
)
add_dependencies(d2rsave_tests reference_db)

include(Catch)
catch_discover_tests(d2rsave_tests)
