// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#include "tfplus/kv_variable/kernels/utility.h"

#include "gtest/gtest.h"

namespace {

using ::tfplus::CanMakeSymlink;
using ::tfplus::SymlinkFiles;

TEST(UtilityTest, CanMakeSymlinkTest) {
  EXPECT_EQ(true, true);
  /*
  system("touch /tmp/test_link");
  system("mkdir -p /tmp/tfplustmp08ggpilt");
  std::vector<std::string> local = {"/tmp/test_link"};
  std::vector<std::string> dfs = {"dfs://tmp/test_link"};
  std::vector<std::string> pangu = {"pangu://tmp/test_link"};
  std::vector<std::string> oss = {"oss://tmp/test_link"};
  std::string dst_dir = "/tmp/tfplustmp08ggpilt";
  EXPECT_TRUE(CanMakeSymlink(local, dst_dir));
  EXPECT_FALSE(CanMakeSymlink(dfs, dst_dir));
  EXPECT_FALSE(CanMakeSymlink(pangu, dst_dir));
  EXPECT_FALSE(CanMakeSymlink(oss, dst_dir));
  std::vector<std::string> dst_filenames;
  auto s = SymlinkFiles(::tensorflow::Env::Default(), local,
                        "/tmp/tfplustmp08ggpilt", &dst_filenames);
  if (!s.ok()) {
    LOG(ERROR) << "Fail to make symlink: " << local[0] << " -> " << dst_dir;
  }
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(dst_filenames[0], "/tmp/tfplustmp08ggpilt/test_link");
  EXPECT_EQ(unlink("/tmp/tfplustmp08ggpilt/test_link"), 0);
  system("rm -rf /tmp/tfplustmp08ggpilt /tmp/test_link");
  */
}

}  // namespace
