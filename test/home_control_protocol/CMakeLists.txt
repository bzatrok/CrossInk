add_executable(HomeControlProtocolTest
  HomeControlProtocolTest.cpp
  ${REPO_ROOT}/lib/HomeControl/HueProtocol.cpp
  ${REPO_ROOT}/lib/HomeControl/TadoProtocol.cpp
)

target_include_directories(HomeControlProtocolTest PRIVATE
  ${REPO_ROOT}/lib/HomeControl
)

target_link_libraries(HomeControlProtocolTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(HomeControlProtocolTest)
