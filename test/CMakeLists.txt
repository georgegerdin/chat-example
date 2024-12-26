add_executable(test-packet test-packet.cc)
add_executable(test-chatserver test-chatserver.cc)
add_executable(test-chatclient test-chatclient.cc)

target_link_libraries(test-packet PUBLIC chat-lib)
target_link_libraries(test-chatserver PUBLIC chat-lib)
target_link_libraries(test-chatclient PUBLIC chat-lib)
