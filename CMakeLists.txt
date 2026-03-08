cmake_minimum_required(VERSION 3.16)

project(stuntcarremake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STUNT_ENABLE_WEB_ASSETS "Preload game assets when targeting Emscripten" ON)

set(SCR_SOURCES
  src/3D_Engine.cpp
  src/Atlas.cpp
  src/Backdrop.cpp
  src/Car.cpp
  src/Car_Behaviour.cpp
  src/dx_linux.cpp
  src/Opponent_Behaviour.cpp
  src/sound_sdl.cpp
  src/StuntCarRacer.cpp
  src/Track.cpp
  src/wavefunctions.cpp
  src/XBOXController.cpp
)

add_executable(stuntcarracer ${SCR_SOURCES})

target_include_directories(stuntcarracer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Force the SDL/OpenGL path across desktop and web.
target_compile_definitions(stuntcarracer PRIVATE linux USE_SDL2)

if(MSVC)
  target_compile_definitions(stuntcarracer PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(stuntcarracer PRIVATE
    -fpermissive
    -Wno-conversion-null
    -Wno-write-strings
  )
endif()

if(EMSCRIPTEN)
  target_link_options(stuntcarracer PRIVATE
    "SHELL:-s FULL_ES2=1"
    "SHELL:-s USE_SDL=2"
    "SHELL:-s USE_SDL_TTF=2"
    "SHELL:--emrun"
  )

  if(STUNT_ENABLE_WEB_ASSETS)
    target_link_options(stuntcarracer PRIVATE
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/Tracks@/Tracks"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/Sounds@/Sounds"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/Bitmap@/Bitmap"
      "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/DejaVuSans-Bold.ttf@/DejaVuSans-Bold.ttf"
      "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/template.html"
    )
  endif()
else()
  find_package(OpenGL REQUIRED)

  find_package(SDL2 CONFIG QUIET)
  find_package(SDL2_ttf CONFIG QUIET)

  if(SDL2_FOUND AND SDL2_ttf_FOUND)
    target_link_libraries(stuntcarracer PRIVATE
      OpenGL::GL
      SDL2::SDL2
      SDL2::SDL2main
      SDL2_ttf::SDL2_ttf
    )
  else()
    if(WIN32)
      message(FATAL_ERROR
        "SDL2/SDL2_ttf CMake packages were not found. "
        "Install SDL2 + SDL2_ttf development packages (for example via vcpkg) "
        "and re-run CMake.")
    else()
      find_package(PkgConfig REQUIRED)
      pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
      pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)

      target_link_libraries(stuntcarracer PRIVATE
        OpenGL::GL
        PkgConfig::SDL2
        PkgConfig::SDL2_TTF
        ${CMAKE_DL_LIBS}
      )
    endif()
  endif()
endif()

set_target_properties(stuntcarracer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
