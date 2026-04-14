set(CAMERA_TEST camera_capture_test)

add_executable(${CAMERA_TEST} main.cpp)

target_link_libraries(${CAMERA_TEST} PRIVATE
    biopass_stb
    openpnp-capture
    CLI11::CLI11
)
