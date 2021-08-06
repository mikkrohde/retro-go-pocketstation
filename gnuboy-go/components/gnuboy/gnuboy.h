#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#define MESSAGE_ERROR(x, ...) printf("!! %s: " x, __func__, ## __VA_ARGS__)
#define MESSAGE_INFO(x, ...) printf("%s: " x, __func__, ## __VA_ARGS__)
// #define MESSAGE_DEBUG(x, ...) printf("> %s: " x, __func__, ## __VA_ARGS__)
#define MESSAGE_DEBUG(x...) {}

typedef uint8_t byte;
typedef uint8_t un8;
typedef uint16_t un16;
typedef uint32_t un32;
typedef int8_t n8;
typedef int16_t n16;
typedef int32_t n32;
typedef uint16_t word;
typedef unsigned int addr_t; // Most efficient but at least 16 bits

bool gnuboy_init(int sample_rate, bool stereo);
void gnuboy_deinit(void);
void gnuboy_reset(bool hard);
void gnuboy_run(bool draw);
void gnuboy_panic(const char *fmt, ...);

int  gnuboy_load_rom(const char *file, bool preload);
void gnuboy_free_rom(void);
int  gnuboy_load_bios(const char *file);
void gnuboy_free_bios(void);

void gnuboy_set_pad(uint32_t new_pad);
void gnuboy_set_btn(int btn, bool pressed);
void gnuboy_set_rtc(time_t epoch);
void gnuboy_set_pal(int palette);
