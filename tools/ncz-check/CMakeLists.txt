# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ncz-check main.cpp)
target_link_libraries(ncz-check PRIVATE common core Threads::Threads)
target_include_directories(ncz-check PRIVATE ${CMAKE_SOURCE_DIR}/src)
create_target_directory_groups(ncz-check)
