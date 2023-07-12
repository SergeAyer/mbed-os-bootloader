// Copyright 2022 Haute école d'ingénierie et d'architecture de Fribourg
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/****************************************************************************
 * @file main.cpp
 * @author Serge Ayer <serge.ayer@hefr.ch>
 *
 * @brief Bootloader application
 *
 * @date 2022-09-01
 * @version 0.1.0
 ***************************************************************************/

#include "FlashIAPBlockDevice.h"
#include "mbed.h"
#include "mbed_trace.h"
#include "update-client/block_device_application.hpp"
#include "update-client/candidate_applications.hpp"
#include "update-client/uc_error_code.hpp"
#if MBED_CONF_MBED_TRACE_ENABLE
#define TRACE_GROUP "bootloader"
#endif  // MBED_CONF_MBED_TRACE_ENABLE

#if MBED_CONF_MBED_TRACE_ENABLE
static UnbufferedSerial g_uart(CONSOLE_TX, CONSOLE_RX);

// Function that directly outputs to an unbuffered serial port in blocking mode.
static void boot_debug(const char* s) {
    size_t len = strlen(s);
    g_uart.write(s, len);
    g_uart.write("\r\n", 2);
}
#endif

int main() {
#if MBED_CONF_MBED_TRACE_ENABLE
    mbed_trace_init();
    mbed_trace_print_function_set(boot_debug);
#endif  // MBED_CONF_MBED_TRACE_ENABLE

    tr_debug("Bootloader");

    // print the reason for reset
    reset_reason_t reason = hal_reset_reason_get();
    tr_debug(" Reset reason is %d", reason);

    reset_reason_capabilities_t cap = {0};
    hal_reset_reason_get_capabilities(&cap);
    tr_debug(" Reset reason capabilities 0x%08x", cap.reasons);

    hal_reset_reason_clear();

    // create a flash updater and initialize it
    FlashIAPBlockDevice flashIAPBlockDevice(MBED_ROM_START, MBED_ROM_SIZE);
    int err = flashIAPBlockDevice.init();
    if (0 == err) {
        // check the integrity of the active application
        tr_debug("Checking active application");
        // addresses are specified relative to block device base address
        mbed::bd_addr_t headerAddress      = HEADER_ADDR - MBED_ROM_START;
        mbed::bd_addr_t applicationAddress = POST_APPLICATION_ADDR - MBED_ROM_START;
        update_client::BlockDeviceApplication activeApplication(
            flashIAPBlockDevice, headerAddress, applicationAddress);
        update_client::UCErrorCode rc = activeApplication.checkApplication();
        if (update_client::UCErrorCode::UC_ERR_NONE != rc) {
            tr_error(" Active application is not valid: %d", rc);
        } else {
            tr_debug(" Active application is valid");
        }

        // search for available firmwares
        const mbed::bd_size_t headerSize = POST_APPLICATION_ADDR - HEADER_ADDR;
        tr_debug(" Header size is %" PRIu64 "", headerSize);
        update_client::CandidateApplications candidateApplications(
            flashIAPBlockDevice,
            MBED_CONF_UPDATE_CLIENT_STORAGE_ADDRESS,
            MBED_CONF_UPDATE_CLIENT_STORAGE_SIZE,
            headerSize,
            MBED_CONF_UPDATE_CLIENT_STORAGE_LOCATIONS);

        uint32_t newestSlotIndex = 0;
        if (candidateApplications.hasValidNewerApplication(activeApplication,
                                                           newestSlotIndex)) {
            tr_debug("Application at slot %" PRIu32 " is newer", newestSlotIndex);
            // there exists a valid newer application
            // compare the active application with the newest valid candidate
            activeApplication.compareTo(
                candidateApplications.getBlockDeviceApplication(newestSlotIndex));

            // install the newest candidate
            rc = candidateApplications.installApplication(newestSlotIndex, headerAddress);
            if (update_client::UCErrorCode::UC_ERR_NONE != rc) {
                tr_error(
                    " Could not install application at slot %d: %d", newestSlotIndex, rc);
            }
            tr_debug(" Application at slot %d installed as active application",
                     newestSlotIndex);

            // compare again the active application and check it
            tr_debug(
                " Comparing the active application with the newly installed one -> "
                "should be identical");
            activeApplication.compareTo(
                candidateApplications.getBlockDeviceApplication(newestSlotIndex));
        }
        err = flashIAPBlockDevice.deinit();
        if (0 != err) {
            tr_error("Cannot initialize block device: %d", err);
        }
    } else {
        tr_error("Blockdevice init failed: %d", err);
    }

    // at this stage we directly branch to the main application
    void* sp = *((void**)POST_APPLICATION_ADDR + 0);  // NOLINT(readability/casting)
    void* pc = *((void**)POST_APPLICATION_ADDR + 1);  // NOLINT(readability/casting)
    tr_debug(" Starting application at address 0x%08x (sp 0x%08x, pc 0x%08x)",
             POST_APPLICATION_ADDR,
             (uint32_t)sp,
             (uint32_t)pc);

    mbed_start_application(POST_APPLICATION_ADDR);

    return 0;
}
