#ifndef WWVB_CODEC_H
#define WWVB_CODEC_H

#include <stdbool.h>
#include <stdint.h>

uint16_t BitsEncoder(uint16_t n);
void encodeYear(uint16_t year, uint8_t *signal);
void encodeDayOfYear(uint16_t dayOfYear, uint8_t *signal);
void encodeHour(uint8_t hour, uint8_t *signal);
void encodeMinute(uint8_t minute, uint8_t *signal);
void setMarkersAndIndicators(uint8_t *signal);
void setDUT1(uint8_t *signal);
void setLeapYear(uint16_t year, uint8_t *signal);
void setLeapSecond(bool IsLeap, uint8_t *signal);
void setDST(bool IsDST, uint8_t *signal);

#endif
