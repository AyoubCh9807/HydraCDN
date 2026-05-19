set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.20)
project(UltraBackend)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Conan Packages
find_package(Drogon REQUIRED)
find_package(glaze REQUIRED)
find_package(spdlog REQUIRED)
find_package(libsodium REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(Tracy REQUIRED)
find_package(absl REQUIRED)

# 2. Arch System Packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED vips-cpp)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)
find_package(uWebSockets REQUIRED)

# NOTE: Since you have /usr/lib/libtbb.so, you don't strictly need find_package(TBB),
# but it's cleaner to just link it by name below.

add_executable(backend main.cpp)

# 3. Manual Header Paths (Thrust & CCCL)
target_include_directories(backend PRIVATE 
    ${CMAKE_SOURCE_DIR}/../Repos/thrust
    ${CMAKE_SOURCE_DIR}/../Repos/cccl/libcudacxx/include
    ${VIPS_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

# 4. Link Everything
target_link_libraries(backend PRIVATE 
    Drogon::Drogon              
    glaze::glaze
    spdlog::spdlog
    libsodium::libsodium
    tomlplusplus::tomlplusplus
    Tracy::TracyClient         
    uwebsockets::uwebsockets
    absl::base
    absl::strings              
    ${VIPS_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    tbb                        # <--- ADDED TBB HERE
)
