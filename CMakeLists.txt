project(iesp)
cmake_minimum_required(VERSION 3.9)

add_compile_options(-Wall -pedantic -Wextra)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(
	iesp
	iscsi-pdu.cpp
	main.cpp
	random.cpp
	server.cpp
	session.cpp
	utils.cpp
)

set(CMAKE_BUILD_TYPE Debug)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(iesp Threads::Threads)
