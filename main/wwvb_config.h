/*
 * WWVB Emulator Configuration Header
 * 
 * Common configuration macros and constants shared across modules
 */

#ifndef WWVB_CONFIG_H
#define WWVB_CONFIG_H

// Debug configuration
// Define WWVBDEBUG to enable debug output
// Can be overridden by CMake build configuration: -DWWVBDEBUG
#ifndef WWVBDEBUG
#define WWVBDEBUG
#endif

#endif // WWVB_CONFIG_H
