#include <gtest/gtest.h>
#include "bci.h" // Include the header for BCIhandler and dependencies

// Mock or stub dependencies as needed
// Example: Mock get8, get32, put8, put32, etc.

class BCIhandlerTest : public ::testing::Test {
protected:
    vm_ctx ctx;
    uint8_t src[64];
    void SetUp() override {
        memset(&ctx, 0, sizeof(ctx));
        memset(src, 0, sizeof(src));
        // Set up any additional initialization or mocks
    }
};

TEST_F(BCIhandlerTest, CallsBCIsendInit) {
    // Arrange: Set up src to trigger a known command
    src[0] = BCIFN_READ;
    // Act
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that BCIsendInit was called (use a mock or flag)
}

TEST_F(BCIhandlerTest, HandlesReadCommand) {
    src[0] = BCIFN_READ;
    src[1] = 1; // n
    // Set up address and expected memory
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that put32 was called with expected value
}

TEST_F(BCIhandlerTest, HandlesWriteCommand) {
    src[0] = BCIFN_WRITE;
    src[1] = 1; // n
    // Set up address and value to write
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that VMwriteCell was called with expected value
}

TEST_F(BCIhandlerTest, HandlesExecuteCommandInAdminMode) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_EXECUTE;
    // Set up stack and xt
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that simulate was called and stack handled
}

TEST_F(BCIhandlerTest, HandlesGetCyclesCommand) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_GET_CYCLES;
    ctx.cycles = 0x123456789ABCDEF0;
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that put32 was called with lower and upper 32 bits
}

TEST_F(BCIhandlerTest, HandlesCRCCommand) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_CRC;
    // Set up code/text memory and lengths
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that CRC32 was called and put32 with results
}

TEST_F(BCIhandlerTest, HandlesWriteTextCommand) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_WRTEXT;
    // Set up address and data
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that FlashWrite was called
}

TEST_F(BCIhandlerTest, HandlesWriteCodeCommand) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_WRCODE;
    // Set up address and data
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that FlashWrite was called
}

TEST_F(BCIhandlerTest, HandlesSectorEraseCommand) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_SECTOR_ERASE;
    // Set up sector number
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that FlashErase was called
}

TEST_F(BCIhandlerTest, HandlesStrobeCommand) {
    ctx.admin = BCI_ADMIN_ACTIVE;
    src[0] = BCIFN_STROBE;
    // Set up strobe value
    // ...
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that VMreset or status change occurred
}

TEST_F(BCIhandlerTest, HandlesInvalidCommand) {
    src[0] = 0xFF; // Invalid command
    BCIhandler(&ctx, src, sizeof(src));
    // Assert: Check that ctx.ior == BCI_BAD_COMMAND
}
