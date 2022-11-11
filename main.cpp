#include "mbed.h"

#include "update-client/mbed_application.hpp"
#include "update-client/candidate_applications.hpp"
#include "update-client/flash_updater.hpp"
#include "update-client/uc_error_codes.hpp"

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

int main_complete()
{
#if MBED_CONF_MBED_TRACE_ENABLE
    mbed_trace_init();
    mbed_trace_print_function_set(boot_debug);
#endif // MBED_CONF_MBED_TRACE_ENABLE

    tr_debug("Bootloader\r\n");

    // print the reason for reset
    reset_reason_t reason = hal_reset_reason_get();
    tr_debug(" Reset reason is %d\r\n", reason);

    reset_reason_capabilities_t cap = {0};
    hal_reset_reason_get_capabilities(&cap);
    tr_debug(" Reset reason capabilities 0x%08x\r\n", cap.reasons);

    hal_reset_reason_clear();

    // create a flash updater and initialize it
    update_client::FlashUpdater flashUpdater;
    int err = flashUpdater.init();
    if (0 == err) {
        // check the integrity of the active application
        update_client::MbedApplication activeApplication(flashUpdater, HEADER_ADDR, POST_APPLICATION_ADDR);
        int32_t result = activeApplication.checkApplication();
        if (result != update_client::UC_ERR_NONE) {
            tr_error(" Active application is not valid: %d\r\n", result);
        } else {
            tr_debug(" Active application is valid\r\n");
        }

        // search for available firmwares
        const uint32_t headerSize = POST_APPLICATION_ADDR - HEADER_ADDR;
        tr_debug(" Header size is %d", headerSize);
        update_client::CandidateApplications candidateApplications(flashUpdater,
                                                                   MBED_CONF_UPDATE_CLIENT_STORAGE_ADDRESS,
                                                                   MBED_CONF_UPDATE_CLIENT_STORAGE_SIZE,
                                                                   headerSize,
                                                                   MBED_CONF_UPDATE_CLIENT_STORAGE_LOCATIONS);

        uint32_t newestSlotIndex = 0;
        if (candidateApplications.hasValidNewerApplication(activeApplication, newestSlotIndex)) {
            // there exists a valid newer application
            // compare the active application with the newest valid candidate
            activeApplication.compareTo(candidateApplications.getMbedApplication(newestSlotIndex));

            // install the newest candidate
            result = candidateApplications.installApplication(newestSlotIndex, HEADER_ADDR);
            if (result != update_client::UC_ERR_NONE) {
                tr_error(" Could not install application at slot %d: %d\r\n", newestSlotIndex, result);
            }
            tr_debug(" Application at slot %d installed as active application\r\n", newestSlotIndex);

            // compare again the active application and check it
            activeApplication.compareTo(candidateApplications.getMbedApplication(newestSlotIndex));
            activeApplication.checkApplication();
        }
        flashUpdater.deinit();
    } else {
        tr_error("Init flash failed: %d\r\n", err);
    }

    // at this stage we directly branch to the main application
    void *sp = *((void **) POST_APPLICATION_ADDR + 0);
    void *pc = *((void **) POST_APPLICATION_ADDR + 1);
    tr_debug(" Starting application at address 0x%08x (sp 0x%08x, pc 0x%08x)\r\n",
             POST_APPLICATION_ADDR, (uint32_t) sp, (uint32_t) pc);

    mbed_start_application(POST_APPLICATION_ADDR);
    
    return 0;
}
