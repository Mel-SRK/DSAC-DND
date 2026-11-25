cmake_minimum_required(VERSION 3.21)
project(DSAC_DND LANGUAGES C)

add_executable(dnd
    src/main.c
    src/dungeon.c
    src/combat.c
    src/loot.c
    src/party.c
)
set_target_properties(dnd PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
if(MSVC)
    target_compile_options(dnd PRIVATE /W4 /utf-8)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dnd PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME gameplay
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_game.py"
                "$<TARGET_FILE:dnd>"
        )
    endif()
endif()
