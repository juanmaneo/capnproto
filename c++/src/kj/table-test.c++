// Copyright (c) 2018 Kenton Varda and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "table.h"
#include <kj/test.h>
#include <set>
#include <unordered_set>
#include "hash.h"
#include "time.h"
#include <stdlib.h>

namespace kj {
namespace _ {
namespace {

#if defined(KJ_DEBUG) && !__OPTIMIZE__
static constexpr uint MEDIUM_PRIME = 619;
static constexpr uint BIG_PRIME = 6143;
#else
static constexpr uint MEDIUM_PRIME = 6143;
static constexpr uint BIG_PRIME = 101363;
#endif
// Some of the tests build large tables. These numbers are used as the table sizes. We use primes
// to avoid any unintended aliasing affects -- this is probably just paranoia, but why not?
//
// We use smaller values for debug builds to keep runtime down.

KJ_TEST("_::tryReserveSize() works") {
  {
    Vector<int> vec;
    tryReserveSize(vec, "foo"_kj);
    KJ_EXPECT(vec.capacity() == 4);  // Vectors always grow by powers of two.
  }
  {
    Vector<int> vec;
    tryReserveSize(vec, 123);
    KJ_EXPECT(vec.capacity() == 0);
  }
}

class StringHasher {
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool matches(StringPtr a, StringPtr b) const {
    return a == b;
  }
  uint hashCode(StringPtr str) const {
    return kj::hashCode(str);
  }
};

KJ_TEST("simple table") {
  Table<StringPtr, HashIndex<StringHasher>> table;

  KJ_EXPECT(table.find("foo") == kj::none);

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert("foo") == "foo");
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert("bar") == "bar");
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("foo")) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(table.find("fop") == kj::none);
  KJ_EXPECT(table.find("baq") == kj::none);

  {
    StringPtr& ref = table.insert("baz");
    KJ_EXPECT(ref == "baz");
    StringPtr& ref2 = KJ_ASSERT_NONNULL(table.find("baz"));
    KJ_EXPECT(&ref == &ref2);
  }

  KJ_EXPECT(table.size() == 3);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "foo");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(iter == table.end());
  }

  KJ_EXPECT(table.eraseMatch("foo"));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(table.find("foo") == kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(iter == table.end());
  }

  {
    auto& row = table.upsert("qux", [&](StringPtr&, StringPtr&&) {
      KJ_FAIL_ASSERT("shouldn't get here");
    });

    auto copy = kj::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(param.begin() == copy.begin());
      KJ_EXPECT(&existing == &row);
    });

    auto& found = KJ_ASSERT_NONNULL(table.find("qux"));
    KJ_EXPECT(&found == &row);
  }

  StringPtr STRS[] = { "corge"_kj, "grault"_kj, "garply"_kj };
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  KJ_EXPECT(table.size() == 6);
  KJ_EXPECT(table.find("corge") != kj::none);
  KJ_EXPECT(table.find("grault") != kj::none);
  KJ_EXPECT(table.find("garply") != kj::none);

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));

  KJ_EXPECT(table.size() == 6);

  KJ_EXPECT(table.insert("baa") == "baa");

  KJ_EXPECT(table.eraseAll([](StringPtr s) { return s.startsWith("ba"); }) == 3);
  KJ_EXPECT(table.size() == 4);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(iter == table.end());
  }

  auto& graultRow = table.begin()[1];
  kj::StringPtr origGrault = graultRow;

  KJ_EXPECT(&table.findOrCreate("grault",
      [&]() -> kj::StringPtr { KJ_FAIL_ASSERT("shouldn't have called this"); }) == &graultRow);
  KJ_EXPECT(graultRow.begin() == origGrault.begin());
  KJ_EXPECT(&KJ_ASSERT_NONNULL(table.find("grault")) == &graultRow);
  KJ_EXPECT(table.find("waldo") == kj::none);
  KJ_EXPECT(table.size() == 4);

  kj::String searchWaldo = kj::str("waldo");
  kj::String insertWaldo = kj::str("waldo");

  auto& waldo = table.findOrCreate(searchWaldo,
      [&]() -> kj::StringPtr { return insertWaldo; });
  KJ_EXPECT(waldo == "waldo");
  KJ_EXPECT(waldo.begin() == insertWaldo.begin());
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("grault")) == "grault");
  KJ_EXPECT(&KJ_ASSERT_NONNULL(table.find("waldo")) == &waldo);
  KJ_EXPECT(table.size() == 5);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(*iter++ == "waldo");
    KJ_EXPECT(iter == table.end());
  }
}

