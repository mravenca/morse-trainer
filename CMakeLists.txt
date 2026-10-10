cmake_minimum_required(VERSION 3.16)
project(MorseTrainer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia)

# Pure C++ core (no Qt): Morse table, decoder, scoring. Unit-tested separately.
add_library(morsecore STATIC
    src/morse.cpp
    src/decoder.cpp
    src/trainer.cpp
)
target_include_directories(morsecore PUBLIC src)

qt_add_executable(morse-trainer
    src/main.cpp
    src/mainwindow.cpp      src/mainwindow.h
    src/tonegenerator.cpp   src/tonegenerator.h
    src/keysource.cpp       src/keysource.h
    src/serialkeysource.cpp src/serialkeysource.h
    src/gpiokeysource.cpp   src/gpiokeysource.h
    src/keyscope.cpp        src/keyscope.h
)
target_link_libraries(morse-trainer PRIVATE morsecore Qt6::Widgets Qt6::Multimedia)

find_package(Threads REQUIRED)
target_link_libraries(morse-trainer PRIVATE Threads::Threads)

install(TARGETS morse-trainer RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(core_test tests/core_test.cpp)
    target_link_libraries(core_test PRIVATE morsecore)
    add_test(NAME core_test COMMAND core_test)
endif()
