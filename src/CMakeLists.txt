add_executable(chat-example main.cc)
target_include_directories(chat-example INTERFACE ${CMAKE_SOURCE_DIR}/include PRIVATE ${CMAKE_SOURCE_DIR}/include/chat-example)