class BadHasher {
  // String hash that always returns the same hash code. This should not affect correctness, only
  // performance.
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool matches(StringPtr a, StringPtr b) const {
    return a == b;
  }
  uint hashCode(StringPtr str) const {
    return 1234;
  }
};

KJ_TEST("hash tables when hash is always same") {
  Table<StringPtr, HashIndex<BadHasher>> table;

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert("foo") == "foo");
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert("bar") == "bar");
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("foo")) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(table.find("fop") == kj::none);
  KJ_EXPECT(table.find("baq") == kj::none);

  {
    StringPtr& ref = table.insert("baz");
    KJ_EXPECT(ref == "baz");
    StringPtr& ref2 = KJ_ASSERT_NONNULL(table.find("baz"));
    KJ_EXPECT(&ref == &ref2);
  }

  KJ_EXPECT(table.size() == 3);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "foo");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(iter == table.end());
  }

  KJ_EXPECT(table.eraseMatch("foo"));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(table.find("foo") == kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(iter == table.end());
  }

  {
    auto& row = table.upsert("qux", [&](StringPtr&, StringPtr&&) {
      KJ_FAIL_ASSERT("shouldn't get here");
    });

    auto copy = kj::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(param.begin() == copy.begin());
      KJ_EXPECT(&existing == &row);
    });

    auto& found = KJ_ASSERT_NONNULL(table.find("qux"));
    KJ_EXPECT(&found == &row);
  }

  StringPtr STRS[] = { "corge"_kj, "grault"_kj, "garply"_kj };
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  KJ_EXPECT(table.size() == 6);
  KJ_EXPECT(table.find("corge") != kj::none);
  KJ_EXPECT(table.find("grault") != kj::none);
  KJ_EXPECT(table.find("garply") != kj::none);

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));
}

class IntHasher {
  // Dumb integer hasher that just returns the integer itself.
public:
  uint keyForRow(uint i) const { return i; }

  bool matches(uint a, uint b) const {
    return a == b;
  }
  uint hashCode(uint i) const {
    return i;
  }
};

KJ_TEST("HashIndex with many erasures doesn't keep growing") {
  HashIndex<IntHasher> index;

  kj::ArrayPtr<uint> rows = nullptr;

  for (uint i: kj::zeroTo(1000000)) {
    KJ_ASSERT(index.insert(rows, 0, i) == kj::none);
    index.erase(rows, 0, i);
  }

  KJ_ASSERT(index.capacity() < 10);
}

struct SiPair {
  kj::StringPtr str;
  uint i;

  inline bool operator==(SiPair other) const {
    return str == other.str && i == other.i;
  }
};

class SiPairStringHasher {
public:
  StringPtr keyForRow(SiPair s) const { return s.str; }

  bool matches(SiPair a, StringPtr b) const {
    return a.str == b;
  }
  uint hashCode(StringPtr str) const {
    return inner.hashCode(str);
  }

private:
  StringHasher inner;
};

class SiPairIntHasher {
public:
  uint keyForRow(SiPair s) const { return s.i; }

  bool matches(SiPair a, uint b) const {
    return a.i == b;
  }
  uint hashCode(uint i) const {
    return i;
  }
};

