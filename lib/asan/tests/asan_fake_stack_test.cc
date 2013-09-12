//===-- asan_fake_stack_test.cc -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Tests for FakeStack.
// This test file should be compiled w/o asan instrumentation.
//===----------------------------------------------------------------------===//

#include "asan_fake_stack.h"
#include "asan_test_utils.h"
#include "sanitizer_common/sanitizer_common.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include <set>

namespace __asan {

TEST(FakeStack, FlagsSize) {
  EXPECT_EQ(FakeStack::SizeRequiredForFlags(10), 1 << 5);
  EXPECT_EQ(FakeStack::SizeRequiredForFlags(11), 1 << 6);
  EXPECT_EQ(FakeStack::SizeRequiredForFlags(20), 1 << 15);
}

TEST(FakeStack, RequiredSize) {
  // for (int i = 15; i < 20; i++) {
  //  uptr alloc_size = FakeStack::RequiredSize(i);
  //  printf("%zdK ==> %zd\n", 1 << (i - 10), alloc_size);
  // }
  EXPECT_EQ(FakeStack::RequiredSize(15), 365568);
  EXPECT_EQ(FakeStack::RequiredSize(16), 727040);
  EXPECT_EQ(FakeStack::RequiredSize(17), 1449984);
  EXPECT_EQ(FakeStack::RequiredSize(18), 2895872);
  EXPECT_EQ(FakeStack::RequiredSize(19), 5787648);
}

TEST(FakeStack, FlagsOffset) {
  for (uptr stack_size_log = 15; stack_size_log <= 20; stack_size_log++) {
    uptr stack_size = 1UL << stack_size_log;
    uptr offset = 0;
    for (uptr class_id = 0; class_id < FakeStack::kNumberOfSizeClasses;
         class_id++) {
      uptr frame_size = FakeStack::BytesInSizeClass(class_id);
      uptr num_flags = stack_size / frame_size;
      EXPECT_EQ(offset, FakeStack::FlagsOffset(stack_size_log, class_id));
      // printf("%zd: %zd => %zd %zd\n", stack_size_log, class_id, offset,
      //        FakeStack::FlagsOffset(stack_size_log, class_id));
      offset += num_flags;
    }
  }
}

TEST(FakeStack, CreateDestroy) {
  for (int i = 0; i < 1000; i++) {
    for (uptr stack_size_log = 20; stack_size_log <= 22; stack_size_log++) {
      FakeStack *fake_stack = FakeStack::Create(stack_size_log);
      fake_stack->Destroy();
    }
  }
}

TEST(FakeStack, ModuloNumberOfFrames) {
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, 0), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<15)), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<10)), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<9)), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<8)), 1<<8);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 0, (1<<15) + 1), 1);

  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 0), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 1<<9), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 1<<8), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 1, 1<<7), 1<<7);

  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 0), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 1), 1);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 15), 15);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 16), 0);
  EXPECT_EQ(FakeStack::ModuloNumberOfFrames(15, 5, 17), 1);
}

TEST(FakeStack, GetFrame) {
  const uptr stack_size_log = 20;
  const uptr stack_size = 1 << stack_size_log;
  FakeStack *fs = FakeStack::Create(stack_size_log);
  u8 *base = fs->GetFrame(stack_size_log, 0, 0);
  EXPECT_EQ(base, reinterpret_cast<u8 *>(fs) +
                      fs->SizeRequiredForFlags(stack_size_log) + 4096);
  EXPECT_EQ(base + 0*stack_size + 64 * 7, fs->GetFrame(stack_size_log, 0, 7));
  EXPECT_EQ(base + 1*stack_size + 128 * 3, fs->GetFrame(stack_size_log, 1, 3));
  EXPECT_EQ(base + 2*stack_size + 256 * 5, fs->GetFrame(stack_size_log, 2, 5));
  fs->Destroy();
}

TEST(FakeStack, Allocate) {
  const uptr stack_size_log = 19;
  FakeStack *fs = FakeStack::Create(stack_size_log);
  std::set<FakeFrame *> s;
  for (int iter = 0; iter < 2; iter++) {
    s.clear();
    for (uptr cid = 0; cid < FakeStack::kNumberOfSizeClasses; cid++) {
      uptr n = FakeStack::NumberOfFrames(stack_size_log, cid);
      uptr bytes_in_class = FakeStack::BytesInSizeClass(cid);
      for (uptr j = 0; j < n; j++) {
        FakeFrame *ff = fs->Allocate(stack_size_log, cid, 0);
        uptr x = reinterpret_cast<uptr>(ff);
        EXPECT_TRUE(s.insert(ff).second);
        EXPECT_EQ(x, fs->AddrIsInFakeStack(x));
        EXPECT_EQ(x, fs->AddrIsInFakeStack(x + 1));
        EXPECT_EQ(x, fs->AddrIsInFakeStack(x + bytes_in_class - 1));
        EXPECT_NE(x, fs->AddrIsInFakeStack(x + bytes_in_class));
      }
      if (iter == 0 &&
          (cid == 0 || cid == FakeStack::kNumberOfSizeClasses - 1)) {
        // This is slow, so we do it only sometimes.
        EXPECT_DEATH(fs->Allocate(stack_size_log, cid, 0),
                     "Failed to allocate a fake stack frame");
      }
    }
    for (std::set<FakeFrame *>::iterator it = s.begin(); it != s.end(); ++it) {
      FakeFrame *ff = *it;
      fs->Deallocate(ff, stack_size_log, ff->class_id, 0);
    }
  }
  fs->Destroy();
}

static void RecursiveFunction(FakeStack *fs, int depth) {
  uptr class_id = depth / 3;
  FakeFrame *ff = fs->Allocate(fs->stack_size_log(), class_id, 0);
  if (depth) {
    RecursiveFunction(fs, depth - 1);
    RecursiveFunction(fs, depth - 1);
  }
  fs->Deallocate(ff, fs->stack_size_log(), class_id, 0);
}

TEST(FakeStack, RecursiveStressTest) {
  const uptr stack_size_log = 16;
  FakeStack *fs = FakeStack::Create(stack_size_log);
  RecursiveFunction(fs, 22);  // with 26 runs for 2-3 seconds.
  fs->Destroy();
}

}  // namespace __asan