set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TC x86_64-w64-mingw32)

find_program(MINGW_CC NAMES ${TC}-gcc-posix ${TC}-gcc REQUIRED)
find_program(MINGW_CXX NAMES ${TC}-g++-posix ${TC}-g++ REQUIRED)
find_program(MINGW_RC NAMES ${TC}-windres REQUIRED)

set(CMAKE_C_COMPILER   "${MINGW_CC}")
set(CMAKE_CXX_COMPILER "${MINGW_CXX}")
set(CMAKE_RC_COMPILER  "${MINGW_RC}")

if(EXISTS "/usr/${TC}/sys-root/mingw")
  set(CMAKE_FIND_ROOT_PATH "/usr/${TC}/sys-root/mingw")
elseif(EXISTS "/usr/${TC}")
  set(CMAKE_FIND_ROOT_PATH "/usr/${TC}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