KJ_TEST("double-index table") {
  Table<SiPair, HashIndex<SiPairStringHasher>, HashIndex<SiPairIntHasher>> table;

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert({"foo", 123}) == (SiPair {"foo", 123}));
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert({"bar", 456}) == (SiPair {"bar", 456}));
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<HashIndex<SiPairStringHasher>>("foo")) ==
            (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<HashIndex<SiPairIntHasher>>(123)) ==
            (SiPair {"foo", 123}));

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert({"foo", 111}));
  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert({"qux", 123}));

  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));

  KJ_EXPECT(
      table.findOrCreate<0>("foo",
          []() -> SiPair { KJ_FAIL_ASSERT("shouldn't have called this"); })
      == (SiPair {"foo", 123}));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table",
      table.findOrCreate<0>("corge", []() -> SiPair { return {"corge", 123}; }));

  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("bar")) == (SiPair {"bar", 456}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(456)) == (SiPair {"bar", 456}));
  KJ_EXPECT(table.find<0>("corge") == kj::none);

  KJ_EXPECT(
      table.findOrCreate<0>("corge", []() -> SiPair { return {"corge", 789}; })
      == (SiPair {"corge", 789}));

  KJ_EXPECT(table.size() == 3);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("bar")) == (SiPair {"bar", 456}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(456)) == (SiPair {"bar", 456}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("corge")) == (SiPair {"corge", 789}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(789)) == (SiPair {"corge", 789}));

  KJ_EXPECT(
      table.findOrCreate<1>(234, []() -> SiPair { return {"grault", 234}; })
      == (SiPair {"grault", 234}));

  KJ_EXPECT(table.size() == 4);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("foo")) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(123)) == (SiPair {"foo", 123}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("bar")) == (SiPair {"bar", 456}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(456)) == (SiPair {"bar", 456}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("corge")) == (SiPair {"corge", 789}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(789)) == (SiPair {"corge", 789}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<0>("grault")) == (SiPair {"grault", 234}));
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find<1>(234)) == (SiPair {"grault", 234}));
}

class UintHasher {
public:
  uint keyForRow(uint i) const { return i; }

  bool matches(uint a, uint b) const {
    return a == b;
  }
  uint hashCode(uint i) const {
    return i;
  }
};

