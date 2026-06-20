add_executable(basic basic.cpp)
target_link_libraries(basic PRIVATE awkward::awkward)

add_executable(optional optional.cpp)
target_link_libraries(optional PRIVATE awkward::awkward)
