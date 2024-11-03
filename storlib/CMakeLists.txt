cmake_minimum_required(VERSION 3.15.0 FATAL_ERROR)

add_library(storlib SHARED
    storlib.cpp)
    
target_include_directories(storlib PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(storlib PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(storlib PRIVATE ${FFMPEG_LIBRARIES})
