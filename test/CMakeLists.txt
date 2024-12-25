add_executable(test-packet test-packet.cc)
add_executable(test-chatserver test-chatserver.cc)

target_link_libraries(test-packet PUBLIC chat-lib)
target_link_libraries(test-chatserver PUBLIC chat-lib)

