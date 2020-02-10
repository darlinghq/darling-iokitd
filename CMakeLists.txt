project(iokitd)

add_compile_options(
	-nostdinc
	-nostdinc++
	-Wno-gcc-compat
)

add_definitions(-DIOKIT=1 -DIOKIT_ALL_IPC=1 -DIOKIT_SERVER_VERSION=20150101)

include_directories(
	${CMAKE_SOURCE_DIR}/src/launchd
	${CMAKE_SOURCE_DIR}/src/lkm/osfmk
	${CMAKE_SOURCE_DIR}/src/external/IOKitUser
)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/src/external/libcxx/include ${CMAKE_CURRENT_BINARY_DIR})

mig(iokitmig.defs)

set(iokitd_sources
	src/main.cpp
	src/stubs.c
	src/iokitd.cpp
	src/Registry.mm
	src/IOObject.cpp
	src/IOIterator.cpp
	src/IOService.mm
	src/IODisplayConnect.mm
	src/IODisplayConnectX11.mm
	${CMAKE_CURRENT_BINARY_DIR}/iokitmigServer.c
	${CMAKE_CURRENT_SOURCE_DIR}/../IOKitUser/IOCFSerialize.c
	${CMAKE_CURRENT_SOURCE_DIR}/../IOKitUser/IOCFUnserialize.tab.c
)

add_darling_executable(iokitd ${iokitd_sources})
target_link_libraries(iokitd cxx CoreFoundation Foundation)

install(TARGETS iokitd DESTINATION libexec/darling/usr/sbin)
install(FILES org.darlinghq.iokitd.plist DESTINATION libexec/darling/System/Library/LaunchDaemons)

