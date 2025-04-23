/*****************************************************************************
 *   ODTWARZACZ MP3
 *
 *   zespół D03
 *   Klaudia Łuczak 251575
 *   Julia Rzeźniczak 251624
 *   Hubert Pacyna 251603
 *   Piotr Wlazło 251662
 ******************************************************************************/

#include "lpc17xx_pinsel.h"		/* konfiguracja pinów */
#include "lpc17xx_gpio.h"		/* obsługa GPIO */
#include "lpc17xx_ssp.h"		/* obsługa interfejsu SPI (dla karty SD)*/
#include "lpc17xx_uart.h"		/* obsługa UART (komunikacja szeregowa) */
#include "lpc17xx_timer.h"		/* obsługa timera */
#include "stdio.h"				/* standardowa biblio wejścia wyjścia */
#include "lpc17xx_adc.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_rtc.h"
#include "lpc17xx_clkpwr.h"


#include "diskio.h"				/* obsługa systemu plików FAT i nośnika SD */
#include "ff.h"

#include "oled.h"				/* obsluga OLEDu */
#include "acc.h"

#include "light.h"				/* biblioteka od czujnika swiatla */

#define UART_DEV LPC_UART3		/* def UART3 jako domyślne urządzenie do komunikacji szeregowej */

static FILINFO Finfo;			/* przechowanie inf o plikach */
static FATFS Fatfs[1];			/* reprezentacja systemu plików FAT */
static uint8_t buf[64];			/* bufor do danych wysyłanych przez UART */

uint8_t bufor1[512];
uint8_t bufor2[512];

void
TIMER1_IRQHandler (void)
{
  if (TIM_GetIntStatus (LPC_TIM1, TIM_MR0_INT))
    {

      TIM_ClearIntPending (LPC_TIM1, TIM_MR0_INT);
    }
  if (TIM_GetIntStatus (LPC_TIM1, TIM_MR1_INT))
    {

      TIM_ClearIntPending (LPC_TIM1, TIM_MR1_INT);
    }
}

static void
init_Timer (void)
{
  TIM_TIMERCFG_Type Config;
  TIM_MATCHCFG_Type Match_Cfg;

  Config.PrescaleOption = TIM_PRESCALE_USVAL;
  Config.PrescaleValue = 1000; // czyli 1 milisekunda

  //najpierw włączyć zasilanie
  CLKPWR_SetPCLKDiv (CLKPWR_PCLKSEL_TIMER1, CLKPWR_PCLKSEL_CCLK_DIV_1);
  // Ustawić timer.
  TIM_Cmd (LPC_TIM1, DISABLE); // to w zasadzie jest niepotrzebne, ponieważ po włączeniu zasilania timer jest nieczynny,
  TIM_Init (LPC_TIM1, TIM_TIMER_MODE, &Config);

  Match_Cfg.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
  Match_Cfg.IntOnMatch = TRUE;
  Match_Cfg.ResetOnMatch = FALSE;
  Match_Cfg.StopOnMatch = FALSE;
  Match_Cfg.MatchChannel = 0;
  Match_Cfg.MatchValue = 200; // Czyli 200 ms.
  TIM_ConfigMatch (LPC_TIM1, &Match_Cfg);
  Match_Cfg.ResetOnMatch = TRUE;
  Match_Cfg.MatchChannel = 1;
  Match_Cfg.MatchValue = 2000; // czyli 2 s.
  TIM_ConfigMatch (LPC_TIM1, &Match_Cfg);
  TIM_Cmd (LPC_TIM1, ENABLE);
  // I odblokować przerwania w VIC
  NVIC_EnableIRQ (TIMER1_IRQn);
}

static void init_rtc(void)
{
    RTC_Init(LPC_RTC);

    RTC_ResetClockTickCounter(LPC_RTC);
    RTC_Cmd(LPC_RTC, ENABLE);

    RTC_TIME_Type RTCTime;
    RTCTime.SEC = 0;
    RTCTime.MIN = 37;
    RTCTime.HOUR = 21;
    RTCTime.DOM = 9;
    RTCTime.DOW = 6;
    RTCTime.DOY = 92;
    RTCTime.MONTH = 4;
    RTCTime.YEAR = 2005;

    RTC_SetFullTime(LPC_RTC, &RTCTime);
}

static void init_uart(void)		/* inicjalizacja UART */
{
	PINSEL_CFG_Type PinCfg;
	UART_CFG_Type uartCfg;

	/* Initialize UART3 pin connect */		/* konfiguruje UART3 na P0.0 i P0.1 */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin(&PinCfg);

	uartCfg.Baud_rate = 115200;		/* ustawia prędkość transmisji */
	uartCfg.Databits = UART_DATABIT_8;		/* ustawia 8 bitów danych */
	uartCfg.Parity = UART_PARITY_NONE;		/* ustawia brak parzystości */
	uartCfg.Stopbits = UART_STOPBIT_1;		/* ustawia 1 bit stopu */

	UART_Init(UART_DEV, &uartCfg);		/* inicjalizuje UART */

	UART_TxCmd(UART_DEV, ENABLE);		/* włącza transmisję */

}


static void init_ssp(void)		/* inicjalizacja interfejsu SPI */
{
	SSP_CFG_Type SSP_ConfigStruct;		/* deklaracja struktury konfiguracyjej dla SPI */
	PINSEL_CFG_Type PinCfg;				/* i pinów */

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK (zegar SPI)
	 * P0.8 - MISO (dane odczytywane)
	 * P0.9 - MOSI (dane wysyłane)
	 * P2.2 ustawiony jako GPIO dla SSEL (chip select)
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);		/* inicjalizuje SPI i włącza go (kod do końca) */

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

