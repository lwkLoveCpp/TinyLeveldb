cmake_minimum_required(VERSION 3.10.0)
project(TinyLeveldb VERSION 0.1.0 LANGUAGES C CXX)

file(GLOB db_source db/*.cpp db/*.h)

add_executable(TinyLeveldb main.cpp)

