project(iesp)
cmake_minimum_required(VERSION 3.9)

add_compile_options(-Wall -pedantic -D_FILE_OFFSET_BITS=64)  # -Wextra -fanalyzer
#add_compile_options(-fsanitize=address -O0 -fno-inline)
#add_compile_options(-fsanitize=undefined -O0 -fno-inline)
#add_compile_options(--coverage -O0 -fno-inline)

SET(VERSION "2.0")

add_definitions(-DVERSION=\"${VERSION}\")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(
	iesp
	backend.cpp
	backend-file.cpp
	backend-nbd.cpp
	com.cpp
	com-sockets.cpp
	iscsi.cpp
	iscsi-pdu.cpp
	log.cpp
	main.cpp
	random.cpp
	server.cpp
	scsi.cpp
	session.cpp
	snmp.cpp
	utils.cpp
	snmp/snmp.cpp
	snmp/snmp_data.cpp
	snmp/snmp_elem.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(iesp Threads::Threads)

#set(CMAKE_BUILD_TYPE RelWithDebInfo)
set(CMAKE_BUILD_TYPE Debug)
#target_link_libraries(iesp -fsanitize=address)
#target_link_libraries(iesp -fsanitize=undefined)
#target_link_libraries(iesp --coverage)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
