#ifndef ECHO_SSD1306_H
#define ECHO_SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#define SSD1306_WIDTH  128U
#define SSD1306_HEIGHT 64U
#define SSD1306_PAGES  8U

typedef struct {
    uint32_t init_attempt_count;
    uint32_t init_success_count;
    uint32_t refresh_count;
    uint32_t refresh_fail_count;
    uint32_t command_fail_count;
    uint8_t address;
    uint8_t online;
    uint16_t reserved;
} ssd1306_diagnostics_t;

extern volatile ssd1306_diagnostics_t g_ssd1306_diag;

bool Ssd1306_Init(void);
void Ssd1306_Clear(void);
void Ssd1306_DrawText(uint8_t x, uint8_t page, const char *text);
bool Ssd1306_Refresh(void);
bool Ssd1306_IsOnline(void);
uint8_t Ssd1306_GetAddress(void);
void Ssd1306_MarkOffline(void);

#endif