KJ_TEST("benchmark: kj::Table<uint, HashIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<uint, HashIndex<UintHasher>> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == kj::none);
      KJ_ASSERT(table.find(i * 5 + 124) == kj::none);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123)));
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == kj::none);
      } else {
        uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

KJ_TEST("benchmark: std::unordered_set<uint>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    std::unordered_set<uint> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(i * 5 + 123);
      KJ_ASSERT(iter != table.end());
      uint value = *iter;
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == table.end());
      KJ_ASSERT(table.find(i * 5 + 124) == table.end());
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(i * 5 + 123) > 0);
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == table.end());
      } else {
        auto iter = table.find(i * 5 + 123);
        KJ_ASSERT(iter != table.end());
        uint value = *iter;
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

KJ_TEST("benchmark: kj::Table<StringPtr, HashIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i: kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<StringPtr, HashIndex<StringHasher>> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      StringPtr value = KJ_ASSERT_NONNULL(table.find(strings[i]));
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(strings[i])));
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == kj::none);
      } else {
        StringPtr value = KJ_ASSERT_NONNULL(table.find(strings[i]));
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

struct StlStringHash {
  inline size_t operator()(StringPtr str) const {
    return kj::hashCode(str);
  }
};

KJ_TEST("benchmark: std::unordered_set<StringPtr>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i: kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    std::unordered_set<StringPtr, StlStringHash> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(strings[i]);
      KJ_ASSERT(iter != table.end());
      StringPtr value = *iter;
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(strings[i]) > 0);
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == table.end());
      } else {
        auto iter = table.find(strings[i]);
        KJ_ASSERT(iter != table.end());
        StringPtr value = *iter;
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

// =======================================================================================

KJ_TEST("B-tree internals") {
  {
    BTreeImpl::Leaf leaf;
    memset(&leaf, 0, sizeof(leaf));

    for (auto i: kj::indices(leaf.rows)) {
      KJ_CONTEXT(i);

      KJ_EXPECT(leaf.size() == i);

      if (i < kj::size(leaf.rows) / 2) {
#ifdef KJ_DEBUG
        KJ_EXPECT_THROW(FAILED, leaf.isHalfFull());
#endif
        KJ_EXPECT(!leaf.isMostlyFull());
      }

      if (i == kj::size(leaf.rows) / 2) {
        KJ_EXPECT(leaf.isHalfFull());
        KJ_EXPECT(!leaf.isMostlyFull());
      }

      if (i > kj::size(leaf.rows) / 2) {
        KJ_EXPECT(!leaf.isHalfFull());
        KJ_EXPECT(leaf.isMostlyFull());
      }

      if (i == kj::size(leaf.rows)) {
        KJ_EXPECT(leaf.isFull());
      } else {
        KJ_EXPECT(!leaf.isFull());
      }

      leaf.rows[i] = 1;
    }
    KJ_EXPECT(leaf.size() == kj::size(leaf.rows));
  }

  {
    BTreeImpl::Parent parent;
    memset(&parent, 0, sizeof(parent));

    for (auto i: kj::indices(parent.keys)) {
      KJ_CONTEXT(i);

      KJ_EXPECT(parent.keyCount() == i);

      if (i < kj::size(parent.keys) / 2) {
#ifdef KJ_DEBUG
        KJ_EXPECT_THROW(FAILED, parent.isHalfFull());
#endif
        KJ_EXPECT(!parent.isMostlyFull());
      }

      if (i == kj::size(parent.keys) / 2) {
        KJ_EXPECT(parent.isHalfFull());
        KJ_EXPECT(!parent.isMostlyFull());
      }

      if (i > kj::size(parent.keys) / 2) {
        KJ_EXPECT(!parent.isHalfFull());
        KJ_EXPECT(parent.isMostlyFull());
      }

      if (i == kj::size(parent.keys)) {
        KJ_EXPECT(parent.isFull());
      } else {
        KJ_EXPECT(!parent.isFull());
      }

      parent.keys[i] = 1;
    }
    KJ_EXPECT(parent.keyCount() == kj::size(parent.keys));
  }
}

class StringCompare {
public:
  StringPtr keyForRow(StringPtr s) const { return s; }

  bool isBefore(StringPtr a, StringPtr b) const {
    return a < b;
  }
  bool matches(StringPtr a, StringPtr b) const {
    return a == b;
  }
};

KJ_TEST("simple tree table") {
  Table<StringPtr, TreeIndex<StringCompare>> table;

  KJ_EXPECT(table.find("foo") == kj::none);

  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.insert("foo") == "foo");
  KJ_EXPECT(table.size() == 1);
  KJ_EXPECT(table.insert("bar") == "bar");
  KJ_EXPECT(table.size() == 2);

  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("foo")) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(table.find("fop") == kj::none);
  KJ_EXPECT(table.find("baq") == kj::none);

  {
    StringPtr& ref = table.insert("baz");
    KJ_EXPECT(ref == "baz");
    StringPtr& ref2 = KJ_ASSERT_NONNULL(table.find("baz"));
    KJ_EXPECT(&ref == &ref2);
  }

  KJ_EXPECT(table.size() == 3);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(*iter++ == "foo");
    KJ_EXPECT(iter == range.end());
  }

  KJ_EXPECT(table.eraseMatch("foo"));
  KJ_EXPECT(table.size() == 2);
  KJ_EXPECT(table.find("foo") == kj::none);
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("bar")) == "bar");
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("baz")) == "baz");

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "bar");
    KJ_EXPECT(*iter++ == "baz");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto& row = table.upsert("qux", [&](StringPtr&, StringPtr&&) {
      KJ_FAIL_ASSERT("shouldn't get here");
    });

    auto copy = kj::str("qux");
    table.upsert(StringPtr(copy), [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(param.begin() == copy.begin());
      KJ_EXPECT(&existing == &row);
    });

    auto& found = KJ_ASSERT_NONNULL(table.find("qux"));
    KJ_EXPECT(&found == &row);
  }

  StringPtr STRS[] = { "corge"_kj, "grault"_kj, "garply"_kj };
  table.insertAll(ArrayPtr<StringPtr>(STRS));
  KJ_EXPECT(table.size() == 6);
  KJ_EXPECT(table.find("corge") != kj::none);
  KJ_EXPECT(table.find("grault") != kj::none);
  KJ_EXPECT(table.find("garply") != kj::none);

  KJ_EXPECT_THROW_MESSAGE("inserted row already exists in table", table.insert("bar"));

  KJ_EXPECT(table.size() == 6);

  KJ_EXPECT(table.insert("baa") == "baa");

  KJ_EXPECT(table.eraseAll([](StringPtr s) { return s.startsWith("ba"); }) == 3);
  KJ_EXPECT(table.size() == 4);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto range = table.range("foo", "har");
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto range = table.range("garply", "grault");
    auto iter = range.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(iter == range.end());
  }

  {
    auto iter = table.seek("garply");
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(iter == table.ordered().end());
  }

  {
    auto iter = table.seek("gorply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(iter == table.ordered().end());
  }

  auto& graultRow = table.begin()[1];
  kj::StringPtr origGrault = graultRow;

  KJ_EXPECT(&table.findOrCreate("grault",
      [&]() -> kj::StringPtr { KJ_FAIL_ASSERT("shouldn't have called this"); }) == &graultRow);
  KJ_EXPECT(graultRow.begin() == origGrault.begin());
  KJ_EXPECT(&KJ_ASSERT_NONNULL(table.find("grault")) == &graultRow);
  KJ_EXPECT(table.find("waldo") == kj::none);
  KJ_EXPECT(table.size() == 4);

  kj::String searchWaldo = kj::str("waldo");
  kj::String insertWaldo = kj::str("waldo");

  auto& waldo = table.findOrCreate(searchWaldo,
      [&]() -> kj::StringPtr { return insertWaldo; });
  KJ_EXPECT(waldo == "waldo");
  KJ_EXPECT(waldo.begin() == insertWaldo.begin());
  KJ_EXPECT(KJ_ASSERT_NONNULL(table.find("grault")) == "grault");
  KJ_EXPECT(&KJ_ASSERT_NONNULL(table.find("waldo")) == &waldo);
  KJ_EXPECT(table.size() == 5);

  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(*iter++ == "waldo");
    KJ_EXPECT(iter == table.end());
  }

  // Verify that move constructor/assignment work.
  Table<StringPtr, TreeIndex<StringCompare>> other(kj::mv(table));
  KJ_EXPECT(other.size() == 5);
  KJ_EXPECT(table.size() == 0);
  KJ_EXPECT(table.begin() == table.end());
  {
    auto iter = other.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(*iter++ == "waldo");
    KJ_EXPECT(iter == other.end());
  }

  table = kj::mv(other);
  KJ_EXPECT(other.size() == 0);
  KJ_EXPECT(table.size() == 5);
  {
    auto iter = table.begin();
    KJ_EXPECT(*iter++ == "garply");
    KJ_EXPECT(*iter++ == "grault");
    KJ_EXPECT(*iter++ == "qux");
    KJ_EXPECT(*iter++ == "corge");
    KJ_EXPECT(*iter++ == "waldo");
    KJ_EXPECT(iter == table.end());
  }
  KJ_EXPECT(other.begin() == other.end());
}

