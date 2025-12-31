if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_ARCHITECTURE "x86_64;arm64" CACHE STRING "" FORCE)
endif()

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")