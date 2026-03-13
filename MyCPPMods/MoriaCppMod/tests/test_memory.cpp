// Unit tests for isReadableMemory (Win32 VirtualQuery wrapper)

#include <gtest/gtest.h>
#include "moria_testable.h"

#include <cstdlib>

using namespace MoriaMods;

TEST(IsReadableMemory, Null)
{
    EXPECT_FALSE(isReadableMemory(nullptr));
    EXPECT_FALSE(isReadableMemory(nullptr, 64));
}

TEST(IsReadableMemory, StackVariable)
{
    int x = 42;
    EXPECT_TRUE(isReadableMemory(&x, sizeof(x)));
}

TEST(IsReadableMemory, StackArray)
{
    char buf[256]{};
    EXPECT_TRUE(isReadableMemory(buf, sizeof(buf)));
}

TEST(IsReadableMemory, HeapAlloc)
{
    void* ptr = malloc(1024);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(isReadableMemory(ptr, 1024));
    free(ptr);
}

TEST(IsReadableMemory, InvalidAddress)
{
    // 0xDEADBEEF should not be mapped memory
    EXPECT_FALSE(isReadableMemory(reinterpret_cast<void*>(0xDEADBEEF)));
}

TEST(IsReadableMemory, VeryHighAddress)
{
    // Kernel-space addresses are not readable from user mode
    EXPECT_FALSE(isReadableMemory(reinterpret_cast<void*>(0xFFFF000000000000ULL)));
}

TEST(IsReadableMemory, StaticData)
{
    static const char staticStr[] = "test string";
    EXPECT_TRUE(isReadableMemory(staticStr, sizeof(staticStr)));
}

TEST(IsReadableMemory, FunctionPointer)
{
    // The code of isReadableMemory itself should be in executable memory
    auto fn = &isReadableMemory;
    EXPECT_TRUE(isReadableMemory(reinterpret_cast<const void*>(fn), 1));
}

TEST(IsReadableMemory, SizeOne)
{
    int x = 42;
    EXPECT_TRUE(isReadableMemory(&x, 1));
}

TEST(IsReadableMemory, StringLiteral)
{
    const char* lit = "hello world";
    EXPECT_TRUE(isReadableMemory(lit, 12));
}

TEST(IsReadableMemory, LowAddress)
{
    // Address 1 (not null but not valid)
    EXPECT_FALSE(isReadableMemory(reinterpret_cast<void*>(1)));
}

TEST(IsReadableMemory, AlignedInvalidPage)
{
    // Page-aligned address in typically unmapped region
    EXPECT_FALSE(isReadableMemory(reinterpret_cast<void*>(0x10000000000ULL)));
}

TEST(IsReadableMemory, SizeZero)
{
    // Size 0 with valid pointer — only checks first page
    int x = 42;
    EXPECT_TRUE(isReadableMemory(&x, 0));
}

TEST(IsReadableMemory, LargeSize)
{
    // Heap allocation with large size check
    void* ptr = malloc(65536);
    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(isReadableMemory(ptr, 65536));
    free(ptr);
}

TEST(IsReadableMemory, NullWithSizeZero)
{
    // Null pointer should fail even with size 0 (early null check)
    EXPECT_FALSE(isReadableMemory(nullptr, 0));
}

TEST(IsReadableMemory, GlobalConstant)
{
    // Static const data should be readable
    static const int arr[100] = {1, 2, 3};
    EXPECT_TRUE(isReadableMemory(arr, sizeof(arr)));
}

TEST(IsReadableMemory, CrossPageBoundary)
{
    // Allocate a large block and check across likely page boundary
    void* ptr = malloc(8192);
    ASSERT_NE(ptr, nullptr);
    // Check starting near the end of the allocation
    EXPECT_TRUE(isReadableMemory(static_cast<uint8_t*>(ptr) + 4000, 4000));
    free(ptr);
}