class UintCompare {
public:
  uint keyForRow(uint i) const { return i; }

  bool isBefore(uint a, uint b) const {
    return a < b;
  }
  bool matches(uint a, uint b) const {
    return a == b;
  }
};

KJ_TEST("large tree table") {
  constexpr uint SOME_PRIME = MEDIUM_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<uint, TreeIndex<UintCompare>> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == kj::none);
      KJ_ASSERT(table.find(i * 5 + 124) == kj::none);
    }
    table.verify();

    {
      auto range = table.ordered();
      auto iter = range.begin();
      for (uint i: kj::zeroTo(SOME_PRIME)) {
        KJ_ASSERT(*iter++ == i * 5 + 123);
      }
      KJ_ASSERT(iter == range.end());
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      KJ_CONTEXT(i);
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123), i));
        table.verify();
      }
    }

    {
      auto range = table.ordered();
      auto iter = range.begin();
      for (uint i: kj::zeroTo(SOME_PRIME)) {
        if (i % 2 == 0 || i % 7 == 0) {
          // erased
          KJ_ASSERT(table.find(i * 5 + 123) == kj::none);
        } else {
          uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
          KJ_ASSERT(value == i * 5 + 123);
          KJ_ASSERT(*iter++ == i * 5 + 123);
        }
      }
      KJ_ASSERT(iter == range.end());
    }
  }
}

