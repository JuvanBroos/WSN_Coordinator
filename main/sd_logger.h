#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "protocol.h"
#include <stdbool.h>

/**
 * @brief Initialize the SD Logger
 *        Attempts to mount the SD card via SPI.
 *        If successful, starts the SD logger task.
 * 
 * @param host_id The SPI host ID (e.g. SPI2_HOST or SPI3_HOST) to attach the SD card to.
 * @return true if initialized and mounted successfully, false otherwise.
 */
bool sd_logger_init(int host_id);

/**
 * @brief Queue a report to be written to the SD card.
 * 
 * @param report The serial report to log.
 */
void sd_logger_queue_report(serial_report_t *report);

#endif // SD_LOGGER_H
