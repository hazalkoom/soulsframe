include(FetchContent)

# Allow legacy cmake_minimum_required() bounds in third-party deps under CMake 4.x.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
  GIT_SHALLOW    TRUE
)

set(XXHASH_BUNDLED_MODE ON CACHE BOOL "" FORCE)
set(XXHASH_BUILD_XXHSUM OFF CACHE BOOL "" FORCE)
FetchContent_Declare(xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG        v0.8.4
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  build/cmake
)

if(SF_HOST_ONLY)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(tomlplusplus xxhash doctest)
else()
  FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(tomlplusplus xxhash minhook)
endif()