KJ_TEST("TreeIndex fuzz test") {
  // A test which randomly modifies a TreeIndex to try to discover buggy state changes.

  uint seed = (kj::systemPreciseCalendarClock().now() - kj::UNIX_EPOCH) / kj::NANOSECONDS;
  KJ_CONTEXT(seed);  // print the seed if the test fails
  srand(seed);

  Table<uint, TreeIndex<UintCompare>> table;

  auto randomInsert = [&]() {
    table.upsert(rand(), [](auto&&, auto&&) {});
  };
  auto randomErase = [&]() {
    if (table.size() > 0) {
      auto& row = table.begin()[rand() % table.size()];
      table.erase(row);
    }
  };
  auto randomLookup = [&]() {
    if (table.size() > 0) {
      auto& row = table.begin()[rand() % table.size()];
      auto& found = KJ_ASSERT_NONNULL(table.find(row));
      KJ_ASSERT(&found == &row);
    }
  };

  // First pass: focus on insertions, aim to do 2x as many insertions as deletions.
  for (auto i KJ_UNUSED: kj::zeroTo(1000)) {
    switch (rand() % 4) {
      case 0:
      case 1:
        randomInsert();
        break;
      case 2:
        randomErase();
        break;
      case 3:
        randomLookup();
        break;
    }

    table.verify();
  }

  // Second pass: focus on deletions, aim to do 2x as many deletions as insertions.
  for (auto i KJ_UNUSED: kj::zeroTo(1000)) {
    switch (rand() % 4) {
      case 0:
        randomInsert();
        break;
      case 1:
      case 2:
        randomErase();
        break;
      case 3:
        randomLookup();
        break;
    }

    table.verify();
  }
}

KJ_TEST("TreeIndex clear() leaves tree in valid state") {
  // A test which ensures that calling clear() does not break the internal state of a TreeIndex.
  // It used to be the case that clearing a non-empty tree would leave it thinking that it had room
  // for one more node than it really did, causing it to write and read beyond the end of its
  // internal array of nodes.
  Table<uint, TreeIndex<UintCompare>> table;

  // Insert at least one value to allocate an initial set of tree nodes.
  table.upsert(1, [](auto&&, auto&&) {});
  KJ_EXPECT(table.find(1) != kj::none);
  table.clear();

  // Insert enough values to force writes/reads beyond the end of the tree's internal node array.
  for (uint i = 0; i < 29; ++i) {
    table.upsert(i, [](auto&&, auto&&) {});
  }
  for (uint i = 0; i < 29; ++i) {
    KJ_EXPECT(table.find(i) != kj::none);
  }
}

KJ_TEST("benchmark: kj::Table<uint, TreeIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<uint, TreeIndex<UintCompare>> table;
    table.reserve(SOME_PRIME);
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == kj::none);
      KJ_ASSERT(table.find(i * 5 + 124) == kj::none);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(i * 5 + 123)));
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == kj::none);
      } else {
        uint value = KJ_ASSERT_NONNULL(table.find(i * 5 + 123));
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

KJ_TEST("benchmark: std::set<uint>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    std::set<uint> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(j * 5 + 123);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(i * 5 + 123);
      KJ_ASSERT(iter != table.end());
      uint value = *iter;
      KJ_ASSERT(value == i * 5 + 123);
      KJ_ASSERT(table.find(i * 5 + 122) == table.end());
      KJ_ASSERT(table.find(i * 5 + 124) == table.end());
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(i * 5 + 123) > 0);
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(i * 5 + 123) == table.end());
      } else {
        auto iter = table.find(i * 5 + 123);
        KJ_ASSERT(iter != table.end());
        uint value = *iter;
        KJ_ASSERT(value == i * 5 + 123);
      }
    }
  }
}