void SysTick_Handler(void) {		/* obsługa przerwania SysTick */
    disk_timerproc();
}


static uint32_t msTicks = 0;


static uint32_t getTicks(void) {
    return msTicks;
}

static void init_i2c(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void) {
	PINSEL_CFG_Type PinCfg;

	/*
	 * Init ADC pin connect
	 * AD0.0 on P0.23
	 */
	PinCfg.Funcnum = 1;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Pinnum = 23;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);

	/* Configuration for ADC :
	 * 	Frequency at 0.2Mhz
	 *  ADC channel 0, no Interrupt
	 */
	ADC_Init(LPC_ADC, 200000);
	ADC_IntConfig(LPC_ADC,ADC_CHANNEL_0,DISABLE);
	ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_0,ENABLE);

}

int main (void) {
    DSTATUS stat;		/* zmienne do obsługi systemu plików FAT */
    DWORD p2;
    WORD w1;
    BYTE res, b1;
    DIR dir;

    int i = 0;

    int32_t xoff = 0;
    int32_t yoff = 0;
    int32_t zoff = 0;

    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

    int32_t t = 0;
    uint32_t lux = 0;
    uint32_t trim = 0;

    init_uart();
    init_i2c();
    init_ssp();		/* inicjalizacja SPI i UART */
    init_adc();

    init_rtc();

    oled_init();
    light_init();
    acc_init();

    light_init();	/* inicjalizacja czujnika swiatla */

    UART_SendString(UART_DEV, (uint8_t*)"MMC/SD example\r\n");		/* wysyła kominikat przez UART */

    SysTick_Config(SystemCoreClock / 100);		/* konfiguruje SysTick i czeka 500s */

    Timer0_Wait(500);

    stat = disk_initialize(0);		/* inicjalizacja karty SD */

	/* sprawdzamy czy karta SD jest dostępna */
    if (stat & STA_NOINIT) {
    	UART_SendString(UART_DEV,(uint8_t*)"MMC: not initialized\r\n");
    }

    if (stat & STA_NODISK) {
    	UART_SendString(UART_DEV,(uint8_t*)"MMC: No Disk\r\n");
    }

    if (stat != 0) {
        return 1;
    }

    UART_SendString(UART_DEV,(uint8_t*)"MMC: Initialized\r\n");

	/* pobiera liczbę sektorow na karcie SD i wysyła przez UART */
    if (disk_ioctl(0, GET_SECTOR_COUNT, &p2) == RES_OK) {
        i = sprintf((char*)buf, "Drive size: %d \r\n", p2);
        UART_Send(UART_DEV, buf, i, BLOCKING);
    }

	/* pobiera rozmiar sektora */
    if (disk_ioctl(0, GET_SECTOR_SIZE, &w1) == RES_OK) {
        i = sprintf((char*)buf, "Sector size: %d \r\n", w1);
        UART_Send(UART_DEV, buf, i, BLOCKING);
    }


    if (disk_ioctl(0, GET_BLOCK_SIZE, &p2) == RES_OK) {
        i = sprintf((char*)buf, "Erase block size: %d \r\n", p2);
        UART_Send(UART_DEV, buf, i, BLOCKING);
    }

    if (disk_ioctl(0, MMC_GET_TYPE, &b1) == RES_OK) {
        i = sprintf((char*)buf, "Card type: %d \r\n", b1);
        UART_Send(UART_DEV, buf, i, BLOCKING);
    }

    /* montuje system plików FAT */
    res = f_mount(0, &Fatfs[0]);
    if (res != FR_OK) {
        i = sprintf((char*)buf, "Failed to mount 0: %d \r\n", res);
        UART_Send(UART_DEV, buf, i, BLOCKING);
        return 1;
    }

    /* otwiera katalog główny karty SD */
    res = f_opendir(&dir, "/");
    if (res) {
        i = sprintf((char*)buf, "Failed to open /: %d \r\n", res);
        UART_Send(UART_DEV, buf, i, BLOCKING);
        return 1;
    }

    RTC_TIME_Type currentTime;
    uint8_t timeStr[40];

    oled_clearScreen(OLED_COLOR_WHITE);

    /* odczytuje i wypisuje nazwy plików z katalogu głównego */
    for(int i = 1; i < 10; i++) {
		res = f_readdir(&dir, &Finfo);
		if (Finfo.fname[0] == '_' || (Finfo.fattrib & AM_DIR)) {
			i--;
			continue;
		}
		if ((res != FR_OK) || !Finfo.fname[0]) break;
		oled_putString(1,1 + i * 8, Finfo.fname, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	};

    acc_read(&x, &y, &z);
    xoff = 0-x;
    yoff = 0-y;
    zoff = 64-z;

    light_enable();						/* aktywowanie czujnika swiatla */
    light_setRange(LIGHT_RANGE_4000);	/* ustawienie zakresu swiatla do 4000 luksow */

	RTC_GetFullTime(LPC_RTC, &currentTime);

	sprintf((char*)timeStr, "%02d-%02d-%04d %02d:%02d:%02d", currentTime.DOM, currentTime.MONTH, currentTime.YEAR, currentTime.HOUR, currentTime.MIN, currentTime.SEC);
	oled_putString(0, 49, timeStr, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_horizontalLeftScroll(0x06, 0x07);		/* 6 i 7 bo to numery segmentow ekranu OLED (kazdy segment to 8 pikseli) */

	init_Timer();

    while(1) {

		lux = light_read(); /* pomiar swiatla */

		if (lux < 100) {
			oled_setInvertDisplay();
		} else {
		    oled_setNormalDisplay();
		}

		Timer0_Wait(1000);
    };

}
