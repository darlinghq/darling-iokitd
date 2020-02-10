project(iokitd)

add_compile_options(
	-nostdinc
)

set(iokitd_sources
	src/main.cpp
)

add_darling_executable(iokitd ${iokitd_sources})

install(TARGETS iokitd DESTINATION libexec/darling/usr/sbin)
install(FILES org.darlinghq.iokitd.plist DESTINATION libexec/darling/System/Library/LaunchDaemons)

