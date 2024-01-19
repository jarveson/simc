##################
# 'engine' library
##################

# define source files
include(../source_files/cmake_engine.txt)

# Define engine library
if(SC_WASM)
    set(OPTS "")
    set(WASM_OPTS
        "-s TOTAL_MEMORY=256MB"
        "-s WASM_MEM_MAX=1024MB"
        "-s ALLOW_MEMORY_GROWTH=1"
        "-s MODULARIZE=1"
        "-s EXPORT_NAME=Simc"
        "-s DISABLE_EXCEPTION_CATCHING=0"
        "-s EXPORTED_RUNTIME_METHODS='[\"UTF8ToString\", \"FS\", \"stringToNewUTF8\", \"_free\"]'"
        "--js-library ${CMAKE_CURRENT_SOURCE_DIR}/wasm/glue.js"
    )
    if(CMAKE_BUILD_TYPE MATCHES Release)
        list(APPEND OPTS -O3 -flto)
        list(APPEND WASM_OPTS -O3 -flto)
    endif()
    add_executable(engine ${source_files} wasm/sc_wasm.cpp)
    target_compile_options(engine PRIVATE ${OPTS})
    target_compile_definitions(engine PUBLIC SC_WASM)
    target_link_libraries(engine PRIVATE ${WASM_OPTS})
else()
    add_library(engine ${source_files})
endif()
target_include_directories(engine PUBLIC . ./include ./lib)
sc_common_compiler_options(engine)
add_definitions(-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)

# Make cmake selections visible to C++ code
if(SC_NO_THREADING)
    target_compile_definitions(engine PUBLIC SC_NO_THREADING)
endif()
if(SC_NO_NETWORKING)
    target_compile_definitions(engine PUBLIC SC_NO_NETWORKING)
endif()

# Detect pthreads
if(NOT SC_NO_THREADING)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(engine Threads::Threads)
endif()

# Networking libs
if(NOT SC_NO_NETWORKING)
    if(NOT WIN32)
        find_package(CURL REQUIRED)
        if (CURL_FOUND)
            target_link_libraries(engine ${CURL_LIBRARIES})
            target_include_directories(engine PUBLIC ${CURL_INCLUDE_DIRS})
        else(CURL_FOUND)
            message(FATAL_ERROR "libcurl not found")
        endif()
    else()
        target_link_libraries(engine crypt32)
        target_link_libraries(engine wininet)
    endif()
endif()

if(SC_EVENT_QUEUE_DEBUG)
    target_compile_definitions(engine PUBLIC EVENT_QUEUE_DEBUG)
endif()

target_compile_definitions(engine PRIVATE "SC_GIT_REV=\"${GIT_COMMIT_HASH}\"" "SC_GIT_BRANCH=\"${GIT_BRANCH}\"")

add_custom_target(file_toucher
        COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${CMAKE_CURRENT_SOURCE_DIR}/util/git_info.cpp)

add_dependencies(engine file_toucher)

# Default Blizzard Community Platform API key
if (DEFINED SC_DEFAULT_APIKEY)
  target_compile_definitions(engine PRIVATE "SC_DEFAULT_APIKEY=\"${SC_DEFAULT_APIKEY}\"")
endif()
