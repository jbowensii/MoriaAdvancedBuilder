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
