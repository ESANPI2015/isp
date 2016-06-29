cmake_minimum_required(VERSION 2.8)

add_executable(isprog isp.cpp)
target_link_libraries(isprog isp ndlcom)
install(TARGETS isprog
    RUNTIME DESTINATION bin)
