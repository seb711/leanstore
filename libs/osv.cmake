message(STATUS "=== Starting osv.cmake ===")

if(NOT DEFINED ENV{OSV_BASE})
    message(FATAL_ERROR "OSV_BASE environment variable not set")
endif()

set(OSV_BASE $ENV{OSV_BASE})
message(STATUS "OSV_BASE: ${OSV_BASE}")

# Determine architecture
if(NOT DEFINED ARCH)
    if(DEFINED ENV{ARCH})
        set(ARCH $ENV{ARCH})
    else()
        set(ARCH "x64")
    endif()
endif()
message(STATUS "ARCH: ${ARCH}")

# Check if target already exists
if(TARGET osv)
    message(STATUS "OSV target already exists, skipping")
    return()
endif()

# Create OSV interface library
add_library(osv INTERFACE)

# Add CLEAN include paths (NO -I or -isystem flags!)
target_include_directories(osv INTERFACE
    ${OSV_BASE}/arch/${ARCH}
    ${OSV_BASE}
    ${OSV_BASE}/include
    ${OSV_BASE}/arch/common
)

# Add system includes separately (for C++ standard library)
target_include_directories(osv SYSTEM INTERFACE
    /nix/store/68ndh04pl2hhhizsarvzwa9cnlp7zj3d-gcc-14.3.0/include/c++/14.3.0
    /nix/store/68ndh04pl2hhhizsarvzwa9cnlp7zj3d-gcc-14.3.0/include/c++/14.3.0/x86_64-unknown-linux-gnu
    /nix/store/68ndh04pl2hhhizsarvzwa9cnlp7zj3d-gcc-14.3.0/include/c++/14.3.0/backward
)

# Find Boost
find_package(Boost COMPONENTS system filesystem)
if(Boost_FOUND)
    message(STATUS "✓ Boost found: ${Boost_VERSION}")
    target_link_libraries(osv INTERFACE Boost::system Boost::filesystem)
else()
    message(WARNING "Boost not found")
endif()

# Architecture-specific definitions
if(ARCH STREQUAL "aarch64")
    target_compile_definitions(osv INTERFACE AARCH64_PORT_STUB)
endif()

message(STATUS "=== OSV configuration complete ===")

# Debug output
get_target_property(OSV_INCLUDES osv INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "Final OSV includes: ${OSV_INCLUDES}")