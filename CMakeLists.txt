cmake_minimum_required(VERSION 3.10)

project(tinysql)

# Find all .c and .h files in the src/ directory
file(GLOB SOURCES "src/*.c" "src/*.h")

# Add executable with all found source files
add_executable(tinysql ${SOURCES})