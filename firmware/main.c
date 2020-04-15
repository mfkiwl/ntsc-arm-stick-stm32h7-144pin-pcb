#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <stm32f4xx_hal.h>

#include "ff.h"

#include "defs.h"
#include "delay.h"
#include "leds.h"
#include "byte_queue.h"
#include "gpio_helpers.h"
#include "utility.h"
#include "crc7.h"
#include "logprintf.h"

#include "monitor_queue.h"
#include "console_queue.h"
#include "uart.h"
#include "sd_spi.h"
#include "keyboard.h"
#include "reset_button.h"

// System APIs
#include "videomode.h"
#include "graphics.h"
#include "rocinante.h"

// System driver internal definitions
#include "videomodeinternal.h"

static int gDumpKeyboardData = 0;

char gMonitorCommandBuffer[80];
unsigned char gMonitorCommandBufferLength = 0;

#define IOBOARD_FIRMWARE_VERSION_STRING XSTR(IOBOARD_FIRMWARE_VERSION)

volatile unsigned char gSerialInputToMonitor = 1;

void panic_worse()
{
    LED_set_panic(1);
    for(;;);
}

void panic(void)
{
    static int entered = 0;

    LED_set_panic(1);

    int pin = 0;
    for(;;) {
        if(!entered) {
            // SERIAL_flush() can itself panic(), so stop reentry here
            entered = 1;
            SERIAL_flush();
            entered = 0;
        }

        LED_set_panic(pin);
        pin = pin ? 0 : 1;
        delay_ms(100);
    }
}

//----------------------------------------------------------------------------
// System Initialization Goop

// DMA_BEATS should be SystemCoreClock / 14318180.0
// need main clock as close as possible to @ 171.816 if DMA_BEATS is 12

typedef struct {
    float CPUFreq;      // not used; computed
    uint32_t HSE;       // not used; fixed
    uint32_t PLL_M;
    uint32_t PLL_N;
    uint32_t PLL_P;
    uint32_t DMA_BEATS;
    float colorburstClock;      // not used; computed
    float error;      // not used; computed
} ClockConfiguration;

unsigned int whichConfig = 0;
static const ClockConfiguration clockConfigs[] =
{
    // Base mode we know works
    {200.47, 16000000, 17, 426, 2, 14, 3.579832, 0.000080},
    // {214.77, 16000000, 13, 349, 2, 15, 3.579487, -0.000016},

    {157.50, 16000000, 16, 315, 2, 11, 3.579545, 0.000000},
    {114.55, 16000000, 22, 315, 2, 8, 3.579545, 0.000000},
    {186.13, 16000000, 15, 349, 2, 13, 3.579487, -0.000016},
    {214.77, 16000000, 13, 349, 2, 15, 3.579487, -0.000016},
    {100.24, 16000000, 17, 213, 2, 7, 3.579832, 0.000080},
    {200.47, 16000000, 17, 426, 2, 14, 3.579832, 0.000080},
    {100.24, 16000000, 34, 426, 2, 7, 3.579832, 0.000080},
    {143.20, 16000000, 10, 179, 2, 10, 3.580000, 0.000127},
    {143.20, 16000000, 20, 358, 2, 10, 3.580000, 0.000127},
    {114.56, 16000000, 25, 358, 2, 8, 3.580000, 0.000127},
    {100.21, 16000000, 19, 238, 2, 7, 3.578947, -0.000167},
    {114.53, 16000000, 19, 272, 2, 8, 3.578947, -0.000167},
    {128.84, 16000000, 19, 306, 2, 9, 3.578947, -0.000167},
    {143.16, 16000000, 19, 340, 2, 10, 3.578947, -0.000167},
    {157.47, 16000000, 19, 374, 2, 11, 3.578947, -0.000167},
    {171.79, 16000000, 19, 408, 2, 12, 3.578947, -0.000167},
    {128.89, 16000000, 9, 145, 2, 9, 3.580247, 0.000196},
    {128.89, 16000000, 18, 290, 2, 9, 3.580247, 0.000196},
    {200.50, 16000000, 16, 401, 2, 14, 3.580357, 0.000227},
    {100.25, 16000000, 32, 401, 2, 7, 3.580357, 0.000227},
    {114.57, 16000000, 28, 401, 2, 8, 3.580357, 0.000227},
    {186.18, 16000000, 11, 256, 2, 13, 3.580420, 0.000244},
    {157.54, 16000000, 13, 256, 2, 11, 3.580420, 0.000244},
    {171.76, 16000000, 17, 365, 2, 12, 3.578431, -0.000311},
    {100.19, 16000000, 21, 263, 2, 7, 3.578231, -0.000367},
    {143.24, 16000000, 21, 376, 2, 10, 3.580952, 0.000393},
    {214.86, 16000000, 7, 188, 2, 15, 3.580952, 0.000393},
    {214.86, 16000000, 14, 376, 2, 15, 3.580952, 0.000393},
    {100.27, 16000000, 15, 188, 2, 7, 3.580952, 0.000393},
    {200.53, 16000000, 15, 376, 2, 14, 3.580952, 0.000393},
    {100.27, 16000000, 30, 376, 2, 7, 3.580952, 0.000393},
    {114.50, 16000000, 16, 229, 2, 8, 3.578125, -0.000397},
    {186.22, 16000000, 18, 419, 2, 13, 3.581197, 0.000461},
    {128.92, 16000000, 26, 419, 2, 9, 3.581197, 0.000461},
    {128.80, 16000000, 10, 161, 2, 9, 3.577778, -0.000494},
    {171.73, 16000000, 15, 322, 2, 12, 3.577778, -0.000494},
    {128.80, 16000000, 20, 322, 2, 9, 3.577778, -0.000494},
    {214.67, 16000000, 6, 161, 2, 15, 3.577778, -0.000494},
    {143.11, 16000000, 9, 161, 2, 10, 3.577778, -0.000494},
    {214.67, 16000000, 12, 322, 2, 15, 3.577778, -0.000494},
    {143.11, 16000000, 18, 322, 2, 10, 3.577778, -0.000494},
    {100.17, 16000000, 23, 288, 2, 7, 3.577640, -0.000532},
    {114.48, 16000000, 29, 415, 2, 8, 3.577586, -0.000547},
    {200.57, 16000000, 14, 351, 2, 14, 3.581633, 0.000583},
    {100.29, 16000000, 28, 351, 2, 7, 3.581633, 0.000583},
    {128.94, 16000000, 17, 274, 2, 9, 3.581699, 0.000602},
    {157.60, 16000000, 10, 197, 2, 11, 3.581818, 0.000635},
    {157.60, 16000000, 20, 394, 2, 11, 3.581818, 0.000635},
    {143.27, 16000000, 11, 197, 2, 10, 3.581818, 0.000635},
    {143.27, 16000000, 22, 394, 2, 10, 3.581818, 0.000635},
};
static const unsigned int clockConfigCount = sizeof(clockConfigs) / sizeof(clockConfigs[0]);

static void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_OscInitTypeDef RCC_OscInitStruct;

  /* Enable Power Control clock */
  __HAL_RCC_PWR_CLK_ENABLE();

  /* The voltage scaling allows optimizing the power consumption when the device is 
     clocked below the maximum system frequency, to update the voltage scaling value 
     regarding system frequency refer to product datasheet.  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  int PLL_M = clockConfigs[whichConfig].PLL_M;
  int PLL_N = clockConfigs[whichConfig].PLL_N;
  int PLL_P = clockConfigs[whichConfig].PLL_P;

  unsigned int PLL_Q = (16000000 / PLL_M * PLL_N / 2 / 24 + 999999) / 1000000;

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = PLL_M; // Divide HSE by this
  RCC_OscInitStruct.PLL.PLLN = PLL_N; // Then multiply by this 
  RCC_OscInitStruct.PLL.PLLP = PLL_P; // Then divide by this
  RCC_OscInitStruct.PLL.PLLQ = PLL_Q; // Divide by this for SD, USB OTG FS, and some other peripherals
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    panic();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4; // APB1 clock
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2; // APB2 clock
  // grantham - 5 cycles for 168MHz is stated in Table 10 in the STM32F4 reference manual
  if(HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    panic();
  }

  // 415 comes up by default with D$ and I$ enabled and prefetch enabled, or HAL_Init sets them
  // FLASH->ACR |= FLASH_ACR_PRFTEN
  // FLASH->ACR &= ~FLASH_ACR_PRFTEN;

  /* Enable other CLKs */
  __HAL_RCC_TIM1_CLK_ENABLE();
  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_RCC_TIM4_CLK_ENABLE();
  __HAL_RCC_TIM5_CLK_ENABLE();
  __HAL_RCC_TIM6_CLK_ENABLE();
  __HAL_RCC_TIM7_CLK_ENABLE();
  __HAL_RCC_TIM8_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // Since this relies solely on the clock 
    delay_init();
}

void DeInitRCCAndPLL()
{
    // Unset the RCC and reset it
    // HAL_RCC_DeInit(); is EMPTY. WTF.
    RCC->CR |= RCC_HSI_ON;
    RCC->CFGR = 0x00000000U;
    uint32_t vl_mask = 0xFFFFFFFFU;
    /* Reset HSEON, PLLSYSON bits */
    CLEAR_BIT(vl_mask, (RCC_CR_HSEON | RCC_CR_HSEBYP | RCC_CR_PLLON | RCC_CR_CSSON));
    RCC->CR &= vl_mask;
    RCC->CR = (RCC->CR & ~RCC_CR_HSITRIM_Msk) | (0x10 << RCC_CR_HSITRIM_Pos); /* Set HSITRIM bits to the reset value*/
    RCC->PLLCFGR = RCC_PLLCFGR_RST_VALUE; /* Reset PLLCFGR register */
    RCC->CR &= ~RCC_CR_HSEBYP; /* Reset HSEBYP bit */
  /* Disable all interrupts */
    RCC->CIR = 0x00000000U;
// *            - HSI ON and used as system clock source
// *            - HSE and PLL OFF
// *            - AHB, APB1 and APB2 prescaler set to 1.
// *            - CSS, MCO1 and MCO2 OFF
// *            - All interrupts disabled
}

//----------------------------------------------------------------------------
// stdio

#define OUTPUT_TO_SERIAL        0x01
#define OUTPUT_TO_VIDEO         0x02

int gOutputDevices = OUTPUT_TO_SERIAL;

void VIDEO_putchar(char c);

void __io_putchar( char c )
{
    if(gOutputDevices & OUTPUT_TO_SERIAL)
        SERIAL_enqueue_one_char(c);
    if(gOutputDevices & OUTPUT_TO_VIDEO)
        VIDEO_putchar(c);
}

void errorchar(char c)
{
    SERIAL_enqueue_one_char(c);
}

void errorchar_flush()
{
    SERIAL_flush();
}

//----------------------------------------------------------------------------
// File operations

FATFS gFATVolume;

unsigned char sd_buffer[SD_BLOCK_SIZE];

int __attribute__((section (".ccmram"))) markHandlerInSamples = 0;

uint32_t __attribute__((section (".ccmram"))) rowCyclesSpent[262];
uint32_t __attribute__((section (".ccmram"))) DMAFIFOUnderruns = 0;
uint32_t __attribute__((section (".ccmram"))) DMATransferErrors = 0;
typedef enum { VIDEO_COLOR_TEST, VIDEO_GRAYSCALE, VIDEO_PALETTIZED, VIDEO_SCAN_TEST, VIDEO_MODE} VideoMode;
VideoMode __attribute__((section (".ccmram"))) videoMode = VIDEO_COLOR_TEST;
int __attribute__((section (".ccmram"))) videoScanTestLeft = 200;
int __attribute__((section (".ccmram"))) videoScanTestRight = 700;
int __attribute__((section (".ccmram"))) videoScanTestTop = 50;
int __attribute__((section (".ccmram"))) videoScanTestBottom = 200;

void check_exceptional_conditions()
{
    if(gConsoleOverflowed) {
        logprintf(DEBUG_WARNINGS, "WARNING: Console input queue overflow\n");
        gConsoleOverflowed = 0;
    }

    if(gKeyboardOverflowed) {
        logprintf(DEBUG_WARNINGS, "WARNING: Keyboard data queue overflow\n");
        gKeyboardOverflowed = 0;
    }

    if(gKeyboardParityError) {
        logprintf(DEBUG_WARNINGS, "WARNING: Keyboard data parity error, received 0x%02X\n", gKeyboardParityError - 256);
        gKeyboardParityError = 0;
    }

    if(gKeyboardBATBadParity) {
        logprintf(DEBUG_EVENTS, "EVENT: Received initial BAT with parity error from PS/2 keyboard\n");
        gKeyboardBATBadParity = 0;
    }
}

void process_local_key(unsigned char c);

void process_monitor_queue()
{
    unsigned char isEmpty = queue_isempty(&mon_queue.q);
    static unsigned char escapeBackToMonitor = 0;

    if(!isEmpty) {
        unsigned char c = queue_deq(&mon_queue.q);
        if(gSerialInputToMonitor)
            process_local_key(c);
        else {
            if(escapeBackToMonitor == 0 && c == 1)
                escapeBackToMonitor = 1;
            else if(escapeBackToMonitor != 0 && c == 2) {
                escapeBackToMonitor = 0;
                gSerialInputToMonitor = 1;
                printf("Serial input returned to monitor\n");
            } else {
                escapeBackToMonitor = 0;
                disable_interrupts();
                console_enqueue_key_unsafe(c);
                enable_interrupts();
            }
        }
    }
}

void uart_received(char c)
{
    queue_enq(&mon_queue.q, c);
}

void console_queue_init()
{
    queue_init(&con_queue.q, CON_QUEUE_CAPACITY);
}

//----------------------------------------------------------------------------
// DAC

#define DAC_VALUE_LIMIT 0xF0

#define MAX_DAC_VOLTAGE 1.22f

unsigned char voltageToDACValue(float voltage)
{
    if(voltage < 0.0f) {
        return 0x0;
    }
    uint32_t value = (uint32_t)(voltage / MAX_DAC_VOLTAGE * 255);
    if(value >= DAC_VALUE_LIMIT) {
        return DAC_VALUE_LIMIT;
    }
    return value;
}

//----------------------------------------------------------------------------
// NTSC Video goop


// Number of samples we target, 4x colorburst yields 227.5 cycles, or 910 samples at 14.318180MHz
// But we cheat and actually scan out 912 cycles to be a multiple
// of 16, so lines are .22% too long.  We hope the receiver is permissive
// enough to ignore this.  My cheap Orion is.

#define ROW_SAMPLES        912

#define NTSC_COLORBURST_FREQUENCY       3579545
#define NTSC_EQPULSE_LINES	3
#define NTSC_VSYNC_LINES	3
#define NTSC_VBLANK_LINES	9
#define NTSC_FIELD_LINES	262
#define NTSC_PIXEL_LINES	(NTSC_FIELD_LINES - NTSC_VBLANK_LINES - NTSC_VSYNC_LINES - NTSC_EQPULSE_LINES * 2)
#define NTSC_EQ_PULSE_INTERVAL	.04
#define NTSC_VSYNC_BLANK_INTERVAL	.43
#define NTSC_HOR_SYNC_DUR	.075
#define NTSC_FIELDS		59.94
#define NTSC_FRONTPORCH		.02
/* BACKPORCH including COLORBURST */
#define NTSC_BACKPORCH		.075
#define NTSC_COLORBURST_CYCLES  10

#define NTSC_SYNC_TIP_VOLTAGE   0.0f
#define NTSC_SYNC_PORCH_VOLTAGE   .285f
#define NTSC_SYNC_BLACK_VOLTAGE   .339f
#define NTSC_SYNC_WHITE_VOLTAGE   1.0f  /* VCR had .912v */

