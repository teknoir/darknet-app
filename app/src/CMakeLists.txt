CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

file(GLOB SOURCE_FILES "*.cpp")
add_executable( teknoir_app ${SOURCE_FILES} )

target_link_libraries(teknoir_app paho-mqttpp3)
target_link_libraries(teknoir_app paho-mqtt3a)
target_link_libraries(teknoir_app darknet)

set(INSTALL_TARGETS
	teknoir_app
)

## install binaries
install(TARGETS teknoir_app RUNTIME DESTINATION bin)