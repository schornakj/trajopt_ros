#include <trajopt_utils/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
TRAJOPT_IGNORE_WARNINGS_POP

int main(int argc, char** argv)
{
  std::this_thread::sleep_for(std::chrono::seconds(3));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