// These are in CCM to reduce contention with SRAM1 during DMA 
unsigned char __attribute__((section (".ccmram"))) NTSCEqSyncPulseLine[ROW_SAMPLES];
unsigned char __attribute__((section (".ccmram"))) NTSCVsyncLine[ROW_SAMPLES];
unsigned char __attribute__((section (".ccmram"))) NTSCBlankLine[ROW_SAMPLES];

unsigned char __attribute__((section (".ccmram"))) NTSCSyncTip;
unsigned char __attribute__((section (".ccmram"))) NTSCSyncPorch;
unsigned char __attribute__((section (".ccmram"))) NTSCBlack;
unsigned char __attribute__((section (".ccmram"))) NTSCWhite;
unsigned char __attribute__((section (".ccmram"))) NTSCMaxAllowed;

int NTSCEqPulseClocks;
int NTSCVSyncClocks;
int NTSCHSyncClocks;
int NTSCLineClocks;
int NTSCFieldClocks;
int NTSCFrontPorchClocks;
int NTSCBackPorchClocks;

// Essentially the remainder of CCMRAM, will need to be shrunk if more goes into CCM
#define IMGBUFFER_SIZE 53248
// (Following was copied from paper chicken scratches)
// On Orion TV, one 14MHz clock is .0225 inches, one 240p row is .052 inches.
// So to find a close value for width, width = sqrt(4 / 3 * .052 / .0225 * IMGBUFFER_SIZE)
// Then, height should be no more than width * 3 * .0225 / (4 / .052)
// So for 53248, a reasonable 4:3 framebuffer is 400x128
// 4:3 aspect would be 1.333
// 400 wide would be 9 inches, and 128 high would be 6.656 inches, and that's 1.352, so it's not too bad
unsigned char __attribute__((section (".ccmram"))) imgBuffer[IMGBUFFER_SIZE];

#define MAX_PALETTE_ENTRIES 254
#define PALETTE_WHITE 255
#define PALETTE_BLACK 254

uint32_t __attribute__((section (".ccmram"))) paletteUInt32[2][256];
unsigned char __attribute__((section (".ccmram"))) rowPalette[256];

unsigned char *imgBufferRow(int row) { return imgBuffer + row * 200; } // XXX this may not fit!
unsigned char *imgBufferPixel(int x, int y) { return imgBufferRow(y) + x; }

// XXX these are in SRAM2 to reduce contention with SRAM1 during DMA
#define SWATCH_SIZE     1
unsigned char __attribute__((section (".sram2"))) row0[ROW_SAMPLES * SWATCH_SIZE];
unsigned char __attribute__((section (".sram2"))) row1[ROW_SAMPLES * SWATCH_SIZE];
unsigned char __attribute__((section (".sram2"))) audio0[512];
unsigned char __attribute__((section (".sram2"))) audio1[512];

unsigned char NTSCYIQToDAC(float y, float i, float q, float tcycles)
{
// This is transcribed from the NTSC spec, double-checked.
    float wt = tcycles * M_PI * 2;
    float sine = sinf(wt + 33.0f / 180.0f * M_PI);
    float cosine = cosf(wt + 33.0f / 180.0f * M_PI);
    float signal = y + q * sine + i * cosine;
// end of transcription

    if(signal < -1.0f) { signal = -1.0f; }
    if(signal > 1.0f) { signal = 1.0f; }

    return NTSCBlack + signal * (NTSCWhite - NTSCBlack);
}

