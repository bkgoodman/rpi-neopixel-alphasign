cmake_minimum_required(VERSION 3.10) # Or a more recent version

project(neosign)

# Add subdirectories for your libraries (git submodules)
add_subdirectory(rpi_ws281x)

# Find source files for your main program
set(SOURCES "neosign.c")

# Add executable
add_executable(neosign ${SOURCES})

add_custom_target(build_rpi_ws281x
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_CURRENT_BINARY_DIR}/rpi_ws281x
    DEPENDS rpi_ws281x # Ensure CMake is run in the subdirectory first
)
add_dependencies(neosign build_rpi_ws281x)

# Link against the libraries built in the subdirectories
target_link_libraries(neosign PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/rpi_ws281x/libws2811.a"
    m
)

# Specify include directories for the libraries
target_include_directories(neosign PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rpi_ws281x
)

# Optional: Install target (if you want to install your program)
# install(TARGETS my_program DESTINATION bin)
