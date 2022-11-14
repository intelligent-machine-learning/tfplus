// Copyright 2019 The TF-plus Authors. All Rights Reserved.

#include "tbb/concurrent_unordered_map.h"
#include "tbb/concurrent_hash_map.h"
#include "gtest/gtest.h"

namespace {
typedef tbb::concurrent_hash_map<std::string, std::vector<int>> HashMap;
typedef tbb::concurrent_unordered_map<std::string, std::vector<int>> UnHashMap;
typedef typename HashMap::const_accessor HashMapConstAccessor;
typedef typename HashMap::accessor HashMapAccessor;
typedef typename HashMap::iterator HashMapIterator;
typedef HashMap::value_type HashMapValuePair;

TEST(TbbLibTest, TbbChmTest) {
  HashMap hashMap;
  std::vector<std::string> key = {"foo", "bar"};
  std::vector<std::vector<int>> val = {{1, 1}, {2, 2}};
  for (size_t i = 0; i < key.size(); ++i) {
    HashMapValuePair hashMapValuePair(key[i], val[i]);
    hashMap.insert(hashMapValuePair);
  }

  HashMapAccessor hashConstAccessor;
  if (hashMap.find(hashConstAccessor, "foo")) {
    for (auto &ite : hashConstAccessor->second) {
      EXPECT_EQ(ite, 1);
    }
    hashConstAccessor.release();
  }

  HashMapAccessor hashAccessor;
  if (hashMap.find(hashAccessor, "foo")) {
    hashAccessor->second = {3, 3};
    for (auto &ite : hashAccessor->second) {
      EXPECT_EQ(ite, 3);
    }
  }

  EXPECT_EQ(hashMap.size(), 2);
  if (!hashMap.erase(hashAccessor)) {
    EXPECT_EQ(true, false);
  }

  EXPECT_EQ(hashMap.size(), 1);
  hashAccessor.release();
}

TEST(TbbLibTest, TbbCumTest) {
  UnHashMap unHashMap;
  std::vector<std::string> key = {"foo", "bar"};
  std::vector<std::vector<int>> val = {{1, 1}, {2, 2}};

  for (size_t i = 0; i < key.size(); ++i) {
    unHashMap[key[i]] = val[i];
  }

  if (unHashMap.count("foo") > 0) {
    unHashMap["foo"] = {3, 3};
  }

  EXPECT_EQ(unHashMap.size(), 2);
  if (!unHashMap.unsafe_erase("foo")) {
      EXPECT_EQ(true, false);
  }
}

}  // namespace