uint32_t NTSCYIQToUInt32(float y, float i, float q)
{
    unsigned char b0 = NTSCYIQToDAC(y, i, q,  .0f);
    unsigned char b1 = NTSCYIQToDAC(y, i, q, .25f);
    unsigned char b2 = NTSCYIQToDAC(y, i, q, .50f);
    unsigned char b3 = NTSCYIQToDAC(y, i, q, .75f);

    return (b0 << 0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

unsigned char NTSCColorburst0;
unsigned char NTSCColorburst90;
unsigned char NTSCColorburst180;
unsigned char NTSCColorburst270;

void NTSCCalculateParameters(float clock)
{
    // Calculate values for a scanline
    NTSCFieldClocks = floorf(clock * 1000000.0 / NTSC_FIELDS + 0.5);
    // NTSCLineClocks = floorf((double)NTSCFieldClocks / NTSC_FIELD_LINES + 0.5);
    NTSCLineClocks = ROW_SAMPLES;
    NTSCHSyncClocks = floorf(NTSCLineClocks * NTSC_HOR_SYNC_DUR + 0.5);
    NTSCFrontPorchClocks = NTSCLineClocks * NTSC_FRONTPORCH;
    NTSCBackPorchClocks = NTSCLineClocks * NTSC_BACKPORCH;
    NTSCEqPulseClocks = NTSCLineClocks * NTSC_EQ_PULSE_INTERVAL;
    NTSCVSyncClocks = NTSCLineClocks * NTSC_VSYNC_BLANK_INTERVAL;

    NTSCSyncTip = voltageToDACValue(NTSC_SYNC_TIP_VOLTAGE);
    NTSCSyncPorch = voltageToDACValue(NTSC_SYNC_PORCH_VOLTAGE);
    NTSCBlack = voltageToDACValue(NTSC_SYNC_BLACK_VOLTAGE);
    NTSCWhite = voltageToDACValue(NTSC_SYNC_WHITE_VOLTAGE);
    NTSCMaxAllowed = 255;

    // Calculate the four values for the colorburst that we'll repeat to make a wave
    NTSCColorburst0 = NTSCSyncPorch;
    NTSCColorburst90 = NTSCSyncPorch - .6 * NTSCSyncPorch;
    NTSCColorburst180 = NTSCSyncPorch;
    NTSCColorburst270 = NTSCSyncPorch + .6 * NTSCSyncPorch;

    // System reserved palette entries
    paletteUInt32[1][255] = paletteUInt32[0][255] = 
        (NTSCWhite <<  0) |
        (NTSCWhite <<  8) |
        (NTSCWhite << 16) |
        (NTSCWhite << 24);
    paletteUInt32[1][254] = paletteUInt32[0][254] = 
        (NTSCBlack <<  0) |
        (NTSCBlack <<  8) |
        (NTSCBlack << 16) |
        (NTSCBlack << 24);

    memset(rowPalette, 0, sizeof(rowPalette));

    memset(row0, NTSCSyncPorch, sizeof(row0));
    memset(row1, NTSCSyncPorch, sizeof(row1));
}

void NTSCFillEqPulseLine(unsigned char *rowBuffer)
{
    for (int col = 0; col < NTSCLineClocks; col++) {
        if (col < NTSCEqPulseClocks || (col > NTSCLineClocks/2 && col < NTSCLineClocks/2 + NTSCEqPulseClocks)) {
            rowBuffer[col] = NTSCSyncTip;
        } else {
            rowBuffer[col] = NTSCSyncPorch;
        }
    }
}

void NTSCFillVSyncLine(unsigned char *rowBuffer)
{
    for (int col = 0; col < NTSCLineClocks; col++) {
        if (col < NTSCVSyncClocks || (col > NTSCLineClocks/2 && col < NTSCLineClocks/2 + NTSCVSyncClocks)) {
            rowBuffer[col] = NTSCSyncTip;
        } else {
            rowBuffer[col] = NTSCSyncPorch;
        }
    }
}

// Haven't accelerated because not yet done in scan ISR
void NTSCAddColorburst(unsigned char *rowBuffer)
{
    static const int startOfColorburstClocks = 80; // XXX magic number for current clock

    for(int col = startOfColorburstClocks; col < startOfColorburstClocks + NTSC_COLORBURST_CYCLES * 4; col++) {
        switch((col - startOfColorburstClocks) % 4) {
            case 0: rowBuffer[col] = NTSCColorburst0; break;
            case 1: rowBuffer[col] = NTSCColorburst90; break;
            case 2: rowBuffer[col] = NTSCColorburst180; break;
            case 3: rowBuffer[col] = NTSCColorburst270; break;
        }
    }
}

void NTSCFillBlankLine(unsigned char *rowBuffer, int withColorburst)
{
    for (int col = 0; col < NTSCLineClocks; col++) {
        if (col < NTSCHSyncClocks) {
            rowBuffer[col] = NTSCSyncTip;
        } else {
            rowBuffer[col] = NTSCSyncPorch;
        }
    }
    if(withColorburst) {
        NTSCAddColorburst(rowBuffer);
    }
}

void NTSCGenerateLineBuffers()
{
    // one line = (1 / 3579545) * (455/2)

    // front porch is (.165) * (1 / 15734) / (1 / 3579545) = 37.53812921062726565701 cycles (37.5)
    //     74 cycles at double clock
    // pixels is (1 - .165) * (1 / 15734) / (1 / 3579545) = 189.96568418711380557696 cycles (190)
    //     280 cycles at double clock

    NTSCFillEqPulseLine(NTSCEqSyncPulseLine);
    NTSCFillVSyncLine(NTSCVsyncLine);
    NTSCFillBlankLine(NTSCBlankLine, 1);
}

int NTSCSetRowPalette(int row, int palette)
{
    rowPalette[row] = palette;
    return 0;
}

//----------------------------------------------------------------------------
// Video modes 

#define font8x8Width 8
#define font8x8Height 8

unsigned char /* __attribute__((section (".ccmram"))) */ font8x8Bits[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x12, 0x10,
    0x7C, 0x10, 0x12, 0x7C, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x00, 0x04, 0x08, 0x3C, 0x42, 0x7E, 0x40, 0x3C, 0x00, 0x24,
    0x00, 0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x08, 0x14, 0x08, 0x14,
    0x22, 0x3E, 0x22, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x02, 0x02, 0x00,
    0x00, 0x14, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x1D, 0x22,
    0x26, 0x2A, 0x32, 0x22, 0x5C, 0x00, 0x10, 0x08, 0x42, 0x42, 0x42,
    0x46, 0x3A, 0x00, 0x32, 0x4C, 0x00, 0x2C, 0x32, 0x22, 0x22, 0x00,
    0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x38,
    0x04, 0x3C, 0x44, 0x3A, 0x00, 0x3E, 0x7A, 0x44, 0x44, 0x78, 0x48,
    0x44, 0x00, 0x14, 0x00, 0x08, 0x14, 0x22, 0x3E, 0x22, 0x00, 0x32,
    0x4C, 0x08, 0x14, 0x22, 0x3E, 0x22, 0x00, 0x32, 0x4C, 0x22, 0x32,
    0x2A, 0x26, 0x22, 0x00, 0x00, 0x14, 0x1C, 0x22, 0x22, 0x22, 0x1C,
    0x00, 0x09, 0x16, 0x26, 0x2A, 0x32, 0x34, 0x48, 0x00, 0x32, 0x4C,
    0x00, 0x3C, 0x42, 0x42, 0x3C, 0x00, 0x3C, 0x22, 0x22, 0x3C, 0x22,
    0x22, 0x7C, 0x00, 0x24, 0x00, 0x42, 0x42, 0x42, 0x46, 0x3A, 0x00,
    0x32, 0x4C, 0x00, 0x18, 0x24, 0x24, 0x18, 0x00, 0x1C, 0x2A, 0x0A,
    0x1C, 0x28, 0x2A, 0x1C, 0x00, 0x28, 0x00, 0x38, 0x04, 0x3C, 0x44,
    0x3A, 0x00, 0x20, 0x10, 0x38, 0x04, 0x3C, 0x44, 0x3A, 0x00, 0x10,
    0x00, 0x38, 0x04, 0x3C, 0x44, 0x3A, 0x00, 0x3C, 0x40, 0x7C, 0x42,
    0x3E, 0x02, 0x3C, 0x00, 0x04, 0x08, 0x3E, 0x20, 0x3E, 0x20, 0x3E,
    0x00, 0x08, 0x1E, 0x24, 0x26, 0x3C, 0x24, 0x26, 0x00, 0x1C, 0x22,
    0x20, 0x20, 0x22, 0x1C, 0x08, 0x10, 0x00, 0x00, 0x32, 0x4C, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x08, 0x00, 0x24, 0x24, 0x24,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x24, 0x7E, 0x24, 0x7E, 0x24,
    0x24, 0x00, 0x08, 0x1E, 0x28, 0x1C, 0x0A, 0x3C, 0x08, 0x00, 0x00,
    0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x30, 0x48, 0x48, 0x30,
    0x4A, 0x44, 0x3A, 0x00, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04, 0x00, 0x20, 0x10,
    0x08, 0x08, 0x08, 0x10, 0x20, 0x00, 0x08, 0x2A, 0x1C, 0x3E, 0x1C,
    0x2A, 0x08, 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x10, 0x00, 0x00, 0x00,
    0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x3C,
    0x42, 0x46, 0x5A, 0x62, 0x42, 0x3C, 0x00, 0x08, 0x18, 0x28, 0x08,
    0x08, 0x08, 0x3E, 0x00, 0x3C, 0x42, 0x02, 0x0C, 0x30, 0x40, 0x7E,
    0x00, 0x3C, 0x42, 0x02, 0x1C, 0x02, 0x42, 0x3C, 0x00, 0x04, 0x0C,
    0x14, 0x24, 0x7E, 0x04, 0x04, 0x00, 0x7E, 0x40, 0x78, 0x04, 0x02,
    0x44, 0x38, 0x00, 0x1C, 0x20, 0x40, 0x7C, 0x42, 0x42, 0x3C, 0x00,
    0x7E, 0x42, 0x04, 0x08, 0x10, 0x10, 0x10, 0x00, 0x3C, 0x42, 0x42,
    0x3C, 0x42, 0x42, 0x3C, 0x00, 0x3C, 0x42, 0x42, 0x3E, 0x02, 0x04,
    0x38, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x08, 0x08, 0x10, 0x06, 0x0C, 0x18, 0x30,
    0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00,
    0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x3C, 0x42,
    0x02, 0x0C, 0x10, 0x00, 0x10, 0x00, 0x1C, 0x22, 0x4A, 0x56, 0x4C,
    0x20, 0x1E, 0x00, 0x18, 0x24, 0x42, 0x7E, 0x42, 0x42, 0x42, 0x00,
    0x7C, 0x22, 0x22, 0x3C, 0x22, 0x22, 0x7C, 0x00, 0x1C, 0x22, 0x40,
    0x40, 0x40, 0x22, 0x1C, 0x00, 0x78, 0x24, 0x22, 0x22, 0x22, 0x24,
    0x78, 0x00, 0x7E, 0x40, 0x40, 0x78, 0x40, 0x40, 0x7E, 0x00, 0x7E,
    0x40, 0x40, 0x78, 0x40, 0x40, 0x40, 0x00, 0x1C, 0x22, 0x40, 0x4E,
    0x42, 0x22, 0x1C, 0x00, 0x42, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x42,
    0x00, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x1C, 0x00, 0x0E, 0x04,
    0x04, 0x04, 0x04, 0x44, 0x38, 0x00, 0x42, 0x44, 0x48, 0x70, 0x48,
    0x44, 0x42, 0x00, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7E, 0x00,
    0x42, 0x66, 0x5A, 0x5A, 0x42, 0x42, 0x42, 0x00, 0x42, 0x62, 0x52,
    0x4A, 0x46, 0x42, 0x42, 0x00, 0x3C, 0x42, 0x42, 0x42, 0x42, 0x42,
    0x3C, 0x00, 0x7C, 0x42, 0x42, 0x7C, 0x40, 0x40, 0x40, 0x00, 0x3C,
    0x42, 0x42, 0x42, 0x4A, 0x44, 0x3A, 0x00, 0x7C, 0x42, 0x42, 0x7C,
    0x48, 0x44, 0x42, 0x00, 0x3C, 0x42, 0x40, 0x3C, 0x02, 0x42, 0x3C,
    0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x42, 0x42,
    0x42, 0x42, 0x42, 0x42, 0x3C, 0x00, 0x42, 0x42, 0x42, 0x24, 0x24,
    0x18, 0x18, 0x00, 0x42, 0x42, 0x42, 0x5A, 0x5A, 0x66, 0x42, 0x00,
    0x42, 0x42, 0x24, 0x18, 0x24, 0x42, 0x42, 0x00, 0x22, 0x22, 0x22,
    0x1C, 0x08, 0x08, 0x08, 0x00, 0x7E, 0x02, 0x04, 0x18, 0x20, 0x40,
    0x7E, 0x00, 0x3C, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3C, 0x00, 0x00,
    0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x3C, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x3C, 0x00, 0x08, 0x14, 0x22, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x10, 0x08,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x04, 0x3C,
    0x44, 0x3A, 0x00, 0x40, 0x40, 0x5C, 0x62, 0x42, 0x62, 0x5C, 0x00,
    0x00, 0x00, 0x3C, 0x42, 0x40, 0x42, 0x3C, 0x00, 0x02, 0x02, 0x3A,
    0x46, 0x42, 0x46, 0x3A, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x7E, 0x40,
    0x3C, 0x00, 0x0C, 0x12, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x00, 0x00,
    0x00, 0x3A, 0x46, 0x46, 0x3A, 0x02, 0x3C, 0x40, 0x40, 0x5C, 0x62,
    0x42, 0x42, 0x42, 0x00, 0x08, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1C,
    0x00, 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x44, 0x38, 0x40, 0x40,
    0x44, 0x48, 0x50, 0x68, 0x44, 0x00, 0x18, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x1C, 0x00, 0x00, 0x00, 0x76, 0x49, 0x49, 0x49, 0x49, 0x00,
    0x00, 0x00, 0x5C, 0x62, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x3C,
    0x42, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x5C, 0x62, 0x62, 0x5C,
    0x40, 0x40, 0x00, 0x00, 0x3A, 0x46, 0x46, 0x3A, 0x02, 0x02, 0x00,
    0x00, 0x5C, 0x62, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x3E, 0x40,
    0x3C, 0x02, 0x7C, 0x00, 0x10, 0x10, 0x7C, 0x10, 0x10, 0x12, 0x0C,
    0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x46, 0x3A, 0x00, 0x00, 0x00,
    0x42, 0x42, 0x42, 0x24, 0x18, 0x00, 0x00, 0x00, 0x41, 0x49, 0x49,
    0x49, 0x36, 0x00, 0x00, 0x00, 0x42, 0x24, 0x18, 0x24, 0x42, 0x00,
    0x00, 0x00, 0x42, 0x42, 0x46, 0x3A, 0x02, 0x3C, 0x00, 0x00, 0x7E,
    0x04, 0x18, 0x20, 0x7E, 0x00, 0x0C, 0x10, 0x10, 0x20, 0x10, 0x10,
    0x0C, 0x00, 0x08, 0x08, 0x08, 0x00, 0x08, 0x08, 0x08, 0x00, 0x30,
    0x08, 0x08, 0x04, 0x08, 0x08, 0x30, 0x00, 0x30, 0x49, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x3E,
    0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x7F, 0x3E, 0x08, 0x08, 0x00, 0x36,
    0x7F, 0x7F, 0x3E, 0x1C, 0x08, 0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E,
    0x1C, 0x08, 0x00, 0x1C, 0x1C, 0x08, 0x6B, 0x7F, 0x6B, 0x08, 0x1C,
    0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C, 0x3C, 0x42, 0xA5,
    0x81, 0x99, 0xA5, 0x42, 0x3C, 0x04, 0x08, 0x10, 0x20, 0x10, 0x08,
    0x04, 0x3C, 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x3C, 0x00,
    0x00, 0x39, 0x46, 0x46, 0x39, 0x00, 0x00, 0x3C, 0x22, 0x3C, 0x22,
    0x22, 0x3C, 0x20, 0x40, 0x61, 0x12, 0x14, 0x18, 0x10, 0x30, 0x30,
    0x00, 0x0C, 0x12, 0x10, 0x0C, 0x0A, 0x12, 0x0C, 0x00, 0x06, 0x08,
    0x10, 0x3E, 0x10, 0x08, 0x06, 0x00, 0x16, 0x06, 0x08, 0x10, 0x1C,
    0x02, 0x0C, 0x00, 0x2C, 0x52, 0x12, 0x12, 0x02, 0x02, 0x02, 0x00,
    0x08, 0x14, 0x22, 0x3E, 0x22, 0x14, 0x08, 0x00, 0x00, 0x20, 0x20,
    0x20, 0x22, 0x22, 0x1C, 0x00, 0x40, 0x48, 0x50, 0x60, 0x50, 0x4A,
    0x44, 0x00, 0x20, 0x10, 0x10, 0x10, 0x18, 0x24, 0x42, 0x00, 0x24,
    0x24, 0x24, 0x24, 0x3A, 0x20, 0x20, 0x00, 0x00, 0x00, 0x32, 0x12,
    0x14, 0x18, 0x10, 0x00, 0x08, 0x1C, 0x20, 0x18, 0x20, 0x1C, 0x02,
    0x0C, 0x00, 0x18, 0x24, 0x42, 0x42, 0x24, 0x18, 0x00, 0x00, 0x00,
    0x3E, 0x54, 0x14, 0x14, 0x14, 0x00, 0x00, 0x18, 0x24, 0x24, 0x38,
    0x20, 0x20, 0x00, 0x00, 0x00, 0x3E, 0x48, 0x48, 0x30, 0x00, 0x00,
    0x00, 0x00, 0x3E, 0x48, 0x08, 0x08, 0x08, 0x08, 0x00, 0x02, 0x64,
    0x24, 0x24, 0x24, 0x18, 0x00, 0x08, 0x1C, 0x2A, 0x2A, 0x2A, 0x1C,
    0x08, 0x00, 0x00, 0x00, 0x62, 0x14, 0x08, 0x14, 0x23, 0x00, 0x49,
    0x2A, 0x2A, 0x1C, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, 0x22, 0x41,
    0x49, 0x49, 0x36, 0x00, 0x1C, 0x22, 0x41, 0x41, 0x63, 0x22, 0x63,
    0x00, 0x1E, 0x10, 0x10, 0x10, 0x50, 0x30, 0x10, 0x00, 0x00, 0x08,
    0x00, 0x3E, 0x00, 0x08, 0x00, 0x00, 0x7E, 0x20, 0x10, 0x0C, 0x10,
    0x20, 0x7E, 0x00, 0x00, 0x32, 0x4C, 0x00, 0x32, 0x4C, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x14, 0x22, 0x7F, 0x00, 0x00, 0x04, 0x08, 0x10,
    0x10, 0x08, 0x08, 0x10, 0x20, 0x01, 0x02, 0x7F, 0x08, 0x7F, 0x20,
    0x40, 0x00, 0x10, 0x08, 0x04, 0x3E, 0x10, 0x08, 0x04, 0x00, 0x3F,
    0x52, 0x24, 0x08, 0x12, 0x25, 0x42, 0x00, 0x1C, 0x22, 0x41, 0x41,
    0x7F, 0x22, 0x22, 0x63, 0x00, 0x00, 0x36, 0x49, 0x49, 0x36, 0x00,
    0x00, 0x00, 0x02, 0x04, 0x48, 0x50, 0x60, 0x40, 0x00, 0x1E, 0x20,
    0x1C, 0x22, 0x1C, 0x02, 0x3C, 0x00, 0x22, 0x55, 0x2A, 0x14, 0x2A,
    0x55, 0x22, 0x00, 0x3C, 0x42, 0x9D, 0xA1, 0xA1, 0x9D, 0x42, 0x3C,
    0x42, 0x24, 0x18, 0x24, 0x18, 0x24, 0x42, 0x00, 0x3E, 0x4A, 0x4A,
    0x3A, 0x0A, 0x0A, 0x0A, 0x0A, 0x08, 0x1C, 0x2A, 0x28, 0x2A, 0x1C,
    0x08, 0x00, 0x3C, 0x7A, 0xA5, 0xA5, 0xB9, 0xA9, 0x66, 0x3C, 0x5F,
    0x60, 0x63, 0x62, 0x64, 0x7B, 0x60, 0x5F, 0xFF, 0x04, 0x03, 0xFC,
    0x02, 0xFC, 0x04, 0xF8, 0xFC, 0x02, 0xFC, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x78, 0x44, 0x44, 0x78, 0x4A, 0x44, 0x4B, 0x00, 0x61, 0x82,
    0x84, 0x68, 0x16, 0x29, 0x49, 0x86, 0x0E, 0x06, 0x0A, 0x70, 0x90,
    0x90, 0x60, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x08, 0x1C, 0x08,
    0x0E, 0x08, 0x08, 0x0E, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xE3, 0xDD,
    0xF3, 0xF7, 0xFF, 0xF7, 0xFF, 0x08, 0x14, 0x08, 0x1C, 0x2A, 0x08,
    0x14, 0x22, 0x08, 0x14, 0x08, 0x1C, 0x2A, 0x14, 0x3E, 0x14, 0x08,
    0x14, 0x22, 0x22, 0x22, 0x2A, 0x36, 0x22, 0x22, 0x14, 0x08, 0x3E,
    0x08, 0x3E, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x50, 0x20,
    0x00, 0x1C, 0x10, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x04, 0x04, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x7E, 0x02, 0x7E, 0x02, 0x04, 0x08, 0x00, 0x00, 0x00, 0x3E,
    0x02, 0x0C, 0x08, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x0C, 0x14,
    0x04, 0x00, 0x00, 0x00, 0x08, 0x3E, 0x22, 0x04, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x3E, 0x08, 0x08, 0x3E, 0x00, 0x00, 0x00, 0x04, 0x3E,
    0x0C, 0x14, 0x24, 0x00, 0x00, 0x00, 0x10, 0x3E, 0x12, 0x14, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x1C, 0x04, 0x04, 0x3E, 0x00, 0x00, 0x00,
    0x3C, 0x04, 0x1C, 0x04, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x2A,
    0x02, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7E, 0x02, 0x16, 0x18, 0x10, 0x20, 0x00, 0x02, 0x04, 0x08,
    0x18, 0x28, 0x48, 0x08, 0x00, 0x10, 0x7E, 0x42, 0x02, 0x02, 0x04,
    0x08, 0x00, 0x00, 0x3E, 0x08, 0x08, 0x08, 0x08, 0x3E, 0x00, 0x04,
    0x7E, 0x0C, 0x14, 0x24, 0x44, 0x04, 0x00, 0x10, 0x7E, 0x12, 0x12,
    0x12, 0x22, 0x44, 0x00, 0x08, 0x7F, 0x08, 0x7F, 0x08, 0x08, 0x08,
    0x00, 0x20, 0x3E, 0x22, 0x42, 0x04, 0x08, 0x10, 0x00, 0x20, 0x3E,
    0x48, 0x08, 0x08, 0x08, 0x10, 0x00, 0x00, 0x7E, 0x02, 0x02, 0x02,
    0x02, 0x7E, 0x00, 0x24, 0x7E, 0x24, 0x24, 0x04, 0x08, 0x10, 0x00,
    0x00, 0x60, 0x02, 0x62, 0x02, 0x04, 0x38, 0x00, 0x00, 0x3E, 0x02,
    0x04, 0x08, 0x14, 0x22, 0x00, 0x10, 0x7E, 0x11, 0x12, 0x10, 0x10,
    0x0E, 0x00, 0x00, 0x42, 0x22, 0x02, 0x04, 0x08, 0x10, 0x00, 0x20,
    0x3E, 0x22, 0x52, 0x0C, 0x08, 0x10, 0x00, 0x04, 0x38, 0x08, 0x7E,
    0x08, 0x08, 0x10, 0x00, 0x00, 0x52, 0x52, 0x52, 0x02, 0x04, 0x08,
    0x00, 0x00, 0x3C, 0x00, 0x7E, 0x08, 0x08, 0x10, 0x00, 0x10, 0x10,
    0x10, 0x18, 0x14, 0x12, 0x10, 0x00, 0x08, 0x08, 0x7E, 0x08, 0x08,
    0x08, 0x10, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00,
    0x00, 0x3E, 0x02, 0x14, 0x08, 0x14, 0x22, 0x00, 0x10, 0x7E, 0x04,
    0x08, 0x14, 0x32, 0x50, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x10,
    0x20, 0x00, 0x00, 0x10, 0x08, 0x44, 0x42, 0x42, 0x42, 0x00, 0x40,
    0x40, 0x7E, 0x40, 0x40, 0x40, 0x3E, 0x00, 0x7E, 0x02, 0x02, 0x02,
    0x02, 0x04, 0x18, 0x00, 0x00, 0x10, 0x28, 0x44, 0x02, 0x01, 0x01,
    0x00, 0x08, 0x7F, 0x08, 0x08, 0x49, 0x49, 0x08, 0x00, 0x7E, 0x02,
    0x02, 0x24, 0x18, 0x08, 0x04, 0x00, 0x40, 0x3C, 0x00, 0x3C, 0x00,
    0x7C, 0x02, 0x00, 0x08, 0x10, 0x20, 0x40, 0x42, 0x7E, 0x02, 0x00,
    0x02, 0x02, 0x14, 0x08, 0x14, 0x20, 0x40, 0x00, 0x00, 0x7E, 0x10,
    0x7E, 0x10, 0x10, 0x0E, 0x00, 0x10, 0x10, 0x7E, 0x12, 0x14, 0x10,
    0x10, 0x00, 0x00, 0x3C, 0x04, 0x04, 0x04, 0x04, 0x7F, 0x00, 0x3E,
    0x02, 0x02, 0x3E, 0x02, 0x02, 0x3E, 0x00, 0x3C, 0x00, 0x7E, 0x02,
    0x02, 0x04, 0x08, 0x00, 0x44, 0x44, 0x44, 0x44, 0x04, 0x08, 0x10,
    0x00, 0x10, 0x50, 0x50, 0x50, 0x52, 0x54, 0x58, 0x00, 0x40, 0x40,
    0x40, 0x44, 0x48, 0x50, 0x60, 0x00, 0x00, 0x7E, 0x42, 0x42, 0x42,
    0x42, 0x7E, 0x00, 0x00, 0x7E, 0x42, 0x42, 0x02, 0x04, 0x08, 0x00,
    0x00, 0x60, 0x00, 0x02, 0x02, 0x04, 0x78, 0x00, 0x10, 0x48, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x50, 0x20, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

// Video mode specifics follow

int SetPaletteEntryAlwaysFails(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    return 0;
}

void SetPaletteEntry(int palette, int which, float r, float g, float b)
{
    float y, i, q;
    RGBToYIQ(r, g, b, &y, &i, &q);
    paletteUInt32[palette][which] = NTSCYIQToUInt32(y, i, q);
}

int SetRowPalette(struct VideoModeEntry* modeEntry, int row, int palette)
{
    return NTSCSetRowPalette(row, palette);
}


// ----------------------------------------
// plain 40x24 black-on-white textport

#define TextportPlain40x24Width 40
#define TextportPlain40x24Height 24
#define TextportPlain40x24TopTick 44
#define TextportPlain40x24LeftTick 196

int TextportPlain40x24CursorX = 0;
int TextportPlain40x24CursorY = 0;
int TextportPlain40x24CursorFlash = 1;

// Textport video mode structs
VideoTextportInfo TextportPlain40x24Info = {
    TextportPlain40x24Width, TextportPlain40x24Height,
    TEXT_8BIT_BLACK_ON_WHITE,
};

void TextportPlain40x24Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 0);        /* monochrome text */
}

void TextportPlain40x24GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoTextportInfo *info = (VideoTextportInfo*)modeEntry->info;
    VideoTextportParameters *params = (VideoTextportParameters*)params_;
    params->base = imgBuffer;
    params->cursorX = &TextportPlain40x24CursorX;
    params->cursorY = &TextportPlain40x24CursorY;
    params->rowSize = info->width;
}

