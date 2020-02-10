project(iokitd)

add_compile_options(
	-nostdinc
	-nostdinc++
	-Wno-gcc-compat
)

add_definitions(-DIOKIT=1 -DIOKIT_ALL_IPC=1)

include_directories(
	${CMAKE_SOURCE_DIR}/src/launchd
	${CMAKE_SOURCE_DIR}/src/lkm/osfmk
)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/external/libcxx/include ${CMAKE_CURRENT_BINARY_DIR})

mig(iokitmig.defs)

set(iokitd_sources
	src/main.cpp
	src/stubs.c
	${CMAKE_CURRENT_BINARY_DIR}/iokitmigServer.c
)

add_darling_executable(iokitd ${iokitd_sources})
target_link_libraries(iokitd cxx)

install(TARGETS iokitd DESTINATION libexec/darling/usr/sbin)
install(FILES org.darlinghq.iokitd.plist DESTINATION libexec/darling/System/Library/LaunchDaemons)

