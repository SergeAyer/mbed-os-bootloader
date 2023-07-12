#include "mbed.h"

#include "mbed_trace.h"
#if MBED_CONF_MBED_TRACE_ENABLE
#define TRACE_GROUP "bootloader"
#endif // MBED_CONF_MBED_TRACE_ENABLE

#if MBED_CONF_MBED_TRACE_ENABLE
static UnbufferedSerial g_uart(CONSOLE_TX, CONSOLE_RX);

// Function that directly outputs to an unbuffered serial port in blocking mode.
static void boot_debug(const char *s)
{
    size_t len = strlen(s);
    g_uart.write(s, len);
    g_uart.write("\r\n", 2);
}
#endif

int main_initial()
{
#if MBED_CONF_MBED_TRACE_ENABLE
    mbed_trace_init();
    mbed_trace_print_function_set(boot_debug);
#endif // MBED_CONF_MBED_TRACE_ENABLE

    tr_debug("BikeComputer bootloader\r\n");

    // at this stage we directly branch to the main application
    void *sp = *((void **) POST_APPLICATION_ADDR + 0);  // NOLINT(readability/casting)
    void *pc = *((void **) POST_APPLICATION_ADDR + 1);  // NOLINT(readability/casting)
    tr_debug("Starting application at address 0x%08x (sp 0x%08x, pc 0x%08x)\r\n", POST_APPLICATION_ADDR, (uint32_t) sp, (uint32_t) pc);

    mbed_start_application(POST_APPLICATION_ADDR);

    return 0;
}