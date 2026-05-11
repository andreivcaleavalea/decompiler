file(GLOB_RECURSE DECOMPILER_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE DECOMPILER_HEADERS CONFIGURE_DEPENDS "include/*.h" "src/*.h")

add_library(DecompilerLib STATIC ${DECOMPILER_SOURCES} ${DECOMPILER_HEADERS})

target_include_directories(DecompilerLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(DecompilerLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
