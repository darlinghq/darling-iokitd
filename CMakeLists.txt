project(iokitd)

add_compile_options(
	-Wno-gcc-compat
)

add_definitions(-DIOKIT=1 -DIOKIT_ALL_IPC=1 -DIOKIT_SERVER_VERSION=20150101)
add_definitions(-nostdinc)

find_package(X11 REQUIRED)
if (NOT X11_Xrandr_FOUND)
	message(FATAL "XRandR not found")
endif (NOT X11_Xrandr_FOUND)

include_directories(
	${X11_INCLUDE_DIR}
	${X11_Xrandr_INCLUDE_PATH}
)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR})
# dserver_platform_identity_t, used by libsystem_kernel's __darling_get_platform_identity
include_directories(${CMAKE_SOURCE_DIR}/src/external/darlingserver/include)

# The host's libdbus-1, used through the dbus wrapper library
find_package(PkgConfig REQUIRED)
pkg_check_modules(PC_DBUS REQUIRED dbus-1)
include_directories(${PC_DBUS_INCLUDE_DIRS})

mig(iokitmig.defs)
mig(powermanagement.defs)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

set(iokitd_sources
	src/main.mm
	src/stubs.c
	src/stubs-powermanagement.c
	src/iokitd.cpp
	src/ServiceRegistry.mm
	src/IOObject.cpp
	src/IOIterator.cpp
	src/IOService.mm
	src/IORegistryEntry.mm
	src/IODisplayConnect.mm
	src/IODisplayConnectX11.mm
	src/PowerAssertions.mm
	src/PowerSources.mm
	src/PowerAssertionsX11.mm
	src/IOPMrootDomain.mm
	src/HostBus.c
	src/HostPowerInhibit.c
	src/LogindSleep.c
	src/IOSurfaceRoot.mm
	src/IOSurface.mm
	src/IOUserClient.mm
	src/IOInterestNotification.mm
	src/IOUserClientApple.mm
	src/IOPlatformExpertDevice.mm
	src/IOServiceNotification.mm
	src/IOHIDGamepadDevice.mm
	src/HostGamepad.c
	src/IOHostGPU.mm
	${CMAKE_CURRENT_BINARY_DIR}/iokitmigServer.c
	${CMAKE_CURRENT_BINARY_DIR}/powermanagementServer.c
	${CMAKE_CURRENT_SOURCE_DIR}/../IOKitUser/IOCFSerialize.c
	${CMAKE_CURRENT_SOURCE_DIR}/../IOKitUser/IOCFUnserialize.tab.c
)

add_darling_executable(iokitd ${iokitd_sources})
target_link_libraries(iokitd cxx bsm.0 CoreFoundation Foundation X11 XRandR Xext EGL GL IOSurface dbus)

install(TARGETS iokitd DESTINATION libexec/darling/usr/sbin)
install(FILES org.darlinghq.iokitd.plist DESTINATION libexec/darling/System/Library/LaunchDaemons)

