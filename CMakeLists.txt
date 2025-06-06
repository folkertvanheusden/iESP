cmake_minimum_required(VERSION 3.9)
project(iesp)

add_compile_options(-Wall -pedantic -D_FILE_OFFSET_BITS=64)  # -D_GLIBCXX_DEBUG -Wextra -fanalyzer
#add_compile_options(-fsanitize=address,undefined)
#add_compile_options(--coverage -O0 -fno-inline)
#add_compile_options(-fno-inline)

SET(VERSION "2.3")

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
	snmp/block.cpp
	snmp/snmp.cpp
	snmp/snmp_data.cpp
	snmp/snmp_elem.cpp
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(iesp Threads::Threads)

include(FindPkgConfig)

if ((NOT IS_WINDOWS) AND (NOT IS_MAC))
add_executable(
	quick-test
	quick-test.cpp
)

pkg_check_modules(LIBISCSI REQUIRED libiscsi)
target_link_libraries(quick-test ${LIBISCSI_LIBRARIES})
target_include_directories(quick-test PUBLIC ${LIBISCSI_INCLUDE_DIRS})
target_compile_options(quick-test PUBLIC ${LIBISCSI_CFLAGS_OTHER})

endif ((NOT IS_WINDOWS) AND (NOT IS_MAC))

set(CMAKE_BUILD_TYPE RelWithDebInfo)
#set(CMAKE_BUILD_TYPE Debug)
#target_link_libraries(iesp -fsanitize=address,undefined)
#target_link_libraries(iesp --coverage)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