void TextportPlain40x24FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithinArea = rowNumber - TextportPlain40x24TopTick;
    int charRow = rowWithinArea / font8x8Height;
    int charPixelY = (rowWithinArea % font8x8Height);

    int invert = (TextportPlain40x24CursorFlash == 0) || (fieldNumber / 15 % 2 == 0);

// XXX this code assumes font width <= 8 and each row padded out to a byte
    if((rowWithinArea >= 0) && (charRow < TextportPlain40x24Height)) {
        for(int charCol = 0; charCol < TextportPlain40x24Width; charCol++) {
            unsigned char whichChar = imgBuffer[charRow * TextportPlain40x24Width + charCol];
            unsigned char charRowBits = font8x8Bits[whichChar * font8x8Height + charPixelY];
            unsigned char *charPixels = rowBuffer + TextportPlain40x24LeftTick + charCol * font8x8Width * 2;
            int invertThis = (charCol == TextportPlain40x24CursorX && charRow == TextportPlain40x24CursorY && invert);
            for(int charPixelX = 0; charPixelX < font8x8Width; charPixelX++) {
                int pixel = (charRowBits & (0x80 >> charPixelX)) ^ invertThis;
                charPixels[charPixelX * 2 + 0] = pixel ? NTSCBlack : NTSCWhite;
                charPixels[charPixelX * 2 + 1] = pixel ? NTSCBlack : NTSCWhite;
            }
        }
    }
}

// ----------------------------------------
// plain 80x24 white-on-black textport

// 196, 832, 44, 236
#define TextportPlain80x24Width 80
#define TextportPlain80x24Height 24
#define TextportPlain80x24TopTick 44
#define TextportPlain80x24LeftTick 196

int TextportPlain80x24CursorX = 0;
int TextportPlain80x24CursorY = 0;
int TextportPlain80x24CursorFlash = 0;

// Textport video mode structs
VideoTextportInfo TextportPlain80x24Info = {
    TextportPlain80x24Width, TextportPlain80x24Height,
    TEXT_8BIT_WHITE_ON_BLACK,
};

void TextportPlain80x24Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 0);        /* monochrome text */
}

void TextportPlain80x24GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoTextportInfo *info = (VideoTextportInfo*)modeEntry->info;
    VideoTextportParameters *params = (VideoTextportParameters*)params_;
    params->base = imgBuffer;
    params->cursorX = &TextportPlain80x24CursorX;
    params->cursorY = &TextportPlain80x24CursorY;
    params->rowSize = info->width;
}

int TextportPlain80x24SetPaletteEntry(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    return 0;
}

void TextportPlain80x24FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithinArea = rowNumber - TextportPlain80x24TopTick;
    int charRow = rowWithinArea / font8x8Height;
    int charPixelY = (rowWithinArea % font8x8Height);

    int invert = (TextportPlain80x24CursorFlash == 0) || (fieldNumber / 15 % 2 == 0);

// XXX this code assumes font width <= 8 and each row padded out to a byte
    if((rowWithinArea >= 0) && (charRow < TextportPlain80x24Height)) {
        for(int charCol = 0; charCol < TextportPlain80x24Width; charCol++) {
            unsigned char whichChar = imgBuffer[charRow * TextportPlain80x24Width + charCol];
            unsigned char charRowBits = font8x8Bits[whichChar * font8x8Height + charPixelY];
            unsigned char *charPixels = rowBuffer + TextportPlain80x24LeftTick + charCol * font8x8Width;
            int invertThis = (charCol == TextportPlain80x24CursorX && charRow == TextportPlain80x24CursorY && invert);
            for(int charPixelX = 0; charPixelX < font8x8Width; charPixelX++) {
                int pixel = (charRowBits & (0x80 >> charPixelX)) ^ invertThis;
                charPixels[charPixelX] = pixel ? NTSCWhite : NTSCBlack;
            }
        }
    }
}

// ----------------------------------------
// highest-horizontal-resolution 8-bit framebuffer

#define MAX_RES_MODE_WIDTH 400
#define MAX_RES_MODE_HEIGHT 128

#if IMGBUFFER_SIZE < MAX_RES_MODE_WIDTH * MAX_RES_MODE_HEIGHT
#error imgBuffer has become too small
#endif

// Pixmap video mode structs
VideoPixmapInfo ColorMaxResInfo = {
    MAX_RES_MODE_WIDTH, MAX_RES_MODE_HEIGHT,
    PALETTE_8BIT,
    256,
    1,
    1,
    225, 520,
};

void ColorMaxResSetup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 1);
}

void ColorMaxResGetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width;
}

// 510 and 142 are magic numbers measured for middle of screen
#define ColorMaxResLeft (512 - MAX_RES_MODE_WIDTH / 2) 
#define ColorMaxResWidth MAX_RES_MODE_WIDTH
#define ColorMaxResTop (142 - MAX_RES_MODE_HEIGHT / 2)
#define ColorMaxResHeight MAX_RES_MODE_HEIGHT

int ColorMaxResSetPaletteEntry(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    if(which >= 256) {
        return 1;
    }
    SetPaletteEntry(palette, which, r, g, b);
    return 0;
}

// XXX assumes ColorMaxResLeft is /4 and ColorMaxResWidth /4
void ColorMaxResFillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - ColorMaxResTop;

    if((rowWithin >= 0) && (rowWithin < ColorMaxResHeight)) {

        unsigned char *srcPixels = imgBuffer + rowWithin * ColorMaxResWidth;
        uint32_t *dstWords = (uint32_t*)(rowBuffer + ColorMaxResLeft);

        uint32_t *palette = paletteUInt32[rowPalette[rowWithin]];

        for(int i = 0; i < ColorMaxResWidth; i += 4) {
            uint32_t waveformPart0 = palette[srcPixels[i + 0]] & 0x000000FF;
            uint32_t waveformPart1 = palette[srcPixels[i + 1]] & 0x0000FF00;
            uint32_t waveformPart2 = palette[srcPixels[i + 2]] & 0x00FF0000;
            uint32_t waveformPart3 = palette[srcPixels[i + 3]] & 0xFF000000;
            *dstWords++ = waveformPart0 | waveformPart1 | waveformPart2 | waveformPart3;
        }
    }
}


// ----------------------------------------
// 175 x 230 8-bit bitmap display
// to be /4, 164, 864, 27, 257

// Pixmap video mode structs
VideoPixmapInfo Color175x230Info = {
    175, 230,
    PALETTE_8BIT,
    256,
    1,
    1,
    900, 520,
};

void Color175x230Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 1);
}

void Color175x230GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width;
}

// to be /4, 164, 864, 27, 257
#define Color175x230Left 164
#define Color175x230Width 700
#define Color175x230Top 27
#define Color175x230Height 230

int Color175x230SetPaletteEntry(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    if(which >= 256) {
        return 1;
    }
    SetPaletteEntry(palette, which, r, g, b);
    return 0;
}

void Color175x230FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - Color175x230Top;

    if((rowWithin >= 0) && (rowWithin < Color175x230Height)) {

        unsigned char *srcPixels = imgBuffer + rowWithin * (Color175x230Width / 4);
        unsigned char *dstBytes = rowBuffer + Color175x230Left;

        uint32_t *palette = paletteUInt32[rowPalette[rowWithin]];

        for(int i = 0; i < Color175x230Width; i += 4) {
            int p = srcPixels[i / 4];
            uint32_t word = palette[p];
            *(uint32_t*)(dstBytes + i) = word;
        }
    }
}

// ----------------------------------------
// 160 x 192 8-bit bitmap display
// 196, 832, 44, 236

// Pixmap video mode structs
VideoPixmapInfo Color160x192Info = {
    160, 192,
    PALETTE_8BIT,
    256,
    1,
    0,
    900, 520,
};

void Color160x192Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 1);
}

void Color160x192GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width;
}

#define Color160x192Left 196
#define Color160x192Width 640
#define Color160x192Top 44
#define Color160x192Height 192

int Color160x192SetPaletteEntry(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    if(which >= 256) {
        return 1;
    }
    SetPaletteEntry(palette, which, r, g, b);
    return 0;
}

void Color160x192FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - Color160x192Top;

    if((rowWithin >= 0) && (rowWithin < Color160x192Height)) {

        unsigned char *srcPixels = imgBuffer + rowWithin * (Color160x192Width / 4);
        unsigned char *dstBytes = rowBuffer + Color160x192Left;

        uint32_t *palette = paletteUInt32[rowPalette[rowWithin]];

        for(int i = 0; i < Color160x192Width; i += 4) {
            int p = srcPixels[i / 4];
            uint32_t word = palette[p];
            *(uint32_t*)(dstBytes + i) = word;
        }
    }
}

// ----------------------------------------
// 350 x 230 4-bit bitmap display
// to be /4, 164, 864, 27, 257

// Pixmap video mode structs
VideoPixmapInfo Color350x230Info = {
    350, 230,
    PALETTE_4BIT,
    16,
    1,
    1,
    450, 520,
};

void Color350x230Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 1);
}

void Color350x230GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width / 2;
}

// to be /4, 164, 864, 27, 257
#define Color350x230Left 164
#define Color350x230Width 700
#define Color350x230Top 27
#define Color350x230Height 230

int Color350x230SetPaletteEntry(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    if(which >= 16) {
        return 1;
    }
    SetPaletteEntry(palette, which, r, g, b);
    return 0;
}

void Color350x230FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - Color350x230Top;

    if((rowWithin >= 0) && (rowWithin < Color350x230Height)) {

        unsigned char *srcPixels = imgBuffer + rowWithin * (Color350x230Width / 4);
        unsigned char *dstBytes = rowBuffer + Color350x230Left;

        uint32_t *palette = paletteUInt32[rowPalette[rowWithin]];

        for(int i = 0; i < Color350x230Width / 2; i++) { /* loop over pixels */

            int colWithin = i;
            int whichByte = colWithin / 2;
            int whichNybble = colWithin % 2;
            int p = (srcPixels[whichByte] >> (whichNybble * 4)) & 0xF;

            uint32_t word = palette[p];
            uint32_t waveWordShift = whichNybble * 16;

            *dstBytes++ = (word >> (waveWordShift + 0)) & 0xFF;
            *dstBytes++ = (word >> (waveWordShift + 8)) & 0xFF;
        }
    }
}

// ----------------------------------------
// 320 x 192 4-bit bitmap display
// 196, 832, 44, 236

// Pixmap video mode structs
VideoPixmapInfo Color320x192Info = {
    320, 192,
    PALETTE_4BIT,
    16,
    1,
    0,
    450, 520,
};

void Color320x192Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 1);
}

void Color320x192GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width / 2;
}

#define Color320x192Left 196
#define Color320x192Width 640
#define Color320x192Top 44
#define Color320x192Height 192

int Color320x192SetPaletteEntry(VideoModeEntry* modeEntry, int palette, int which, float r, float g, float b)
{
    if(which >= 16) {
        return 1;
    }
    SetPaletteEntry(palette, which, r, g, b);
    return 0;
}

void Color320x192FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - Color320x192Top;

    if((rowWithin >= 0) && (rowWithin < Color320x192Height)) {

        unsigned char *srcPixels = imgBuffer + rowWithin * (Color320x192Width / 4);
        unsigned char *dstBytes = rowBuffer + Color320x192Left;

        uint32_t *palette = paletteUInt32[rowPalette[rowWithin]];

        for(int i = 0; i < Color320x192Width / 2; i++) { /* loop over pixels */

            int colWithin = i;
            int whichByte = colWithin / 2;
            int whichNybble = colWithin % 2;
            int p = (srcPixels[whichByte] >> (whichNybble * 4)) & 0xF;

            uint32_t word = palette[p];
            uint32_t waveWordShift = whichNybble * 16;

            *dstBytes++ = (word >> (waveWordShift + 0)) & 0xFF;
            *dstBytes++ = (word >> (waveWordShift + 8)) & 0xFF;
        }
    }
}

// ----------------------------------------
// 704 x 230 monochrome bitmap display
// to be /8, 164, 868, 27, 257

// Pixmap video mode structs
VideoPixmapInfo Bitmap704x230Info = {
    704, 230,
    BITMAP,
    -1,
    0,
    1,
    225, 520,
};

void Bitmap704x230Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 0);        /* monochrome text */
}

void Bitmap704x230GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width / 8;
}

// to be /8, 164, 868, 27, 257
#define Bitmap704x230Left 164
#define Bitmap704x230Width 704
#define Bitmap704x230Top 27
#define Bitmap704x230Height 230

uint32_t __attribute__((section (".ccmram"))) NybblesToMasks[16] = {
    0x00000000,
    0x000000FF,
    0x0000FF00,
    0x0000FFFF,
    0x00FF0000,
    0x00FF00FF,
    0x00FFFF00,
    0x00FFFFFF,
    0xFF000000,
    0xFF0000FF,
    0xFF00FF00,
    0xFF00FFFF,
    0xFFFF0000,
    0xFFFF00FF,
    0xFFFFFF00,
    0xFFFFFFFF,
};

void Bitmap704x230FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - Bitmap704x230Top;

    if((rowWithin >= 0) && (rowWithin < Bitmap704x230Height)) {

        unsigned char *bitmapRow = imgBuffer + rowWithin * (Bitmap704x230Width / 8);
        uint32_t *dstWords = (uint32_t*)(rowBuffer + Bitmap704x230Left);

        uint32_t whiteLong = (NTSCWhite << 24) | (NTSCWhite << 16) | (NTSCWhite << 8) | (NTSCWhite << 0);
        uint32_t blackLong = (NTSCBlack << 24) | (NTSCBlack << 16) | (NTSCBlack << 8) | (NTSCBlack << 0);

        for(int i = 0; i < Bitmap704x230Width; i += 8) {
            int whiteMask = 0;
            unsigned char byte = bitmapRow[i / 8];
            whiteMask = NybblesToMasks[byte & 0xF];
            *dstWords++ = (whiteLong & whiteMask) | (blackLong & ~whiteMask);
            whiteMask = NybblesToMasks[(byte >> 4) & 0xF];
            *dstWords++ = (whiteLong & whiteMask) | (blackLong & ~whiteMask);
        }
    }
}

// ----------------------------------------
// 640 x 192 monochrome bitmap display
// 196, 832, 44, 236

// Pixmap video mode structs
VideoPixmapInfo Bitmap640x192Info = {
    640, 192,
    BITMAP,
    -1,
    0,
    0,
    225, 520,
};

void Bitmap640x192Setup(VideoModeEntry *modeEntry)
{
    /* const VideoTextportInfo *info = (VideoTextportInfo*)info_; */
    NTSCFillBlankLine(NTSCBlankLine, 0);        /* monochrome text */
}

void Bitmap640x192GetParameters(VideoModeEntry *modeEntry, void *params_)
{
    const VideoPixmapInfo *info = (VideoPixmapInfo*)modeEntry->info;
    VideoPixmapParameters *params = (VideoPixmapParameters*)params_;
    params->base = imgBuffer;
    params->rowSize = info->width / 8;
}

#define Bitmap640x192Left 196
#define Bitmap640x192Width 640
#define Bitmap640x192Top 44
#define Bitmap640x192Height 192

