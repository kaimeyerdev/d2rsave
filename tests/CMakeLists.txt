cmake_minimum_required(VERSION 3.25)

add_executable(d2rsave_tests
    test_bitreader.cpp
    test_save.cpp
    test_refdb.cpp
    test_character.cpp
    test_items.cpp
    test_stash.cpp
    test_sunder.cpp
    test_vendor.cpp
    test_excel_table.cpp
    test_sql_emitter.cpp
)
target_link_libraries(d2rsave_tests PRIVATE
    d2rsave_core
    Catch2::Catch2WithMain
)
if(TARGET d2rsave_refdbgen)
    target_link_libraries(d2rsave_tests PRIVATE d2rsave_refdbgen)
endif()
target_compile_definitions(d2rsave_tests PRIVATE
    D2R_TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/live-snapshot"
    D2R_TEST_VENDOR_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/vendor"
    D2R_TEST_REFERENCE_DB="${D2R_REFERENCE_DB}"
)
add_dependencies(d2rsave_tests reference_db)

include(Catch)
catch_discover_tests(d2rsave_tests)
