cmake_minimum_required(VERSION 3.22)

project(ScoopAutoUpdater LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ScoopAutoUpdater WIN32
    src/winmain.cpp
    src/app.cpp
    src/ui/general.cpp
    src/ui/policy.cpp
    src/ui/settings.cpp
    res/app.rc
)

target_include_directories(ScoopAutoUpdater PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/res
)

if (MSVC)
    target_compile_options(ScoopAutoUpdater PRIVATE
        /EHsc
        /permissive-
        /utf-8
    )
else()
    target_link_options(ScoopAutoUpdater PRIVATE
        -municode
        -mwindows
    )
endif()

target_link_libraries(ScoopAutoUpdater PRIVATE
    ole32
    shell32
    gdi32
    comctl32
    uuid
)

set_target_properties(ScoopAutoUpdater PROPERTIES
  OUTPUT_NAME "Scoop Auto Updater"
)
