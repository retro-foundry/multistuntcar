cmake_minimum_required(VERSION 3.16)

project(stuntcarremake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STUNT_USE_SDL2 "Use SDL2 on non-Windows builds" ON)

set(SCR_CORE_SOURCES
  src/3D_Engine.cpp
  src/Atlas.cpp
  src/Backdrop.cpp
  src/Car.cpp
  src/Car_Behaviour.cpp
  src/Opponent_Behaviour.cpp
  src/StuntCarRacer.cpp
  src/Track.cpp
  src/wavefunctions.cpp
  src/XBOXController.cpp
)

if(WIN32)
  enable_language(RC)

  add_executable(stuntcarracer WIN32
    ${SCR_CORE_SOURCES}
    Common/dxstdafx.cpp
    Common/DXUT.cpp
    Common/DXUTenum.cpp
    Common/DXUTmisc.cpp
    Common/DXUTRes.cpp
    src/StuntCarRacer.rc
  )

  target_include_directories(stuntcarracer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
  )

  # Optional legacy DirectX SDK (June 2010) path for d3dx9/dxerr.
  set(DXSDK_DIR "" CACHE PATH "Path to legacy DirectX SDK (June 2010)")
  if(DXSDK_DIR)
    target_include_directories(stuntcarracer PRIVATE "${DXSDK_DIR}/Include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      target_link_directories(stuntcarracer PRIVATE "${DXSDK_DIR}/Lib/x64")
    else()
      target_link_directories(stuntcarracer PRIVATE "${DXSDK_DIR}/Lib/x86")
    endif()
  endif()

  target_link_libraries(stuntcarracer PRIVATE
    comctl32
    d3d9
    d3dx9
    dsound
    dxerr
    dxguid
    winmm
    xinput
  )
else()
  add_executable(stuntcarracer
    ${SCR_CORE_SOURCES}
    src/dx_linux.cpp
    src/sound_openal.cpp
  )

  find_package(PkgConfig REQUIRED)
  find_package(OpenGL REQUIRED)

  if(STUNT_USE_SDL2)
    pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(SDL_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
    target_compile_definitions(stuntcarracer PRIVATE USE_SDL2)
  else()
    pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl)
    pkg_check_modules(SDL_TTF REQUIRED IMPORTED_TARGET SDL_ttf)
  endif()

  pkg_check_modules(OPENAL REQUIRED IMPORTED_TARGET openal)

  target_compile_definitions(stuntcarracer PRIVATE linux)
  target_include_directories(stuntcarracer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
  )
  target_link_libraries(stuntcarracer PRIVATE
    OpenGL::GL
    OpenGL::GLU
    PkgConfig::OPENAL
    PkgConfig::SDL
    PkgConfig::SDL_TTF
    dl
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(stuntcarracer PRIVATE
      -fpermissive
      -Wno-conversion-null
      -Wno-write-strings
    )
  endif()
endif()

set_target_properties(stuntcarracer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
