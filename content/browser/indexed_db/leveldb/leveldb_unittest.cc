// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstring>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/platform_file.h"
#include "base/string16.h"
#include "content/browser/indexed_db/leveldb/leveldb_comparator.h"
#include "content/browser/indexed_db/leveldb/leveldb_database.h"
#include "content/browser/indexed_db/leveldb/leveldb_iterator.h"
#include "content/browser/indexed_db/leveldb/leveldb_slice.h"
#include "content/browser/indexed_db/leveldb/leveldb_transaction.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

class SimpleComparator : public LevelDBComparator {
 public:
  virtual int Compare(const LevelDBSlice& a, const LevelDBSlice& b) const
      OVERRIDE {
    size_t len = std::min(a.end() - a.begin(), b.end() - b.begin());
    return memcmp(a.begin(), b.begin(), len);
  }
  virtual const char* Name() const OVERRIDE { return "temp_comparator"; }
};

std::vector<char> EncodeString(const std::string& s) {
  std::vector<char> ret(s.size());
  for (size_t i = 0; i < s.size(); ++i)
    ret[i] = s[i];
  return ret;
}

TEST(LevelDBDatabaseTest, CorruptionTest) {
  base::ScopedTempDir temp_directory;
  ASSERT_TRUE(temp_directory.CreateUniqueTempDir());

  const std::vector<char> key = EncodeString("key");
  const std::vector<char> put_value = EncodeString("value");
  std::vector<char> got_value;
  SimpleComparator comparator;

  scoped_ptr<LevelDBDatabase> leveldb =
      LevelDBDatabase::Open(temp_directory.path(), &comparator);
  EXPECT_TRUE(leveldb);
  bool success = leveldb->Put(LevelDBSlice(key), put_value);
  EXPECT_TRUE(success);
  leveldb.Pass();
  EXPECT_FALSE(leveldb);

  leveldb = LevelDBDatabase::Open(temp_directory.path(), &comparator);
  EXPECT_TRUE(leveldb);
  bool found = false;
  success = leveldb->Get(LevelDBSlice(key), got_value, found);
  EXPECT_TRUE(success);
  EXPECT_TRUE(found);
  EXPECT_EQ(put_value, got_value);
  leveldb.Pass();
  EXPECT_FALSE(leveldb);

  base::FilePath file_path = temp_directory.path().AppendASCII("CURRENT");
  base::PlatformFile handle = base::CreatePlatformFile(
      file_path,
      base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_WRITE,
      NULL,
      NULL);
  base::TruncatePlatformFile(handle, 0);
  base::ClosePlatformFile(handle);

  leveldb = LevelDBDatabase::Open(temp_directory.path(), &comparator);
  EXPECT_FALSE(leveldb);

  bool destroyed = LevelDBDatabase::Destroy(temp_directory.path());
  EXPECT_TRUE(destroyed);

  leveldb = LevelDBDatabase::Open(temp_directory.path(), &comparator);
  EXPECT_TRUE(leveldb);
  success = leveldb->Get(LevelDBSlice(key), got_value, found);
  EXPECT_TRUE(success);
  EXPECT_FALSE(found);
}

TEST(LevelDBDatabaseTest, Transaction) {
  base::ScopedTempDir temp_directory;
  ASSERT_TRUE(temp_directory.CreateUniqueTempDir());

  const std::vector<char> key = EncodeString("key");
  std::vector<char> got_value;
  SimpleComparator comparator;

  scoped_ptr<LevelDBDatabase> leveldb =
      LevelDBDatabase::Open(temp_directory.path(), &comparator);
  EXPECT_TRUE(leveldb);

  const std::vector<char> old_value = EncodeString("value");
  bool success = leveldb->Put(LevelDBSlice(key), old_value);
  EXPECT_TRUE(success);

  scoped_refptr<LevelDBTransaction> transaction =
      LevelDBTransaction::Create(leveldb.get());

  const std::vector<char> new_value = EncodeString("new value");
  success = leveldb->Put(LevelDBSlice(key), new_value);
  EXPECT_TRUE(success);

  bool found = false;
  success = transaction->Get(LevelDBSlice(key), got_value, found);
  EXPECT_TRUE(success);
  EXPECT_TRUE(found);
  EXPECT_EQ(
      comparator.Compare(LevelDBSlice(got_value), LevelDBSlice(old_value)), 0);

  found = false;
  success = leveldb->Get(LevelDBSlice(key), got_value, found);
  EXPECT_TRUE(success);
  EXPECT_TRUE(found);
  EXPECT_EQ(
      comparator.Compare(LevelDBSlice(got_value), LevelDBSlice(new_value)), 0);

  const std::vector<char> added_key = EncodeString("added key");
  const std::vector<char> added_value = EncodeString("added value");
  success = leveldb->Put(LevelDBSlice(added_key), added_value);
  EXPECT_TRUE(success);

  success = leveldb->Get(LevelDBSlice(added_key), got_value, found);
  EXPECT_TRUE(success);
  EXPECT_TRUE(found);
  EXPECT_EQ(
      comparator.Compare(LevelDBSlice(got_value), LevelDBSlice(added_value)),
      0);

  success = transaction->Get(LevelDBSlice(added_key), got_value, found);
  EXPECT_TRUE(success);
  EXPECT_FALSE(found);
}

TEST(LevelDBDatabaseTest, TransactionIterator) {
  base::ScopedTempDir temp_directory;
  ASSERT_TRUE(temp_directory.CreateUniqueTempDir());

  const std::vector<char> key1 = EncodeString("key1");
  const std::vector<char> value1 = EncodeString("value1");
  const std::vector<char> key2 = EncodeString("key2");
  const std::vector<char> value2 = EncodeString("value2");

  SimpleComparator comparator;
  bool success;

  scoped_ptr<LevelDBDatabase> leveldb =
      LevelDBDatabase::Open(temp_directory.path(), &comparator);
  EXPECT_TRUE(leveldb);

  success = leveldb->Put(LevelDBSlice(key1), value1);
  EXPECT_TRUE(success);
  success = leveldb->Put(LevelDBSlice(key2), value2);
  EXPECT_TRUE(success);

  scoped_refptr<LevelDBTransaction> transaction =
      LevelDBTransaction::Create(leveldb.get());

  success = leveldb->Remove(LevelDBSlice(key2));
  EXPECT_TRUE(success);

  scoped_ptr<LevelDBIterator> it = transaction->CreateIterator();

  const char empty[] = {0};
  it->Seek(LevelDBSlice(empty, empty));

  EXPECT_TRUE(it->IsValid());
  EXPECT_EQ(comparator.Compare(LevelDBSlice(it->Key()), LevelDBSlice(key1)), 0);
  EXPECT_EQ(comparator.Compare(it->Value(), LevelDBSlice(value1)), 0);

  it->Next();

  EXPECT_TRUE(it->IsValid());
  EXPECT_EQ(comparator.Compare(it->Key(), LevelDBSlice(key2)), 0);
  EXPECT_EQ(comparator.Compare(it->Value(), LevelDBSlice(value2)), 0);

  it->Next();

  EXPECT_FALSE(it->IsValid());
}

}  // namespace

}  // namespace content
