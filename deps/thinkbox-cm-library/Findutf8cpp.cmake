add_library(utf8cpp::utf8cpp INTERFACE IMPORTED)
target_include_directories(utf8cpp::utf8cpp INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../../deps/utf8cpp/source")
set(utf8cpp_FOUND TRUE)