KJ_TEST("benchmark: kj::Table<StringPtr, TreeIndex>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i: kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    Table<StringPtr, TreeIndex<StringCompare>> table;
    table.reserve(SOME_PRIME);
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      StringPtr value = KJ_ASSERT_NONNULL(table.find(strings[i]));
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        table.erase(KJ_ASSERT_NONNULL(table.find(strings[i])));
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == kj::none);
      } else {
        auto& value = KJ_ASSERT_NONNULL(table.find(strings[i]));
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

KJ_TEST("benchmark: std::set<StringPtr>") {
  constexpr uint SOME_PRIME = BIG_PRIME;
  constexpr uint STEP[] = {1, 2, 4, 7, 43, 127};

  kj::Vector<String> strings(SOME_PRIME);
  for (uint i: kj::zeroTo(SOME_PRIME)) {
    strings.add(kj::str(i * 5 + 123));
  }

  for (auto step: STEP) {
    KJ_CONTEXT(step);
    std::set<StringPtr> table;
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      uint j = (i * step) % SOME_PRIME;
      table.insert(strings[j]);
    }
    for (uint i: kj::zeroTo(SOME_PRIME)) {
      auto iter = table.find(strings[i]);
      KJ_ASSERT(iter != table.end());
      StringPtr value = *iter;
      KJ_ASSERT(value == strings[i]);
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        KJ_ASSERT(table.erase(strings[i]) > 0);
      }
    }

    for (uint i: kj::zeroTo(SOME_PRIME)) {
      if (i % 2 == 0 || i % 7 == 0) {
        // erased
        KJ_ASSERT(table.find(strings[i]) == table.end());
      } else {
        auto iter = table.find(strings[i]);
        KJ_ASSERT(iter != table.end());
        StringPtr value = *iter;
        KJ_ASSERT(value == strings[i]);
      }
    }
  }
}

// =======================================================================================

KJ_TEST("insertion order index") {
  Table<uint, InsertionOrderIndex> table;

  {
    auto range = table.ordered();
    KJ_EXPECT(range.begin() == range.end());
  }

  table.insert(12);
  table.insert(34);
  table.insert(56);
  table.insert(78);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 12);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 34);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 56);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 78);
    KJ_EXPECT(iter == range.end());
    KJ_EXPECT(*--iter == 78);
    KJ_EXPECT(*--iter == 56);
    KJ_EXPECT(*--iter == 34);
    KJ_EXPECT(*--iter == 12);
    KJ_EXPECT(iter == range.begin());
  }

  table.erase(table.begin()[1]);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 12);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 56);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 78);
    KJ_EXPECT(iter == range.end());
    KJ_EXPECT(*--iter == 78);
    KJ_EXPECT(*--iter == 56);
    KJ_EXPECT(*--iter == 12);
    KJ_EXPECT(iter == range.begin());
  }

  // Allocate enough more elements to cause a resize.
  table.insert(111);
  table.insert(222);
  table.insert(333);
  table.insert(444);
  table.insert(555);
  table.insert(666);
  table.insert(777);
  table.insert(888);
  table.insert(999);

  {
    auto range = table.ordered();
    auto iter = range.begin();
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 12);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 56);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 78);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 111);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 222);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 333);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 444);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 555);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 666);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 777);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 888);
    KJ_ASSERT(iter != range.end());
    KJ_EXPECT(*iter++ == 999);
    KJ_EXPECT(iter == range.end());
  }

  // Remove everything.
  while (table.size() > 0) {
    table.erase(*table.begin());
  }

  {
    auto range = table.ordered();
    KJ_EXPECT(range.begin() == range.end());
  }
}