void Bitmap640x192FillRow(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    int rowWithin = rowNumber - Bitmap640x192Top;

    if((rowWithin >= 0) && (rowWithin < Bitmap640x192Height)) {

        unsigned char *bitmapRow = imgBuffer + rowWithin * (Bitmap640x192Width / 8);
        uint32_t *dstWords = (uint32_t*)(rowBuffer + Bitmap640x192Left);

        uint32_t whiteLong = (NTSCWhite << 24) | (NTSCWhite << 16) | (NTSCWhite << 8) | (NTSCWhite << 0);
        uint32_t blackLong = (NTSCBlack << 24) | (NTSCBlack << 16) | (NTSCBlack << 8) | (NTSCBlack << 0);

        for(int i = 0; i < Bitmap640x192Width; i += 8) {
            int whiteMask = 0;
            unsigned char byte = bitmapRow[i / 8];
            whiteMask = NybblesToMasks[byte & 0xF];
            *dstWords++ = (whiteLong & whiteMask) | (blackLong & ~whiteMask);
            whiteMask = NybblesToMasks[(byte >> 4) & 0xF];
            *dstWords++ = (whiteLong & whiteMask) | (blackLong & ~whiteMask);
        }
    }
}

// ----------------------------------------
// Video mode table

VideoModeEntry __attribute__((section (".ccmram"))) videoModes[] =
{
    {
        VIDEO_TEXTPORT,
        &TextportPlain80x24Info,
        TextportPlain80x24Setup,
        TextportPlain80x24GetParameters,
        TextportPlain80x24FillRow,
        SetPaletteEntryAlwaysFails,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_TEXTPORT,
        &TextportPlain40x24Info,
        TextportPlain40x24Setup,
        TextportPlain40x24GetParameters,
        TextportPlain40x24FillRow,
        SetPaletteEntryAlwaysFails,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &Bitmap640x192Info,
        Bitmap640x192Setup,
        Bitmap640x192GetParameters,
        Bitmap640x192FillRow,
        SetPaletteEntryAlwaysFails,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &Color320x192Info,
        Color320x192Setup,
        Color320x192GetParameters,
        Color320x192FillRow,
        Color320x192SetPaletteEntry,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &Color160x192Info,
        Color160x192Setup,
        Color160x192GetParameters,
        Color160x192FillRow,
        Color160x192SetPaletteEntry,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &Bitmap704x230Info,
        Bitmap704x230Setup,
        Bitmap704x230GetParameters,
        Bitmap704x230FillRow,
        SetPaletteEntryAlwaysFails,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &Color350x230Info,
        Color350x230Setup,
        Color350x230GetParameters,
        Color350x230FillRow,
        Color350x230SetPaletteEntry,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &Color175x230Info,
        Color175x230Setup,
        Color175x230GetParameters,
        Color175x230FillRow,
        Color175x230SetPaletteEntry,
        SetRowPalette,
        NULL,
    },
    {
        VIDEO_PIXMAP,
        &ColorMaxResInfo,
        ColorMaxResSetup,
        ColorMaxResGetParameters,
        ColorMaxResFillRow,
        ColorMaxResSetPaletteEntry,
        SetRowPalette,
        NULL,
    },
};

// Generic videomode functions

int __attribute__((section (".ccmram"))) CurrentVideoMode = -1;

int VideoGetModeCount()
{
    return sizeof(videoModes) / sizeof(videoModes[0]);
}

enum VideoModeType VideoModeGetType(int n)
{
    return videoModes[n].type;
}

void VideoModeGetInfo(int n, void *info)
{
    VideoModeEntry* entry = videoModes + n;
    switch(entry->type) {
        case VIDEO_PIXMAP: {
            *(VideoPixmapInfo*)info = *(VideoPixmapInfo*)entry->info;
            break;
        }
        case VIDEO_TEXTPORT: {
            *(VideoTextportInfo*)info = *(VideoTextportInfo*)entry->info;
            break;
        }
        case VIDEO_SEGMENTS: {
            break;
        }
        case VIDEO_DCT: {
            break;
        }
        case VIDEO_WOZ: {
            break;
        }
        case VIDEO_TMS9918A: {
            break;
        }
        case VIDEO_WOLFENSTEIN: {
            break;
        }
    }
}

void VideoSetMode(int n)
{
    CurrentVideoMode = n;
    VideoModeEntry* entry = videoModes + CurrentVideoMode;
    entry->setup(entry);
}

int VideoGetCurrentMode()
{
    return CurrentVideoMode;
}

void VideoModeGetParameters(void *params)
{
    VideoModeEntry* entry = videoModes + CurrentVideoMode;
    entry->getParameters(entry, params);
}

int VideoModeSetPaletteEntry(int palette, int which, float r, float g, float b)
{
    VideoModeEntry* entry = videoModes + CurrentVideoMode;
    return entry->setPaletteEntry(entry, palette, which, r, g, b);
}
int VideoModeSetRowPalette(int row, int palette)
{
    VideoModeEntry* entry = videoModes + CurrentVideoMode;
    return entry->setRowPalette(entry, row, palette);
}

//------------------------------------------------------------------------------
// NTSC goop 

void NTSCFillRowBuffer(int fieldNumber, int rowNumber, unsigned char *rowBuffer)
{
    /*
     * Rows 0 through 8 are equalizing pulse, then vsync, then equalizing pulse
     */
    if(rowNumber < NTSC_EQPULSE_LINES) {

        // XXX should just change DMA source address but then these need to be in SRAM2
        memcpy(rowBuffer, NTSCEqSyncPulseLine, sizeof(NTSCEqSyncPulseLine));

    } else if(rowNumber - NTSC_EQPULSE_LINES < NTSC_VSYNC_LINES) {

        // XXX should just change DMA source address but then these need to be in SRAM2
        memcpy(rowBuffer, NTSCVsyncLine, sizeof(NTSCVsyncLine));

    } else if(rowNumber - (NTSC_EQPULSE_LINES + NTSC_VSYNC_LINES) < NTSC_EQPULSE_LINES) {

        // XXX should just change DMA source address but then these need to be in SRAM2
        memcpy(rowBuffer, NTSCEqSyncPulseLine, sizeof(NTSCEqSyncPulseLine));

    } else if(rowNumber - (NTSC_EQPULSE_LINES + NTSC_VSYNC_LINES + NTSC_EQPULSE_LINES) < NTSC_VBLANK_LINES) {

        /*
         * Rows 9 through 23 are other part of vertical blank
         */

        // XXX should just change DMA source address, then this needs to be in SRAM2
        memcpy(rowBuffer, NTSCBlankLine, sizeof(NTSCBlankLine));

    } else {

        // XXX copying a lot of bytes here that are immediately overwritten...
        memcpy(rowBuffer, NTSCBlankLine, sizeof(NTSCBlankLine));

        // 244 lines
        // 189 columns @ 4 per pixel
        int y = rowNumber - (NTSC_EQPULSE_LINES + NTSC_VSYNC_LINES + NTSC_EQPULSE_LINES + NTSC_VBLANK_LINES);

        switch(videoMode) {
            case VIDEO_COLOR_TEST: {
                if(y > 20 && y < 230) {
                    unsigned char *rowOut = rowBuffer + (NTSCHSyncClocks + NTSCBackPorchClocks + 3) / 4 * 4 + 20 * 4;
                    uint32_t* rowWords = (uint32_t*)rowOut;
                    unsigned char v = NTSCBlack + (NTSCWhite - NTSCBlack) / 2;
                    unsigned char a = NTSCWhite;
                    unsigned char c = NTSCBlack;
                    uint32_t colorWord = (v << 0) | (v << 8) | (v << 16) | (v << 24); // little endian
                    for(int col = 20; col < 50; col++) {
                        *rowWords++ = colorWord;
                    }
                    colorWord = (v << 0) | (a << 8) | (v << 16) | (c << 24); // little endian
                    for(int col = 50; col < 169; col++) {
                        *rowWords++ = colorWord;
                    }
                }
                break;
            }
            case VIDEO_GRAYSCALE: {
                if(y < 224) {
                    unsigned char *imgRow = imgBufferRow(y);
                    unsigned char *rowOut = rowBuffer + (NTSCHSyncClocks + NTSCBackPorchClocks + 3) / 4 * 4 + 20 * 4;
                    uint32_t* rowWords = (uint32_t*)rowOut;
                    for(int col = 0; col < 189; col++) {
                        unsigned char v = *imgRow++;
                        uint32_t colorWord = (v << 0) | (v << 8) | (v << 16) | (v << 24);
                        *rowWords++ = colorWord;
                    }
                }
                break;
            }
            case VIDEO_PALETTIZED: {
                if(y < 224) {
                    unsigned char *imgRow = imgBufferRow(y);
                    uint32_t* palette = paletteUInt32[rowPalette[y]];
                    unsigned char *rowOut = rowBuffer + (NTSCHSyncClocks + NTSCBackPorchClocks + 3) / 4 * 4;
                    uint32_t* rowWords = (uint32_t*)rowOut;
                    for(int col = 0; col < 189; col++) {
                        unsigned char pixel = *imgRow++;
                        uint32_t word = palette[pixel];
                        *rowWords++ = word;
                    }
                }
                break;
            }
            case VIDEO_SCAN_TEST: {
                if(rowNumber >= videoScanTestTop && rowNumber <= videoScanTestBottom) {
                    for(int row = videoScanTestLeft; row < videoScanTestRight; row++) {
                        rowBuffer[row] = 200;
                    }
                }
                break;
            }
            case VIDEO_MODE: {
                videoModes[VideoGetCurrentMode()].fillRow(fieldNumber, rowNumber, rowBuffer);
                break;
            }
        }
    }
    // if(rowNumber == 0) { rowBuffer[0] = 255;}
}

float GetPaletteUInt32(float h, float s, float v)
{
    float r, g, b;
    HSVToRGB3f(h, s, v, &r, &g, &b);
    float y, i, q;
    RGBToYIQ(r, g, b, &y, &i, &q);
    return NTSCYIQToUInt32(y, i, q);
}

void showSolidFramebuffer(uint32_t word)
{
    paletteUInt32[0][0] = word;
    for(int y = 0; y < 224; y++) {
        rowPalette[y] = 0;
        unsigned char *imgRow = imgBufferRow(y);

        for(int x = 0; x < 194; x++) {
            imgRow[x] = 0;
        }
    }
}

int __errno; // XXX fmod
void showVectorScopeImage(int phaseDegrees)
{
    for(int a = 0; a < MAX_PALETTE_ENTRIES; a++) {
        float hueDegrees = a * 360.0f / MAX_PALETTE_ENTRIES;
        float r, g, b;
        float hueRadians = hueDegrees / 180.0f * M_PI;
        HSVToRGB3f(hueRadians, 1, 1, &r, &g, &b);
        float y, i, q;
        RGBToYIQ(r, g, b, &y, &i, &q);
        paletteUInt32[0][a] = NTSCYIQToUInt32(y, i, q);
    }

    for(int y = 0; y < 224; y++) {
        unsigned char *imgRow = imgBufferRow(y);
        rowPalette[y] = 0;

        for(int x = 0; x < 194; x++) {
            float u = (x - 194.0f / 2.0f) / 194.0f; /* left to right */
            float v = - (y - 224.0f / 2.0f) / 224.0f; /* bottom to top */
            float degrees = atan2f(v, u) / M_PI * 180.0f; // HSV Red would be all the way to the right, Yellow-Green up
            float vectorScopeDegrees = degrees; // - 103.0f;
            float hue = fmodf((360.0f + vectorScopeDegrees + phaseDegrees) / 180.0f * M_PI, M_PI * 2);
            imgRow[x] = (unsigned char)(hue / (M_PI * 2) * MAX_PALETTE_ENTRIES);
        }
    }
}

unsigned char whichPalette = 0;

int readPalettized(const char *filename, float *duration)
{
    // Configure timer TIM5 - Only TIM2 and TIM5 are 32-bit
    TIM5->SR = 0;                       /* reset status */
    TIM5->ARR = 0xFFFFFFFF;
    TIM5->CNT = 0;
    TIM5->CR1 = TIM_CR1_CEN;            /* enable the timer */

    FIL file;
    FRESULT result = f_open (&file, filename, FA_READ | FA_OPEN_EXISTING);
    if(result) {
        logprintf(DEBUG_ERRORS, "ERROR: couldn't open \"%s\" for reading, FatFS result %d\n", filename, result);
        return 1;
    }

    UINT wasread;
    result = f_read(&file, paletteUInt32[whichPalette], sizeof(uint32_t) * 254, &wasread);
    if(result) {
        logprintf(DEBUG_ERRORS, "ERROR: couldn't read palette from \"%s\", result %d\n", filename, result);
        return 1;
    }

    static unsigned char rowTmp[194];
    for(int y = 0; y < 224; y++) {
        result = f_read(&file, rowTmp, 194, &wasread);
        // XXX wait until end of visible pixels?
        rowPalette[y] = whichPalette;
        memcpy(imgBufferRow(y), rowTmp, 194);

        if(result) {
            logprintf(DEBUG_ERRORS, "ERROR: couldn't read row %d from \"%s\", result %d\n", filename, y, result);
            return 1;
        }
    }
    uint32_t clocks = TIM5->CNT;
    TIM5->CR1 = 0;            /* stop the timer */

    uint32_t hundredthsSeconds = clocks / (SystemCoreClock / 100);
    if(0) {
        printf("image load took %lu.%02lu seconds (%lu clocks)\n", hundredthsSeconds / 100, hundredthsSeconds % 100, clocks);
    }
    *duration = hundredthsSeconds / 100.0f;

    whichPalette = (whichPalette + 1) % 2;

    return 0;
}

int readImage(const char *filename)
{
    static unsigned char rgbRow[194][3]; 
    static unsigned char palette[256][3];

    int paletteNext = 0;

    // Configure timer TIM5 - Only TIM2 and TIM5 are 32-bit
    TIM5->SR = 0;                       /* reset status */
    TIM5->ARR = 0xFFFFFFFF;
    TIM5->CNT = 0;
    TIM5->CR1 = TIM_CR1_CEN;            /* enable the timer */

    FIL file;
    FRESULT result = f_open (&file, filename, FA_READ | FA_OPEN_EXISTING);
    if(result) {
        logprintf(DEBUG_ERRORS, "ERROR: couldn't open \"%s\" for reading, FatFS result %d\n", filename, result);
        return 1;
    }

    for(int y = 0; y < 224; y++) {
        UINT wasread;
        result = f_read(&file, rgbRow, sizeof(rgbRow), &wasread);

        if(result) {
            logprintf(DEBUG_ERRORS, "ERROR: couldn't read row %d from \"%s\", result %d\n", filename, y, result);
            return 1;
        }

        unsigned char *imgRow = imgBufferRow(y);
        rowPalette[y] = whichPalette;

        for(int x = 0; x < 194; x++) {
            unsigned char r = rgbRow[x][0];
            unsigned char g = rgbRow[x][1];
            unsigned char b = rgbRow[x][2];
            int c;
            for(c = 0; c < paletteNext; c++) {
                if(palette[c][0] == r && palette[c][1] == g && palette[c][2] == b) {
                    break;
                }
            }
            if(c == paletteNext) {
                if(c == MAX_PALETTE_ENTRIES) {
                    printf("ran out of palette!\n");
                    return 1;
                } else {
                    palette[c][0] = r;
                    palette[c][1] = g;
                    palette[c][2] = b;
                    float y, i, q;
                    RGBToYIQ(r / 255.0f, g / 255.0f, b / 255.0f, &y, &i, &q);
                    paletteUInt32[whichPalette][c] = NTSCYIQToUInt32(y, i, q);
                    paletteNext++;
                }
            }
            imgRow[x] = c;
        }
    }
    uint32_t clocks = TIM5->CNT;
    TIM5->CR1 = 0;            /* stop the timer */
    uint32_t hundredthsSeconds = clocks / (SystemCoreClock / 100);
    printf("image load took %lu.%02lu seconds (%lu clocks)\n", hundredthsSeconds / 100, hundredthsSeconds % 100, clocks);

    whichPalette = (whichPalette + 1) % 2;

    return 0;
}

//----------------------------------------------------------------------------
// DMA and debug scanout specifics

#if 0

