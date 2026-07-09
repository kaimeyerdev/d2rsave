cmake_minimum_required(VERSION 3.25)
project(d2rsave
    VERSION 0.1.0
    DESCRIPTION "D2R save-game parser and rename/seed tool"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(D2R_USE_VCPKG "Enable features that need vcpkg deps (sqlite3, catch2, fmt)" OFF)
option(D2R_BUILD_LEGACY "Build the legacy rename_d2r C tool for regression testing" ON)
option(D2R_BUILD_TESTS "Build unit tests (needs D2R_USE_VCPKG=ON)" ${D2R_USE_VCPKG})

# Runewords in the chronicle report are a work-in-progress: we can count how
# many the account has chronicled but can't map each chronicle entry back to
# its runeword name without a D2R item-runes.json string-table extract that
# we don't currently ship. The feature is compiled out by default; opt in
# with -DD2R_ENABLE_RUNEWORDS_WIP=ON when experimenting.
option(D2R_ENABLE_RUNEWORDS_WIP
       "Include runewords in the chronicle report (WIP; name mapping missing)"
       OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Optional platform features (probe for inotify to enable --watch on Linux).
# ---------------------------------------------------------------------------
include(CheckIncludeFileCXX)
check_include_file_cxx(sys/inotify.h D2R_HAVE_INOTIFY)

# ---------------------------------------------------------------------------
# Core library + CLI (no external deps).
# ---------------------------------------------------------------------------
add_library(d2rsave_core STATIC
    src/save.cpp
    src/bitreader.cpp
    src/character_parser.cpp
)
target_include_directories(d2rsave_core PUBLIC include)
target_compile_features(d2rsave_core PUBLIC cxx_std_20)

if(D2R_ENABLE_RUNEWORDS_WIP)
    target_compile_definitions(d2rsave_core PUBLIC D2R_ENABLE_RUNEWORDS_WIP=1)
endif()

if(D2R_HAVE_INOTIFY)
    target_sources(d2rsave_core PRIVATE src/watcher_inotify.cpp)
    target_compile_definitions(d2rsave_core PUBLIC D2R_HAVE_INOTIFY=1)
endif()

add_executable(d2rsave src/main.cpp)
target_link_libraries(d2rsave PRIVATE d2rsave_core)

# ---------------------------------------------------------------------------
# Legacy C tool (kept for regression comparison).
# ---------------------------------------------------------------------------
if(D2R_BUILD_LEGACY)
    add_subdirectory(legacy)
endif()

# ---------------------------------------------------------------------------
# Optional deps (vcpkg): sqlite3, fmt, catch2. Later phases.
# ---------------------------------------------------------------------------
if(D2R_USE_VCPKG)
    find_package(unofficial-sqlite3 CONFIG QUIET)
    find_package(SQLite3 QUIET)
    find_package(fmt CONFIG QUIET)

    if(TARGET unofficial::sqlite3::sqlite3)
        set(D2R_SQLITE_TARGET unofficial::sqlite3::sqlite3)
    elseif(TARGET SQLite::SQLite3)
        set(D2R_SQLITE_TARGET SQLite::SQLite3)
    endif()

    if(D2R_SQLITE_TARGET AND TARGET fmt::fmt)
        message(STATUS "d2rsave: sqlite3 + fmt available; enabling reference DB + query features.")
        set(D2R_FULL_STACK ON)

        # Reference DB pipeline: cat data/sql/*.sql | sqlite3 <db>.
        find_program(SQLITE3_BIN sqlite3 REQUIRED)
        file(GLOB D2R_SQL_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/data/sql/*.sql")
        set(D2R_REFERENCE_DB "${CMAKE_BINARY_DIR}/reference.sqlite")
        add_custom_command(
            OUTPUT "${D2R_REFERENCE_DB}"
            COMMAND ${CMAKE_COMMAND} -E rm -f "${D2R_REFERENCE_DB}"
            COMMAND sh -c "cat \"${CMAKE_SOURCE_DIR}/data/sql/\"*.sql | \"${SQLITE3_BIN}\" \"${D2R_REFERENCE_DB}\""
            DEPENDS ${D2R_SQL_FILES}
            COMMENT "Building D2R reference database"
            VERBATIM
        )
        add_custom_target(reference_db ALL DEPENDS "${D2R_REFERENCE_DB}")

        # RefDb + related sources are compiled into the core library and linked
        # against sqlite3.
        target_sources(d2rsave_core PRIVATE
            src/refdb.cpp
            src/item_parser.cpp
            src/shared_stash_parser.cpp
            src/terror_zones.cpp
        )
        target_link_libraries(d2rsave_core PUBLIC ${D2R_SQLITE_TARGET} fmt::fmt)
        target_compile_definitions(d2rsave_core PUBLIC D2R_HAVE_SQLITE=1)
        target_compile_definitions(d2rsave PRIVATE
            D2R_REFERENCE_DB_DEFAULT="${D2R_REFERENCE_DB}"
        )
        add_dependencies(d2rsave reference_db)

        # Standalone forecasting CLI, useful for validating the RNG
        # anchor + hash against online oracles. Kept separate from the
        # main d2rsave binary so it can be invoked without touching the
        # interactive dashboard.
        add_executable(d2r_tz_forecast src/tz_forecast_main.cpp)
        target_link_libraries(d2r_tz_forecast PRIVATE d2rsave_core)
        target_compile_definitions(d2r_tz_forecast PRIVATE
            D2R_REFERENCE_DB_DEFAULT="${D2R_REFERENCE_DB}"
        )
        add_dependencies(d2r_tz_forecast reference_db)
    else()
        message(WARNING "d2rsave: D2R_USE_VCPKG=ON but sqlite3/fmt not found. Run ./bootstrap-vcpkg.sh.")
    endif()

    # -----------------------------------------------------------------------
    # Optional TUI dashboard (FTXUI). Cross-platform, MIT. Depends on
    # D2R_HAVE_SQLITE for the reference DB; also uses the inotify watcher
    # when available for live refresh.
    # -----------------------------------------------------------------------
    if(D2R_FULL_STACK)
        find_package(ftxui CONFIG QUIET)
        if(TARGET ftxui::screen AND TARGET ftxui::dom AND TARGET ftxui::component)
            message(STATUS "d2rsave: ftxui available; enabling 'dashboard' subcommand.")
            target_sources(d2rsave PRIVATE
                src/dashboard_model.cpp
                src/dashboard_ftxui.cpp
                src/dashboard_config.cpp
            )
            target_link_libraries(d2rsave PRIVATE
                ftxui::screen ftxui::dom ftxui::component)
            if(TARGET nlohmann_json::nlohmann_json)
                target_link_libraries(d2rsave PRIVATE nlohmann_json::nlohmann_json)
            else()
                message(WARNING "d2rsave: nlohmann_json missing; dashboard config persistence disabled.")
            endif()
            target_compile_definitions(d2rsave PRIVATE D2R_HAVE_DASHBOARD=1)
        else()
            message(STATUS "d2rsave: ftxui missing; 'dashboard' subcommand disabled.")
        endif()
    endif()

    if(D2R_BUILD_TESTS)
        find_package(Catch2 3 CONFIG QUIET)
    endif()

    # -----------------------------------------------------------------------
    # Reference-DB regenerator (CascLib + nlohmann-json).
    # Declared BEFORE add_subdirectory(tests) so the test binary can pick up
    # d2rsave_refdbgen when it exists.
    # -----------------------------------------------------------------------
    find_package(CascLib CONFIG QUIET)
    find_package(nlohmann_json CONFIG QUIET)

    if(TARGET CascLib::casc OR TARGET CascLib::casc_static)
        if(TARGET CascLib::casc_static)
            set(D2R_CASC_TARGET CascLib::casc_static)
        else()
            set(D2R_CASC_TARGET CascLib::casc)
        endif()
    endif()

    if(D2R_CASC_TARGET AND TARGET nlohmann_json::nlohmann_json)
        message(STATUS "d2rsave: CascLib + nlohmann-json available; enabling d2r_refdb_gen.")

        # Excel / SQL emitter core: no CASC or JSON dep for the pure-parse
        # sources, so unit tests can exercise them without a D2R install.
        add_library(d2rsave_refdbgen STATIC
            src/excel_table.cpp
            src/sql_emitter.cpp
            src/casc_storage.cpp
            src/string_table.cpp
        )
        target_include_directories(d2rsave_refdbgen PUBLIC include)
        target_compile_features(d2rsave_refdbgen PUBLIC cxx_std_20)
        target_link_libraries(d2rsave_refdbgen PUBLIC
            ${D2R_CASC_TARGET}
            nlohmann_json::nlohmann_json
        )

        add_executable(d2r_refdb_gen src/refdb_gen_main.cpp)
        target_link_libraries(d2r_refdb_gen PRIVATE d2rsave_refdbgen)

        # Minimal read-only CASC dump helper. Used for exploratory work
        # (e.g. locating desecratedzones.json) without touching the fixed
        # kCatalogue in d2r_refdb_gen.
        add_executable(d2r_casc_dump src/casc_dump_main.cpp)
        target_link_libraries(d2r_casc_dump PRIVATE d2rsave_refdbgen)

        # Optional maintainer target: regenerate committed data/sql/ against a
        # user-supplied D2R install. Guarded on -DD2R_D2R_INSTALL=<path>; the
        # default build never touches the install path.
        if(D2R_D2R_INSTALL)
            add_custom_target(regen-refdb-sql
                COMMAND d2r_refdb_gen
                        --d2r-install "${D2R_D2R_INSTALL}"
                        --out "${CMAKE_SOURCE_DIR}/data/sql"
                DEPENDS d2r_refdb_gen
                COMMENT "Regenerating data/sql/*.sql from ${D2R_D2R_INSTALL}"
                VERBATIM
            )
        endif()
    else()
        message(STATUS "d2rsave: CascLib or nlohmann-json missing; d2r_refdb_gen disabled.")
    endif()

    if(D2R_BUILD_TESTS)
        if(Catch2_FOUND)
            enable_testing()
            add_subdirectory(tests)
        else()
            message(WARNING "d2rsave: Catch2 not found; tests disabled.")
        endif()
    endif()
endif()