KJ_TEST("insertion order index is movable") {
  using UintTable = Table<uint, InsertionOrderIndex>;

  kj::Maybe<UintTable> myTable;

  {
    UintTable yourTable;

    yourTable.insert(12);
    yourTable.insert(34);
    yourTable.insert(56);
    yourTable.insert(78);
    yourTable.insert(111);
    yourTable.insert(222);
    yourTable.insert(333);
    yourTable.insert(444);
    yourTable.insert(555);
    yourTable.insert(666);
    yourTable.insert(777);
    yourTable.insert(888);
    yourTable.insert(999);

    myTable = kj::mv(yourTable);
  }

  auto& table = KJ_ASSERT_NONNULL(myTable);

  // At one time the following induced a segfault/double-free, due to incorrect memory management in
  // InsertionOrderIndex's move ctor and dtor.
  auto range = table.ordered();
  auto iter = range.begin();
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 12);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 34);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 56);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 78);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 111);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 222);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 333);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 444);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 555);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 666);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 777);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 888);
  KJ_ASSERT(iter != range.end());
  KJ_EXPECT(*iter++ == 999);
  KJ_EXPECT(iter == range.end());
}

// =======================================================================================
// Test bug where insertion failure on a later index in the table would not be rolled back
// correctly if a previous index was TreeIndex.

class StringLengthCompare {
  // Considers two strings equal if they have the same length.
public:
  inline size_t keyForRow(StringPtr entry) const {
    return entry.size();
  }

  inline bool matches(StringPtr e, size_t key) const {
    return e.size() == key;
  }

  inline bool isBefore(StringPtr e, size_t key) const {
    return e.size() < key;
  }

  uint hashCode(size_t size) const {
    return size;
  }
};

KJ_TEST("HashIndex rollback on insertion failure") {
  // Test that when an insertion produces a duplicate on a later index, changes to previous indexes
  // are properly rolled back.

  Table<StringPtr, HashIndex<StringHasher>, HashIndex<StringLengthCompare>> table;
  table.insert("a"_kj);
  table.insert("ab"_kj);
  table.insert("abc"_kj);

  {
    // We use upsert() so that we don't throw an exception from the duplicate, but this exercises
    // the same logic as a duplicate insert() other than throwing.
    kj::StringPtr& found = table.upsert("xyz"_kj, [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(existing == "abc");
      KJ_EXPECT(param == "xyz");
    });
    KJ_EXPECT(found == "abc");

    table.erase(found);
  }

  table.insert("xyz"_kj);

  {
    kj::StringPtr& found = table.upsert("tuv"_kj, [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(existing == "xyz");
      KJ_EXPECT(param == "tuv");
    });
    KJ_EXPECT(found == "xyz");
  }
}

KJ_TEST("TreeIndex rollback on insertion failure") {
  // Test that when an insertion produces a duplicate on a later index, changes to previous indexes
  // are properly rolled back.

  Table<StringPtr, TreeIndex<StringCompare>, TreeIndex<StringLengthCompare>> table;
  table.insert("a"_kj);
  table.insert("ab"_kj);
  table.insert("abc"_kj);

  {
    // We use upsert() so that we don't throw an exception from the duplicate, but this exercises
    // the same logic as a duplicate insert() other than throwing.
    kj::StringPtr& found = table.upsert("xyz"_kj, [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(existing == "abc");
      KJ_EXPECT(param == "xyz");
    });
    KJ_EXPECT(found == "abc");

    table.erase(found);
  }

  table.insert("xyz"_kj);

  {
    kj::StringPtr& found = table.upsert("tuv"_kj, [&](StringPtr& existing, StringPtr&& param) {
      KJ_EXPECT(existing == "xyz");
      KJ_EXPECT(param == "tuv");
    });
    KJ_EXPECT(found == "xyz");
  }
}

}  // namespace
}  // namespace _
}  // namespace kj