static const int fontWidth = 5, fontHeight = 7;
// static const unsigned char fontMask = 0xfc;
static const int fontOffset = 32;

static const unsigned char fontBytes[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x20, 0x20, 0x20, 0x00, 0x20, 0x00,
    0x50, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x50, 0xF8, 0x50, 0xF8, 0x50, 0x00,
    0x00, 0x70, 0xA0, 0x70, 0x28, 0x70, 0x00,
    0x80, 0x90, 0x20, 0x40, 0x90, 0x10, 0x00,
    0x00, 0x40, 0xA0, 0x40, 0xA0, 0x50, 0x00,
    0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x40, 0x40, 0x40, 0x40, 0x20, 0x00,
    0x40, 0x20, 0x20, 0x20, 0x20, 0x40, 0x00,
    0x00, 0x50, 0x20, 0x70, 0x20, 0x50, 0x00,
    0x00, 0x20, 0x20, 0xF8, 0x20, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x20, 0x40,
    0x00, 0x00, 0x00, 0xF0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x60, 0x60, 0x00,
    0x00, 0x10, 0x20, 0x40, 0x80, 0x00, 0x00,
    0x20, 0x50, 0x50, 0x50, 0x50, 0x20, 0x00,
    0x20, 0x60, 0x20, 0x20, 0x20, 0x70, 0x00,
    0x60, 0x90, 0x10, 0x20, 0x40, 0xF0, 0x00,
    0xF0, 0x10, 0x60, 0x10, 0x90, 0x60, 0x00,
    0x20, 0x60, 0xA0, 0xF0, 0x20, 0x20, 0x00,
    0xF0, 0x80, 0xE0, 0x10, 0x90, 0x60, 0x00,
    0x60, 0x80, 0xE0, 0x90, 0x90, 0x60, 0x00,
    0xF0, 0x10, 0x20, 0x20, 0x40, 0x40, 0x00,
    0x60, 0x90, 0x60, 0x90, 0x90, 0x60, 0x00,
    0x60, 0x90, 0x90, 0x70, 0x10, 0x60, 0x00,
    0x00, 0x60, 0x60, 0x00, 0x60, 0x60, 0x00,
    0x00, 0x60, 0x60, 0x00, 0x60, 0x40, 0x80,
    0x00, 0x10, 0x20, 0x40, 0x20, 0x10, 0x00,
    0x00, 0x00, 0xF0, 0x00, 0xF0, 0x00, 0x00,
    0x00, 0x40, 0x20, 0x10, 0x20, 0x40, 0x00,
    0x20, 0x50, 0x10, 0x20, 0x00, 0x20, 0x00,
    0x60, 0x90, 0xB0, 0xB0, 0x80, 0x60, 0x00,
    0x60, 0x90, 0x90, 0xF0, 0x90, 0x90, 0x00,
    0xE0, 0x90, 0xE0, 0x90, 0x90, 0xE0, 0x00,
    0x60, 0x90, 0x80, 0x80, 0x90, 0x60, 0x00,
    0xE0, 0x90, 0x90, 0x90, 0x90, 0xE0, 0x00,
    0xF0, 0x80, 0xE0, 0x80, 0x80, 0xF0, 0x00,
    0xF0, 0x80, 0xE0, 0x80, 0x80, 0x80, 0x00,
    0x60, 0x90, 0x80, 0xB0, 0x90, 0x70, 0x00,
    0x90, 0x90, 0xF0, 0x90, 0x90, 0x90, 0x00,
    0x70, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00,
    0x10, 0x10, 0x10, 0x10, 0x90, 0x60, 0x00,
    0x90, 0xA0, 0xC0, 0xC0, 0xA0, 0x90, 0x00,
    0x80, 0x80, 0x80, 0x80, 0x80, 0xF0, 0x00,
    0x90, 0xF0, 0xF0, 0x90, 0x90, 0x90, 0x00,
    0x90, 0xD0, 0xD0, 0xB0, 0xB0, 0x90, 0x00,
    0x60, 0x90, 0x90, 0x90, 0x90, 0x60, 0x00,
    0xE0, 0x90, 0x90, 0xE0, 0x80, 0x80, 0x00,
    0x60, 0x90, 0x90, 0x90, 0xD0, 0x60, 0x10,
    0xE0, 0x90, 0x90, 0xE0, 0xA0, 0x90, 0x00,
    0x60, 0x90, 0x40, 0x20, 0x90, 0x60, 0x00,
    0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x60, 0x00,
    0x90, 0x90, 0x90, 0x90, 0x60, 0x60, 0x00,
    0x90, 0x90, 0x90, 0xF0, 0xF0, 0x90, 0x00,
    0x90, 0x90, 0x60, 0x60, 0x90, 0x90, 0x00,
    0x50, 0x50, 0x50, 0x20, 0x20, 0x20, 0x00,
    0xF0, 0x10, 0x20, 0x40, 0x80, 0xF0, 0x00,
    0x70, 0x40, 0x40, 0x40, 0x40, 0x70, 0x00,
    0x00, 0x80, 0x40, 0x20, 0x10, 0x00, 0x00,
    0x70, 0x10, 0x10, 0x10, 0x10, 0x70, 0x00,
    0x20, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00,
    0x40, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x90, 0xB0, 0x50, 0x00,
    0x80, 0x80, 0xE0, 0x90, 0x90, 0xE0, 0x00,
    0x00, 0x00, 0x60, 0x80, 0x80, 0x60, 0x00,
    0x10, 0x10, 0x70, 0x90, 0x90, 0x70, 0x00,
    0x00, 0x00, 0x60, 0xB0, 0xC0, 0x60, 0x00,
    0x20, 0x50, 0x40, 0xE0, 0x40, 0x40, 0x00,
    0x00, 0x00, 0x70, 0x90, 0x60, 0x80, 0x70,
    0x80, 0x80, 0xE0, 0x90, 0x90, 0x90, 0x00,
    0x20, 0x00, 0x60, 0x20, 0x20, 0x70, 0x00,
    0x10, 0x00, 0x10, 0x10, 0x10, 0x50, 0x20,
    0x80, 0x80, 0xA0, 0xC0, 0xA0, 0x90, 0x00,
    0x60, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00,
    0x00, 0x00, 0xA0, 0xF0, 0x90, 0x90, 0x00,
    0x00, 0x00, 0xE0, 0x90, 0x90, 0x90, 0x00,
    0x00, 0x00, 0x60, 0x90, 0x90, 0x60, 0x00,
    0x00, 0x00, 0xE0, 0x90, 0x90, 0xE0, 0x80,
    0x00, 0x00, 0x70, 0x90, 0x90, 0x70, 0x10,
    0x00, 0x00, 0xE0, 0x90, 0x80, 0x80, 0x00,
    0x00, 0x00, 0x70, 0xC0, 0x30, 0xE0, 0x00,
    0x40, 0x40, 0xE0, 0x40, 0x40, 0x30, 0x00,
    0x00, 0x00, 0x90, 0x90, 0x90, 0x70, 0x00,
    0x00, 0x00, 0x50, 0x50, 0x50, 0x20, 0x00,
    0x00, 0x00, 0x90, 0x90, 0xF0, 0xF0, 0x00,
    0x00, 0x00, 0x90, 0x60, 0x60, 0x90, 0x00,
    0x00, 0x00, 0x90, 0x90, 0x50, 0x20, 0x40,
    0x00, 0x00, 0xF0, 0x20, 0x40, 0xF0, 0x00,
    0x10, 0x20, 0x60, 0x20, 0x20, 0x10, 0x00,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00,
    0x40, 0x20, 0x30, 0x20, 0x20, 0x40, 0x00,
};

const int validRight = 15;
const int validTop = 26;
const int validWidth = 164;
const int validHeight = 200;

#endif

int charX = 0, charY = 0;

void VIDEO_putchar(char c)
{
    if((VideoGetCurrentMode() >= 0) && (videoModes[VideoGetCurrentMode()].type == VIDEO_TEXTPORT)) {

        VideoTextportInfo info;
        VideoTextportParameters params;
        VideoModeGetInfo(VideoGetCurrentMode(), &info);
        VideoModeGetParameters(&params);

        if(c == '\n') {

            charX = 0;
            charY++;

        } else if(c == 127 || c == '\b') {

            if(charX > 0) {
                charX--;
            }

        } else {

            params.base[params.rowSize * charY + charX] = c;
            charX++;
            if(charX >= info.width) {
                charX = 0;
                charY++;
            }
        }

        if(charY >= info.height) {
            /* scroll */
            for(int y = 0; y < info.height - 1; y++) {
                memcpy(params.base + params.rowSize * y, params.base + params.rowSize * (y + 1), info.width);
            }
            memset(params.base + params.rowSize * (info.height - 1), ' ', info.width);
            charY--;
        }

        *params.cursorX = charX;
        *params.cursorY = charY;
    }
#if 0
    /* grayscale 5x7 text */
    else {
        int pixelX = validRight + charX * fontWidth;
        int pixelY = validTop + charY * fontHeight;

        if(c == '\n') {

            charX = 0;
            charY++;

        } else if(c == 127 || c == '\b') {

            charX--;

        } else {

            if(c >= fontOffset) {
                const unsigned char *glyph = fontBytes + fontHeight * (c - fontOffset);
                for(int gx = 0; gx < fontWidth + 1; gx++) {
                    for(int gy = 0; gy < fontHeight + 1; gy++) {
                        int v;
                        if(gx < fontWidth && gy < fontHeight && (glyph[gy + gx / 8] & (1 << (7 - (gx % 8))))) {
                            v = PALETTE_WHITE;
                        } else {
                            v = PALETTE_BLACK;
                        }
                        *imgBufferPixel(pixelX + gx, pixelY + gy) = v;
                    } 
                }
                charX++;
                if(charX >= validWidth / fontWidth) {
                    charX = 0;
                    charY++;
                }
            }
        }

        if(charY >= validHeight / fontHeight) {
            /* scroll */
            for(int y = validTop; y < validTop + validHeight - fontHeight; y++) {
                memcpy(imgBufferRow(y) + validRight, imgBufferRow(y + fontHeight) + validRight, validWidth);
            }
            for(int y = validTop + validHeight - fontHeight; y < validTop + validHeight; y++) {
                memset(imgBufferRow(y) + validRight, PALETTE_BLACK, validWidth);
            }
            charY--;
        }
    }
#endif
}

int __attribute__((section (".ccmram"))) debugOverlayEnabled = 0;

#define debugDisplayWidth 19
#define debugDisplayHeight 13
#define debugDisplayLeftTick (NTSCHSyncClocks + NTSCBackPorchClocks + 48)
#define debugDisplayTopTick (NTSC_EQPULSE_LINES + NTSC_VSYNC_LINES + NTSC_EQPULSE_LINES + NTSC_VBLANK_LINES + 20)
/* debugFontWidthScale != 4 looks terrible in a color field because of adjacent color columns; probably need to ensure 0s around any 1 text column */
#define debugFontWidthScale 4
#define debugCharGapPixels 1
#define debugFontHeightScale 1
char __attribute__((section (".ccmram"))) debugDisplay[debugDisplayHeight][debugDisplayWidth];

#include "8x16.h"
// static int font8x16Width = 8, font8x16Height = 16;
// static unsigned char font8x16Bits[] = /* was a bracket here */

/* 8x16 font, 4x width, doubled height */

void fillRowDebugOverlay(int fieldNumber, int rowNumber, unsigned char* nextRowBuffer)
{
    uint32_t NTSCWhiteLong =
        (NTSCWhite <<  0) |
        (NTSCWhite <<  8) |
        (NTSCWhite << 16) |
        (NTSCWhite << 24);
    int debugFontScanlineHeight = font8x16Height * debugFontHeightScale;

    int rowWithinDebugArea = rowNumber - debugDisplayTopTick;
    int charRow = rowWithinDebugArea / debugFontScanlineHeight;
    int charPixelY = (rowWithinDebugArea % debugFontScanlineHeight) / debugFontHeightScale;

// XXX this code assumes font width <= 8 and each row padded out to a byte
    if((rowWithinDebugArea >= 0) && (charRow < debugDisplayHeight)) {
        for(int charCol = 0; charCol < debugDisplayWidth; charCol++) {
            unsigned char debugChar = debugDisplay[charRow][charCol];
            if(debugChar != 0) {
                unsigned char charRowBits = font8x16Bits[debugChar * font8x16Height + charPixelY];
#if debugFontWidthScale == 4 && font8x16Width == 8
                unsigned char *charPixels = nextRowBuffer + debugDisplayLeftTick + (charCol * (font8x16Width + debugCharGapPixels)) * debugFontWidthScale;
                if(charRowBits & 0x80) { ((uint32_t*)charPixels)[0] = NTSCWhiteLong; }
                if(charRowBits & 0x40) { ((uint32_t*)charPixels)[1] = NTSCWhiteLong; }
                if(charRowBits & 0x20) { ((uint32_t*)charPixels)[2] = NTSCWhiteLong; }
                if(charRowBits & 0x10) { ((uint32_t*)charPixels)[3] = NTSCWhiteLong; }
                if(charRowBits & 0x08) { ((uint32_t*)charPixels)[4] = NTSCWhiteLong; }
                if(charRowBits & 0x04) { ((uint32_t*)charPixels)[5] = NTSCWhiteLong; }
                if(charRowBits & 0x02) { ((uint32_t*)charPixels)[6] = NTSCWhiteLong; }
                if(charRowBits & 0x01) { ((uint32_t*)charPixels)[7] = NTSCWhiteLong; }
#else
                for(int charPixelX = 0; charPixelX < font8x16Width; charPixelX++) {
                    int pixel = charRowBits & (0x80 >> charPixelX);
                    if(pixel) {
                        unsigned char *charPixels = nextRowBuffer + debugDisplayLeftTick + (charCol * (font8x16Width + debugCharGapPixels) + charPixelX) * debugFontWidthScale;
#if debugFontWidthScale == 4
                        *(uint32_t *)charPixels = NTSCWhiteLong; 
#else
                        for(int col = 0; col < debugFontWidthScale; col++) {
                            charPixels[col] = NTSCWhite;
                        }
#endif
                    }
                }
#endif
            }
        }
    }
}

//----------------------------------------------------------------------------
// Text command interface

int showScanoutStats = 0;

void NTSCGenerateLineBuffers();

int doCommandTestColor(int wordCount, const char **words)
{
    enum VideoModeType type = VideoModeGetType(VideoGetCurrentMode());
    
    if(type != VIDEO_PIXMAP) {

        printf("current mode is not a pixmap; use \"modes\"\n");
        printf("and \"mode\" to choose and set a pixmap mode\n");

    } else {

        VideoPixmapInfo info;
        VideoPixmapParameters params;
        VideoModeGetInfo(VideoGetCurrentMode(), &info);
        VideoModeGetParameters(&params);
        if(info.paletteSize > 0) {
            MakePalette(0);
            for(int y = 0; y < info.height; y++) {
                rowPalette[y] = 0;
            }
        }

        ClearPixmap(0);

        // Yes, my use of rand() here is bad.

        for(int i = 0; i < 100; i++) {
            int cx = 10 + rand() % (info.width - 20);
            int cy = 10 + rand() % (info.height - 20);
            int cr = 5 + rand() % 30;
            int c;
            if(info.paletteSize == -1) {
                c = rand() % 2;
            } else {
                c = rand() % info.paletteSize;
            }
            DrawFilledCircle(cx, cy, cr, c, info.aspectX, info.aspectY);

            int x0 = 10 + rand() % (info.width - 20);
            int y0 = 10 + rand() % (info.height - 20);
            int x1 = 10 + rand() % (info.width - 20);
            int y1 = 10 + rand() % (info.height - 20);
            DrawLine(x0, y0, x1, y1, c);
        }
    }

    return COMMAND_CONTINUE;
}

