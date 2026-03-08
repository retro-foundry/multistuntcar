cmake_minimum_required(VERSION 3.20)

project(stuntcarremake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STUNT_ENABLE_WEB_ASSETS "Preload game assets when targeting Emscripten" ON)
option(STUNT_FETCH_NATIVE_DEPS "Fetch SDL2/SDL2_ttf/glm from source for native builds" ON)

set(SCR_SOURCES
  src/3D_Engine.cpp
  src/Atlas.cpp
  src/Backdrop.cpp
  src/Car.cpp
  src/Car_Behaviour.cpp
  src/Opponent_Behaviour.cpp
  src/platform_sdl_gl.cpp
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

# Keep existing SDL/OpenGL code path enabled across all targets.
target_compile_definitions(stuntcarracer PRIVATE linux USE_SDL2 SDL_MAIN_HANDLED)

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
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data/Tracks@/data/Tracks"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data/Sounds@/data/Sounds"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data/Bitmap@/data/Bitmap"
      "SHELL:--embed-file ${CMAKE_CURRENT_SOURCE_DIR}/data/DejaVuSans-Bold.ttf@/data/DejaVuSans-Bold.ttf"
      "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/template.html"
    )
  endif()
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(stuntcarracer PRIVATE OpenGL::GL)

  if(STUNT_FETCH_NATIVE_DEPS)
    include(FetchContent)

    # Needed for some vendored third-party CMakeLists when using CMake 4.x.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # SDL2
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
      SDL2
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-2.30.2
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL2)

    # SDL2_ttf
    set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      SDL2_ttf
      GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
      GIT_TAG release-2.22.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL2_ttf)

    # glm headers
    FetchContent_Declare(
      glm
      GIT_REPOSITORY https://github.com/g-truc/glm.git
      GIT_TAG 0.9.9.8
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
    if(TARGET glm::glm)
      target_link_libraries(stuntcarracer PRIVATE glm::glm)
    else()
      target_include_directories(stuntcarracer PRIVATE ${glm_SOURCE_DIR})
    endif()

    if(TARGET SDL2::SDL2-static)
      target_link_libraries(stuntcarracer PRIVATE SDL2::SDL2-static)
    elseif(TARGET SDL2::SDL2)
      target_link_libraries(stuntcarracer PRIVATE SDL2::SDL2)
    else()
      message(FATAL_ERROR "SDL2 target not found after FetchContent.")
    endif()

    if(TARGET SDL2_ttf::SDL2_ttf)
      target_link_libraries(stuntcarracer PRIVATE SDL2_ttf::SDL2_ttf)
    elseif(TARGET SDL2_ttf::SDL2_ttf-static)
      target_link_libraries(stuntcarracer PRIVATE SDL2_ttf::SDL2_ttf-static)
    else()
      message(FATAL_ERROR "SDL2_ttf target not found after FetchContent.")
    endif()
  else()
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(glm REQUIRED)

    target_link_libraries(stuntcarracer PRIVATE
      SDL2::SDL2
      SDL2_ttf::SDL2_ttf
      glm::glm
    )
  endif()
endif()

set_target_properties(stuntcarracer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(NOT EMSCRIPTEN)
  add_custom_target(
    copy_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "${CMAKE_BINARY_DIR}/data"
    COMMENT "Syncing runtime data assets to build directory"
  )

  add_dependencies(stuntcarracer copy_runtime_data)

  add_custom_command(
    TARGET stuntcarracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:stuntcarracer>/data"
    COMMENT "Copying runtime data assets to output directory"
  )

  set_target_properties(stuntcarracer PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:stuntcarracer>"
  )
endif()
