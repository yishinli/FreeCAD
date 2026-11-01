# SPDX-License-Identifier: LGPL-2.1-or-later

add_executable(Inspection_tests_run
        Interference.cpp
        PartsList.cpp
)

target_include_directories(Inspection_tests_run PUBLIC
        ${CMAKE_BINARY_DIR}
)
