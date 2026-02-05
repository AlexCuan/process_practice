cmake_minimum_required(VERSION 3.10)
project(Procesos C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")


add_executable(ship ship.c map.c)
target_link_libraries(ship m)


add_executable(captain captain.c map.c)
target_link_libraries(captain m)


add_executable(ursula ursula.c map.c)
target_link_libraries(ursula m)