int doCommandVideoModes(int wordCount, const char **words)
{
    for(int i = 0; i < VideoGetModeCount(); i++) {
        printf("Mode %d\n", i);
        enum VideoModeType type = VideoModeGetType(i);
        switch(type) {
            case VIDEO_PIXMAP: {
                VideoPixmapInfo info;
                VideoModeGetInfo(i, &info);
                printf("    pixmap, %d by %d\n", info.width, info.height);
                printf("    is %s", info.color ? "color" : "monochrome or grayscale");
                if(info.paletteSize > 0) {
                    printf(", %d palette entries", info.paletteSize);
                }
                printf("\n");
                printf("    %s\n", info.overscan ? "overscan" : "underscan");
                break;
            }
            case VIDEO_TEXTPORT: {
                VideoTextportInfo info;
                VideoModeGetInfo(i, &info);
                printf("    text, %d by %d\n", info.width, info.height);
                switch(info.attributes) {
                    case TEXT_8BIT_WHITE_ON_BLACK: printf("    8-bit, white on black\n"); break;
                    case TEXT_8BIT_BLACK_ON_WHITE: printf("    8-bit, black on white\n"); break;
                    case TEXT_16BIT_8_3_3_2_RICH: printf("    16-bit, rich formatting\n"); break;
                }
                break;
            }
            case VIDEO_WOZ: {
                printf("    Woz-type\n");
                break;
            }
            case VIDEO_TMS9918A: {
                printf("    TMS9918A-type\n");
                break;
            }
            case VIDEO_SEGMENTS: {
                printf("    real-time segment-renderer-type\n");
                break;
            }
            case VIDEO_DCT: {
                printf("    DCT-type\n");
                break;
            }
            case VIDEO_WOLFENSTEIN: {
                printf("    Wolfenstein-type\n");
                break;
            }
            default: {
                printf("    unknown mode type %08X\n", type);
            }
        }
    }
    return COMMAND_CONTINUE;
}

int doCommandSetVideoMode(int wordCount, const char **words)
{
    videoMode = VIDEO_MODE;
    int which = strtol(words[1], NULL, 0);
    if((which < 0) || (which >= VideoGetModeCount())) {
        printf("mode %d is out of range; only %d modes (list modes with \"modes\")\n", which, VideoGetModeCount());
    } else {
        VideoSetMode(which);
    }
    return COMMAND_CONTINUE;
}

int doCommandStream(int wordCount, const char **words)
{
    videoMode = VIDEO_PALETTIZED;

    const char *formatName = words[1];
    int start = strtol(words[2], NULL, 0);
    int end = strtol(words[3], NULL, 0);

    char formattedName[512];
    float displayTime = 0;
    float endTime = (end - start) / 30.0;
    do {
        int frameNumber = start + (int)(displayTime * 30.0);
        sprintf(formattedName, formatName, frameNumber);
        // Must have been quantized to 256 colors!
        // while(rowNumber < 224); // Wait for VBLANK-ish
        float duration;
        if(readPalettized(formattedName, &duration)) {
            printf("failed to show \"%s\"\n", formattedName); 
            break;
        }
        displayTime += duration;
    } while(displayTime < endTime);
    return COMMAND_CONTINUE;
}

int doCommandSDReset(int wordCount, const char **words)
{
    printf("Resetting SD card...\n");

    if(!SDCARD_init()) {
        printf("Failed to start access to SD card as SPI\n");
    }
    return COMMAND_CONTINUE;
}

int doCommandVideoTest(int wordCount, const char **words)
{
    videoMode = VIDEO_COLOR_TEST;
    return COMMAND_CONTINUE;
}

int doCommandGrayscale(int wordCount, const char **words)
{
    videoMode = VIDEO_GRAYSCALE;
    return COMMAND_CONTINUE;
}

int doCommandScanoutStats(int wordCount, const char **words)
{
    showScanoutStats = !showScanoutStats;
    if(!showScanoutStats) {
        debugOverlayEnabled = 0;
    }
    return COMMAND_CONTINUE;
}

int doCommandVideoYIQ(int wordCount, const char **words)
{
    videoMode = VIDEO_PALETTIZED;
    return COMMAND_CONTINUE;
}

int doCommandVideoText(int wordCount, const char **words)
{
    gOutputDevices = gOutputDevices ^ OUTPUT_TO_VIDEO;
    return COMMAND_CONTINUE;
}

int doCommandShowLineEnd(int wordCount, const char **words)
{
    markHandlerInSamples = !markHandlerInSamples;
    return COMMAND_CONTINUE;
}

int doCommandScanoutCycles(int wordCount, const char **words)
{
    for(int i = 0; i < 262; i++) {
        printf("row %3d: %8lu cycles, %lu microseconds", i, rowCyclesSpent[i],
            rowCyclesSpent[i] / (SystemCoreClock / 1000000));
        if(rowCyclesSpent[i] > 10677) {
            printf(", overrun by %lu%%\n", rowCyclesSpent[i] * 100 / 10677 - 100);
        } else { 
            printf(", %lu%% remained\n", (10677 - rowCyclesSpent[i]) * 100 / 10677);
        }
    }

    return COMMAND_CONTINUE;
}

int doCommandLS(int wordCount, const char **words)
{
    FRESULT res;
    DIR dir;
    static FILINFO fno;

    res = f_opendir(&dir, "/");                       /* Open the directory */
    if (res == FR_OK) {
        for (;;) {
            res = f_readdir(&dir, &fno);                   /* Read a directory item */
            if(res != FR_OK) {
                printf("failed to readdir - %d\n", res);
                break;
            }
            if (fno.fname[0] == 0) break;  /* Break on end of dir */
            if (fno.fattrib & AM_DIR) {                    /* It is a directory */
                printf("/%s/\n", fno.fname);
            } else {                                       /* It is a file. */
                printf("/%s\n", fno.fname);
            }
        }
        f_closedir(&dir);
    } else {
        printf("failed to f_opendir - %d\n", res);
    }
    return COMMAND_CONTINUE;
}

int doCommandSolidFill(int wordCount, const char **words)
{
    uint32_t fill = strtoul(words[1], NULL, 0);
    videoMode = VIDEO_PALETTIZED;
    printf("solid image with %08lX word\n", fill);
    showSolidFramebuffer(fill);
    return COMMAND_CONTINUE;
}

int doCommandScopeFill(int wordCount, const char **words)
{
    videoMode = VIDEO_PALETTIZED;
    int degrees = strtol(words[1], NULL, 0);
    printf("vector scope image at %d degrees phase\n", degrees);
    showVectorScopeImage(degrees);
    return COMMAND_CONTINUE;
}

int doCommandShowImage(int wordCount, const char **words)
{
    videoMode = VIDEO_PALETTIZED;

    // Must have been quantized to 256 colors!
    if(readImage(words[1])) {
        printf("failed to show \"%s\"\n", words[1]); 
    }
    return COMMAND_CONTINUE;
}

int doCommandTestSDSpeed(int wordCount, const char **words)
{
    const int megabytes = 2;
    printf("will read %d megabyte of SD\n", megabytes);

    SERIAL_flush();

    int then = HAL_GetTick();
    for(int i = 0; i < megabytes * 1024 * 1024 / SD_BLOCK_SIZE; i++)
        SDCARD_readblock(i, sd_buffer);
    int now = HAL_GetTick();

    int kilobytes = megabytes * 1024;
    int millis = now - then;
    int kb_per_second = kilobytes * 1000 / millis;

    printf("done, %d KB per second\n", kb_per_second);

    return COMMAND_CONTINUE;
}

int doCommandDumpKbdData(int wordCount, const char **words)
{
    gDumpKeyboardData = !gDumpKeyboardData;
    if(gDumpKeyboardData)
        printf("Dumping keyboard data...\n");
    else
        printf("Not dumping keyboard data...\n");
    return COMMAND_CONTINUE;
}

int doCommandFlashInfoLED(int wordCount, const char **words)
{
    for(int i = 0; i < 8; i++) {
        LED_set_info(1);
        delay_ms(125);
        LED_set_info(0);
        delay_ms(125);
    }
    return COMMAND_CONTINUE;
}

int doCommandPanic(int wordCount, const char **words)
{
    printf("panicking now\n");
    panic();
    return COMMAND_CONTINUE; /* notreached */
}

int doCommandShowVersion(int wordCount, const char **words)
{
    printf("%s\n", IOBOARD_FIRMWARE_VERSION_STRING);
    return COMMAND_CONTINUE;
}

int doCommandSetDebugLevel(int wordCount, const char **words)
{
    gDebugLevel = strtol(words[1], NULL, 0);
    printf("Debug level set to %d\n", gDebugLevel);
    return COMMAND_CONTINUE;
}

int doCommandReadBlock(int wordCount, const char **words)
{
    int n = strtol(words[1], NULL, 0);
    if(SDCARD_readblock(n, sd_buffer)) {
        dump_buffer_hex(4, sd_buffer, sizeof(sd_buffer));
    }
    return COMMAND_CONTINUE;
}

int doCommandTestRect(int wordCount, const char **words)
{
    videoMode = VIDEO_SCAN_TEST;
    do {
        SERIAL_poll_continue();
        unsigned char isEmpty = queue_isempty(&mon_queue.q);
        if(!isEmpty) {
            unsigned char c = queue_deq(&mon_queue.q);
            switch(c) {
                case 'l': videoScanTestLeft--; break;
                case 'L': videoScanTestLeft++; break;
                case 'r': videoScanTestRight--; break;
                case 'R': videoScanTestRight++; break;
                case 't': videoScanTestTop--; break;
                case 'T': videoScanTestTop++; break;
                case 'b': videoScanTestBottom--; break;
                case 'B': videoScanTestBottom++; break;
                case 'q': break; break;
            }
            printf("rect is %d, %d, %d, %d\n", videoScanTestLeft, videoScanTestRight, videoScanTestTop, videoScanTestBottom);
            SERIAL_flush();
        }
        delay_ms(10);
    } while(1);
    return COMMAND_CONTINUE;
}

#define MAX_COMMANDS 50

Command commands[MAX_COMMANDS] = {
    {
        "modes", 1, doCommandVideoModes, "",
        "list video modes"
    },
    {
        "mode", 2, doCommandSetVideoMode, "",
        "set video mode"
    },
    {
        "rect", 1, doCommandTestRect, "",
        "run interactive over/underscan test"
    },
    {
        "stream", 4, doCommandStream, "name M N",
        "stream images from templated name (e.g. 'frame%05d') from number M to N"
    },
    {
        "sdreset", 1, doCommandSDReset, "",
        "reset SD card"
    },
    {
        "videotest", 1, doCommandVideoTest, "",
        "switch to video test mode"
    },
    {
        "grayscale", 1, doCommandGrayscale, "",
        "switch to grayscale image mode"
    },
    {
        "stats", 1, doCommandScanoutStats, "",
        "toggle viewing of scanout error statistics"
    },
    {
        "showend", 1, doCommandShowLineEnd, "",
        "put out a marker in the DAC at line processing end"
    },
    {
        "yiq", 1, doCommandVideoYIQ, "",
        "switch to YIQ palettized video mode"
    },
    {
        "text", 1, doCommandVideoText, "",
        "toggle text into bitmap mode"
    },
    {
        "rowcycles", 1, doCommandScanoutCycles, "",
        "print time spent in each row operation"
    },
    {
        "ls", 1, doCommandLS, "",
        "list files on SD"
    },
    {
        "solid", 2, doCommandSolidFill, "fillword",
        "fill video with specified long int"
    },
    {
        "scope", 2, doCommandScopeFill, "degrees",
        "fill with scope signal with degree offset"
    },
    {
        "oldshow", 2, doCommandShowImage, "name",
        "show 194x224 24-bit RGB image blob (must be quantized to 254)"
    },
    {
        "sdspeed", 1, doCommandTestSDSpeed, "",
        "test SD read speed"
    },
    {
        "dumpkbd", 1, doCommandDumpKbdData, "",
        "dump keyboard data"
    },
    {
        "flashinfo", 1, doCommandFlashInfoLED, "",
        "flash the info LED"
    },
    {
        "panic", 1, doCommandPanic, "",
        "panic"
    },
    {
        "version", 1, doCommandShowVersion, "",
        "display build version string"
    },
    {
        "debug", 2, doCommandSetDebugLevel, "N",
        "set debugging level to N"
    },
    {
        "read", 2, doCommandReadBlock, "N",
        "read and dump out SD block N"
    },
};
int commandsCount = 23;

int RegisterApp(const char* name, int minWords, ProcessCommandFunc go, const char *form, const char *help)
{
    if(commandsCount >= MAX_COMMANDS) {
        printf("maximum command count reached!\n");
        return COMMAND_ADD_FAILED;
    }
    Command *newcmd = commands + commandsCount;
    commandsCount++;

    newcmd->name = name;
    newcmd->minWords = minWords;
    newcmd->go = go;
    newcmd->form = form;
    newcmd->help = help;

    return 0;
}

static void RegisterMyApp(void) __attribute__((constructor));
static void RegisterMyApp(void)
{
    RegisterApp("testcolor", 1, doCommandTestColor, "",
        "test color modes"
        );
}

void usage()
{
    int maxNeeded = 0;
    for(int which = 0; which < commandsCount; which++) {
        Command *cmd = commands + which;
        int needed = strlen(cmd->name) + 1 + strlen(cmd->form);
        maxNeeded = (needed > maxNeeded) ? needed : maxNeeded;
    }
    for(int which = 0; which < commandsCount; which++) {
        Command *cmd = commands + which;
        printf("%s %s", cmd->name, cmd->form);
        // XXX some day be smarter about word wrap etc
        printf("%*s - %s\n", maxNeeded - strlen(cmd->name) - 1 - strlen(cmd->form), "", cmd->help);
    }
}

#define CommandWordsMax 10

int processCommandLine(char *line)
{
    static char *words[CommandWordsMax];

    int wordsCount = SplitString(line, words, CommandWordsMax);

    if(wordsCount == 0) {
        return COMMAND_CONTINUE;
    }

    if(wordsCount == CommandWordsMax) {
        printf("(warning, parsing command filled word storage)\n");
        printf("(arguments %d and after were combined)\n", CommandWordsMax);
    }

    int found = 0;
    for(int which = 0; which < commandsCount; which++) {
        Command *cmd = commands + which;
        if(strcmp(words[0], cmd->name) == 0) {
            found = 1;
            if(wordsCount < cmd->minWords) {
                printf("expected at least %d words for command \"%s\", parsed only %d\n", cmd->minWords, cmd->name, wordsCount);
                usage();
            } else {
                return cmd->go(wordsCount, words);
            }
        }
    }

    if(!found) {
        printf("Unknown command \"%s\"\n", words[0]);
        usage();
    }
    return COMMAND_CONTINUE; // XXX COMMAND_UNKNOWN?
}

void process_local_key(unsigned char c)
{
    // XXX make this table driven, break into lots smaller functions
    if(c == '\r' || c == '\n') {
        putchar('\n');
        gMonitorCommandBuffer[gMonitorCommandBufferLength] = 0;

        processCommandLine(gMonitorCommandBuffer);
        printf("* ");
        gMonitorCommandBufferLength = 0;

    } else {

        if(c == 127 || c == '\b') {
            if(gMonitorCommandBufferLength > 0) {
                putchar('\b');
                putchar(' ');
                putchar('\b');
                gMonitorCommandBufferLength--;
            } else {
                bell();
            }
        } else {
            if(gMonitorCommandBufferLength < sizeof(gMonitorCommandBuffer) - 1) {
                putchar(c);
                gMonitorCommandBuffer[gMonitorCommandBufferLength++] = c;
            } else {
                bell();
            }
        }
    // TODO - upload data, write block
    }
}

// Scanout DMA

