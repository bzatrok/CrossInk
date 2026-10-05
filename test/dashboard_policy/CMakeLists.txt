add_executable(DashboardPolicyTest
  DashboardPolicyTest.cpp
  ${REPO_ROOT}/src/dashboard/DashboardPolicy.cpp
)

target_include_directories(DashboardPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_compile_definitions(DashboardPolicyTest PRIVATE
  CROSSINK_APP_CAP_DASHBOARD=1
)

target_link_libraries(DashboardPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DashboardPolicyTest)
