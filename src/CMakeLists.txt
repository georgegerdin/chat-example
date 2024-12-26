add_library(chat-lib
    chatclient.cc
    chatserver.cc
    format.cc
)

add_executable(chat-example
    main.cc
)

target_include_directories(chat-lib INTERFACE ${CMAKE_SOURCE_DIR}/include PRIVATE ${CMAKE_SOURCE_DIR}/include/chat_example)
target_link_libraries(chat-example PUBLIC chat-lib)