// XXX I Don't think there's initialization code in crt0 for ccmram
volatile int __attribute__((section (".ccmram"))) rowNumber;
volatile int __attribute__((section (".ccmram"))) fieldNumber;

void MemoryCopyDMA(unsigned char* dst, unsigned char* src, size_t size)
{
    // XXX wait on previous DMA
    // Configure DMA to copy tmp row
    DMA2_Stream1->CR &= ~DMA_SxCR_EN;       /* disable DMA2_1 */
    DMA2->LIFCR = 0xF00;                        /* clear flags */
    DMA2_Stream1->NDTR = size / 4;
    DMA2_Stream1->PAR = (uint32_t)src;        // Source buffer address 0 in Memory-to-Memory mode
    DMA2_Stream1->M0AR = (uint32_t)dst;        // Dest buffer address in Memory-to-Memory mode
    DMA2_Stream1->FCR = DMA_FIFOMODE_ENABLE |   // Enable FIFO to improve stutter
        0; // DMA_FIFO_THRESHOLD_FULL;        
    DMA2_Stream1->CR =
        DMA_CHANNEL_1 |                         
        DMA_MEMORY_TO_MEMORY |                  // Memory to Memory
        DMA_PDATAALIGN_WORD | // DMA_PBURST_INC4 |
        DMA_MDATAALIGN_WORD | // DMA_MBURST_INC4 |
        DMA_PRIORITY_LOW |
        DMA_PINC_ENABLE |                       // Increment memory address
        DMA_MINC_ENABLE |                       // Increment memory address
        0;
    DMA2_Stream1->CR |= DMA_SxCR_EN;    /* enable DMA */
}

void DMA2_Stream2_IRQHandler(void)
{
    if(markHandlerInSamples) {
        for(int i = 0; i < 28; i++) { GPIOC->ODR = 0xFFFFFFF8; }
    }

    if(DMA2->LISR &= DMA_FLAG_FEIF2_6) {
        DMA2->LIFCR |= DMA_LIFCR_CFEIF2;
        DMAFIFOUnderruns++;
    }
    if(DMA2->LISR &= DMA_FLAG_TEIF2_6) {
        DMA2->LIFCR |= DMA_LIFCR_CTEIF2;
        DMATransferErrors++;
    }

    // Configure timer TIM2 for performance measurement
    TIM2->SR = 0;                       /* reset status */
    TIM2->ARR = 0xFFFFFFFF;
    TIM2->CNT = 0;
    TIM2->CR1 = TIM_CR1_CEN;            /* enable the timer */

    int whichIsScanning = (DMA2_Stream2->CR & DMA_SxCR_CT) ? 1 : 0;

    unsigned char *nextRowBuffer = (whichIsScanning == 1) ? row0 : row1;

    for(int i = 0; i < SWATCH_SIZE; i++) {
        NTSCFillRowBuffer(fieldNumber, rowNumber, nextRowBuffer + ROW_SAMPLES * i);
        if(debugOverlayEnabled) {
            fillRowDebugOverlay(fieldNumber, rowNumber, nextRowBuffer + ROW_SAMPLES * i);
        }
        rowNumber = rowNumber + 1;
        if(rowNumber == NTSC_FIELD_LINES) {
            rowNumber = 0;
            fieldNumber++;
        }
    }

    /* wait until a little later to return */
    // while(TIM2->CNT < SystemCoreClock * 7 / (227 * 262 * 10) );

    rowCyclesSpent[rowNumber] = TIM2->CNT;
    TIM2->CR1 = 0;            /* stop the timer */

    // Clear interrupt flag
    // DMA2->LISR &= ~DMA_TCIF;
    DMA2->LIFCR |= DMA_LIFCR_CTCIF2;

    // A little pulse so we know where we are on the line
    if(markHandlerInSamples) {
        for(int i = 0; i < 28; i++) { GPIOC->ODR = 0xFFFFFFE8; }
    }
    /* wait until a little later to return */
    // while(DMA2_Stream2->NDTR > 200); // Causes dark banding - clocks drifted?  Or voltage drop?
}

void DMAStartScanout(uint32_t dmaCount)
{
    // Configure DAC
    GPIO_InitTypeDef  GPIO_InitStruct = {0};
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Pin = 0xFF;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct); 

    // Enable DMA interrupt handler
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 1);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

    // Configure DMA
    DMA2_Stream2->NDTR = ROW_SAMPLES * SWATCH_SIZE;
    DMA2_Stream2->M0AR = (uint32_t)row0;        // Source buffer address 0
    DMA2_Stream2->M1AR = (uint32_t)row1;        // Source buffer address 1 
    DMA2_Stream2->PAR = (uint32_t)&GPIOC->ODR;  // Destination address
    DMA2_Stream2->FCR = DMA_FIFOMODE_ENABLE |   // Enable FIFO to improve stutter
        DMA_FIFO_THRESHOLD_FULL;        
    DMA2_Stream2->CR =
        DMA_CHANNEL_6 |                         // which channel is driven by which timer to which peripheral is limited
        DMA_MEMORY_TO_PERIPH |                  // Memory to Peripheral
        DMA_PDATAALIGN_BYTE |                   // BYTES to peripheral
        DMA_MDATAALIGN_WORD | DMA_MBURST_INC4 |
        DMA_SxCR_DBM |                          // double buffer
        DMA_PRIORITY_VERY_HIGH |                // Video data must be highest priority, can't stutter
        DMA_MINC_ENABLE |                       // Increment memory address
        DMA_IT_TC |                             // Interrupt on transfer complete of each buffer 
        0;

    // Configure TIM1_CH2 to drive DMA
    TIM1->CCR2 = (dmaCount + 1) / 2;         /* 50% duty cycle, although I've tried 1 and dmacount-1 and nothing seems to change.  Is this used? */ 
    TIM1->CCMR1 |= TIM_CCMR1_OC2FE;    /* fast enable...? */
    TIM1->CCER |= TIM_CCER_CC2E;        /* enable capture/compare CH2 */
    TIM1->DIER |= TIM_DIER_CC2DE;       /* enable capture/compare updates DMA */

    // Configure timer TIM1
    TIM1->SR = 0;                       /* reset status */
    TIM1->ARR = dmaCount - 1;           /* load the register with this */

    rowNumber = 1; // Next up is row 1
    fieldNumber = 0; 

    // Clear FIFO and transfer error flags
    DMA2->LIFCR |= DMA_LIFCR_CFEIF2;
    DMA2->LIFCR |= DMA_LIFCR_CTEIF2;

    DMA2_Stream2->CR |= DMA_SxCR_EN;    /* enable DMA */
    TIM1->CR1 = TIM_CR1_CEN;            /* enable the timer */
}

void DMAStopScanout()
{
    DMA2_Stream2->CR &= ~DMA_SxCR_EN;       /* disable DMA */
    TIM1->CR1 &= ~TIM_CR1_CEN;            /* disable the timer */
}

uint32_t /* __attribute__((section (".ccmram"))) */ vectorTable[100] __attribute__ ((aligned (512)));

extern int KBDInterrupts;
extern int UARTInterrupts;
int main()
{
    memcpy(font8x16Bits, font8x16BitsSrc, sizeof(font8x16BitsSrc));

    HAL_Init();

    uint32_t* oldVectorTable = (uint32_t*)SCB->VTOR;
    memcpy(vectorTable, oldVectorTable, sizeof(vectorTable));
    SCB->VTOR = (uint32_t)vectorTable; // This didn't help glitching, and can't be in *data* CCM RAM

    SystemClock_Config(); // XXX resets all CCM variables.

    LED_init();
    LED_beat_heart();

    MON_init();
    console_queue_init();
    LED_beat_heart();

    setbuf(stdout, NULL);

    SERIAL_init(); // transmit and receive but global interrupts disabled
    LED_beat_heart();


    printf("\n\nAlice 3 I/O firmware, %s\n", IOBOARD_FIRMWARE_VERSION_STRING);
    printf("System core clock: %lu Hz, %lu MHz\n", SystemCoreClock, SystemCoreClock / 1000000);

    float clock = 14.318180;
    NTSCCalculateParameters(clock);

    printf("calculated NTSCLineClocks = %d\n", NTSCLineClocks);
    printf("calculated NTSCHSyncClocks = %d\n", NTSCHSyncClocks);
    printf("calculated NTSCFrontPorchClocks = %d\n", NTSCFrontPorchClocks);
    printf("calculated NTSCBackPorchClocks = %d\n", NTSCBackPorchClocks);
    printf("calculated NTSCEqPulseClocks = %d\n", NTSCEqPulseClocks);
    printf("calculated NTSCVSyncClocks = %d\n", NTSCVSyncClocks);

    LED_beat_heart();
    SERIAL_flush();

    SPI_config_for_sd();
    LED_beat_heart();

    if(!SDCARD_init()) {
        printf("Failed to start access to SD card SPI!!\n");
        LED_beat_heart();
        SERIAL_flush();
    } else  {
        printf("Opened SD Card\n");
        LED_beat_heart();
        SERIAL_flush();

        FRESULT result = f_mount(&gFATVolume, "0:", 1);
        if(result != FR_OK) {
            logprintf(DEBUG_ERRORS, "ERROR: FATFS mount result is %d\n", result);
            panic();
        } else {
            printf("Mounted FATFS from SD card successfully.\n");
        }
        LED_beat_heart();
        SERIAL_flush();
    }

    KBD_init();
    LED_beat_heart();

    NTSCGenerateLineBuffers();
    NTSCFillRowBuffer(0, 0, row0);
    memset(row0, 0x10, sizeof(row0));
    memset(row1, 0xF0, sizeof(row1));

    float colorBurstInCoreClocks = SystemCoreClock / 14318180.0;
    // need main clock as close as possible to @ 171.816
    uint32_t DMACountAt14MHz = (colorBurstInCoreClocks + .5);
    // printf("DMACountAt14MHz = %lu, expected %d\n", DMACountAt14MHz, clockConfigs[whichConfig].DMA_BEATS);
    DMACountAt14MHz = clockConfigs[whichConfig].DMA_BEATS; // (colorBurstInCoreClocks + .5);

    DMAStartScanout(DMACountAt14MHz);

    // Wait for a click to progress to the next clock config
    { 
        GPIO_InitTypeDef  GPIO_InitStruct;

        GPIO_InitStruct.Pin = GPIO_PIN_9;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_PULLDOWN;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct); 
    }
    delay_ms(50);

    // If button is pushed down, enter clock config inspector
    if(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9)) {

        debugOverlayEnabled = 1;
        memset(debugDisplay, 0, debugDisplayWidth * debugDisplayHeight);

        int debugLine = 0;
        sprintf(debugDisplay[debugLine++], "Release button");
        sprintf(debugDisplay[debugLine++], "For ClockConfig");
        sprintf(debugDisplay[debugLine++], "Inspector");

        delay_ms(50);
        while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9));
        delay_ms(50);

        int counter = 0; 
        for(;;) {

            whichConfig = counter;

            DMAStopScanout();

            // Probably will break UART and SD and GPIOs:
            DeInitRCCAndPLL();
            SystemClock_Config();

            // Set up LED that toggles for every mode we look at
            GPIO_InitTypeDef  GPIO_InitStruct = {0};
            GPIO_InitStruct.Pull = GPIO_NOPULL;
            GPIO_InitStruct.Pin = 0x100;
            GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
            GPIO_InitStruct.Speed = GPIO_SPEED_LOW;
            HAL_GPIO_Init(GPIOC, &GPIO_InitStruct); 
            GPIOC->ODR = GPIOC->ODR & ~0x100;

            if(counter % 2 != 0) {
                GPIOC->ODR |= 0x100; // PC8
            }

            float colorBurstInCoreClocks = SystemCoreClock / 14318180.0;
            // need main clock as close as possible to @ 171.816
            uint32_t DMACountAt14MHz = (colorBurstInCoreClocks + .5);
            // printf("DMACountAt14MHz = %lu, expected %d\n", DMACountAt14MHz, clockConfigs[whichConfig].DMA_BEATS);
            DMACountAt14MHz = clockConfigs[whichConfig].DMA_BEATS; // (colorBurstInCoreClocks + .5);

            DMAStartScanout(DMACountAt14MHz);

            debugOverlayEnabled = 1;
            memset(debugDisplay, 0, debugDisplayWidth * debugDisplayHeight);

            int debugLine = 0;
            sprintf(debugDisplay[debugLine++], "Number %d", whichConfig);
            sprintf(debugDisplay[debugLine++], "PLL_M %lu", clockConfigs[whichConfig].PLL_M);
            sprintf(debugDisplay[debugLine++], "PLL_N %lu", clockConfigs[whichConfig].PLL_N);
            sprintf(debugDisplay[debugLine++], "PLL_P %lu", clockConfigs[whichConfig].PLL_P);
            sprintf(debugDisplay[debugLine++], "(DMA_BEATS %lu)", clockConfigs[whichConfig].DMA_BEATS);
            uint32_t at14MHz = (colorBurstInCoreClocks + .5);
            sprintf(debugDisplay[debugLine++], "calcd DMA BEATS %lu", at14MHz);
            sprintf(debugDisplay[debugLine++], "CPU clock: %d", (int)(clockConfigs[whichConfig].CPUFreq));
            sprintf(debugDisplay[debugLine++], "SCClock: %lu", SystemCoreClock);
            int clockConfigsWhole = clockConfigs[whichConfig].colorburstClock;
            sprintf(debugDisplay[debugLine++], "color %d.%03d", clockConfigsWhole, (int)((clockConfigs[whichConfig].colorburstClock - clockConfigsWhole) * 100000.0f));

            // Wait for a click to progress to the next clock config
            { 
                GPIO_InitTypeDef  GPIO_InitStruct;

                GPIO_InitStruct.Pin = GPIO_PIN_9;
                GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
                GPIO_InitStruct.Pull = GPIO_PULLDOWN;
                GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
                HAL_GPIO_Init(GPIOC, &GPIO_InitStruct); 
            }
            while(!HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9)) {
                delay_ms(50);
            }
            delay_ms(250);

            counter++;
            if(counter >= clockConfigCount) {
                memset(debugDisplay, 0, debugDisplayWidth * debugDisplayHeight);
                int debugLine = 0;
                sprintf(debugDisplay[debugLine++], "END OF CONFIGS");
                sprintf(debugDisplay[debugLine++], "HOORAY!");
                break;
            }
        }
        while(1);
    }

    // Must have been quantized to 256 colors!
    if(readImage("test.rgb.bin")) {
        printf("failed to load \"test.rgb.bin\"\n");
    }

    printf("* ");
    SERIAL_flush();

    for(;;) {

        // Should be in VBlank callback so is continuously updated
        if(showScanoutStats) {
            debugOverlayEnabled = 1;
            uint32_t currentField = fieldNumber;
            while(currentField == fieldNumber); // Wait for VBLANK
            memset(debugDisplay, 0, debugDisplayWidth * debugDisplayHeight);
            sprintf(debugDisplay[debugDisplayHeight - 4], "UART %d", UARTInterrupts);
            sprintf(debugDisplay[debugDisplayHeight - 3], "KBD %d", KBDInterrupts);
            sprintf(debugDisplay[debugDisplayHeight - 2], "FIFO unders %lu", DMAFIFOUnderruns);
            sprintf(debugDisplay[debugDisplayHeight - 1], "DMA errors %lu", DMATransferErrors);
        }
        int key;

        SERIAL_try_to_transmit_buffers();
        // LED_beat_heart();

        SERIAL_poll_continue();

        process_monitor_queue();

        key = KBD_process_queue(gDumpKeyboardData);
        if(key >= 0) {
            disable_interrupts();
            // console_enqueue_key_unsafe(key);
            monitor_enqueue_key_unsafe(key);
            enable_interrupts();
        }

        check_exceptional_conditions();
    }

    // should not reach
    panic();
}
