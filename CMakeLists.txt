cmake_minimum_required(VERSION 3.12)
project(parrot-lite C)

set(CMAKE_C_STANDARD 99)

add_executable(parrot-lite
        proto/c_string.c
        proto/parrot_message.c
        proto/parrot_payload.c
        main.c
)

target_include_directories(parrot-lite PRIVATE src)
