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
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"		/* obsługa interfejsu SPI (dla karty SD)*/
#include "lpc17xx_uart.h"		/* obsługa UART (komunikacja szeregowa) */
#include "lpc17xx_timer.h"
#include "stdio.h"				/* standardowa biblio wejścia wyjścia */
#include "lpc17xx_adc.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_rtc.h"
#include "lpc17xx_clkpwr.h"
#include "stdbool.h"
#include "joystick.h"

#include "diskio.h"				/* obsługa systemu plików FAT i nośnika SD */
#include "ff.h"

#include "oled.h"
#include "acc.h"
#include "light.h"				/* biblioteka od czujnika swiatla */
#include "rotary.h"

#define UART_DEV LPC_UART3		/* def UART3 jako domyślne urządzenie do komunikacji szeregowej */
#define SAMPLE_RATE 8000
#define DELAY 1000000 / 8000

#define BUF_SIZE ((uint32_t)4096)
#define MAX_SONGS 5
#define MAX_NAME_LEN 13


static uint8_t buf[64];			/* bufor do danych wysyłanych przez UART */
static uint8_t songs = 0;
static char songsList[MAX_SONGS][MAX_NAME_LEN];

uint8_t bufor1[BUF_SIZE];
uint8_t bufor2[BUF_SIZE];

volatile bool bufor1_pusty = true;
volatile bool bufor2_pusty = true;
volatile int aktualnyBufor = 0;

uint32_t dataOffset = 0;
FIL wavFile;

void TIMER1_IRQHandler(void) {
    static uint32_t pozycja = 0;

    if (TIM_GetIntStatus(LPC_TIM1, TIM_MR0_INT)) {
        if (aktualnyBufor == 0) {
            DAC_UpdateValue(LPC_DAC, bufor1[pozycja]);
        } else {
            DAC_UpdateValue(LPC_DAC, bufor2[pozycja]);
        }

        pozycja++;

        if (pozycja >= BUF_SIZE) {
            pozycja = 0;
            if (aktualnyBufor == 0) {
            	bufor1_pusty = true;
            }
            else {
            	bufor2_pusty = true;
            }
            aktualnyBufor ^= 1;
        }

        TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
    }
}

static void init_Timer (void)
{
  TIM_TIMERCFG_Type Config;
  TIM_MATCHCFG_Type Match_Cfg;

  Config.PrescaleOption = TIM_PRESCALE_USVAL;
  Config.PrescaleValue = 1; 					// czyli 1 mikrosek

  //najpierw włączyć zasilanie
  CLKPWR_SetPCLKDiv (CLKPWR_PCLKSEL_TIMER1, CLKPWR_PCLKSEL_CCLK_DIV_4);
  // Ustawić timer.
  TIM_Cmd (LPC_TIM1, DISABLE); 					// to w zasadzie jest niepotrzebne, ponieważ po włączeniu zasilania timer jest nieczynny,
  TIM_Init (LPC_TIM1, TIM_TIMER_MODE, &Config);

  Match_Cfg.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
  Match_Cfg.IntOnMatch = TRUE;
  Match_Cfg.ResetOnMatch = TRUE;
  Match_Cfg.StopOnMatch = FALSE;
  Match_Cfg.MatchChannel = 0;
  Match_Cfg.MatchValue = 125; 					// Czyli 125  us.
  TIM_ConfigMatch (LPC_TIM1, &Match_Cfg);
  TIM_Cmd (LPC_TIM1, ENABLE);
  NVIC_EnableIRQ (TIMER1_IRQn);  				// I odblokować przerwania w VIC
}

static void init_rtc(void)
{
    RTC_Init(LPC_RTC);

    RTC_ResetClockTickCounter(LPC_RTC);
    RTC_Cmd(LPC_RTC, ENABLE);

    char uartBuf[32];
    int sec = 0;
    int min = 0;
    int hour = 0;
    int dom = 0;
    int month = 0;
    int year = 0;

    int i = 0;
    char c;
    while (i < 31) {
        while (!UART_Receive(UART_DEV, (uint8_t*)&c, 1, NONE_BLOCKING));
        if (c == '\n' || c == '\r') break;
        uartBuf[i++] = c;
    }
    uartBuf[i] = '\0';

    if (sscanf(uartBuf,"%d-%d-%d %d:%d:%d", &year, &month, &dom, &hour, &min, &sec) != 3) {
        sec = 00;
        min = 37;
        hour = 21;
        dom = 9;
        month = 4;
        year = 2005;
    }

    RTC_TIME_Type RTCTime;
    RTCTime.SEC = sec;
    RTCTime.MIN = min;
    RTCTime.HOUR = hour;
    RTCTime.DOM = dom;
    RTCTime.DOW = 6;
    RTCTime.DOY = 92;
    RTCTime.MONTH = month;
    RTCTime.YEAR = year;

    RTC_SetFullTime(LPC_RTC, &RTCTime);
}

static void init_uart(void)
{
	PINSEL_CFG_Type PinCfg;
	UART_CFG_Type uartCfg;

	// konfiguruje UART3 na P0.0 i P0.1
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin(&PinCfg);

	uartCfg.Baud_rate = 115200;				// ustawia prędkość transmisji
	uartCfg.Databits = UART_DATABIT_8;		// ustawia 8 bitów danych
	uartCfg.Parity = UART_PARITY_NONE;		// ustawia brak parzystości
	uartCfg.Stopbits = UART_STOPBIT_1;		// ustawia 1 bit stopu

	UART_Init(UART_DEV, &uartCfg);			// inicjalizuje UART

	UART_TxCmd(UART_DEV, ENABLE);			// włącza transmisję
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

	SSP_ConfigStructInit(&SSP_ConfigStruct);		// inicjalizuje SPI i włącza go (kod do końca)
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);			// Initialize SSP peripheral with parameter given in structure above
	SSP_Cmd(LPC_SSP1, ENABLE);						// Enable SSP peripheral
}

static void init_dac (void) {
   PINSEL_CFG_Type PinCfg;

	/*
	 * Init DAC pin connect
	 * AOUT on P0.26
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Pinnum = 26;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);

	/* init DAC structure to default
	 * Maximum current is 700 uA
	 * First value to AOUT is 0
	 */
	DAC_Init(LPC_DAC);

	// Inicjalizacja GPIO dla kontroli wzmacniacza audio LM4811
	GPIO_SetDir(0, 1UL << 27u, 1u); // LM4811-clk
	GPIO_SetDir(0, 1UL << 28u, 1u); // LM4811-up/dn
	GPIO_SetDir(2, 1UL << 13u, 1u); // LM4811-shutdn

	GPIO_ClearValue(0, 1UL << 27u); // LM4811-clk
	GPIO_ClearValue(0, 1UL << 28u); // LM4811-up/dn
	GPIO_ClearValue(2, 1UL << 13u); // LM4811-shutdn
}

void SysTick_Handler(void) {		// obsługa przerwania SysTick
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

	I2C_Init(LPC_I2C2, 100000);				// Initialize I2C peripheral
	I2C_Cmd(LPC_I2C2, ENABLE);				// Enable I2C1 operation
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

static uint8_t parseWavHeader(FIL* file) {
	uint8_t header[44];
	UINT bytesRead;
	uint32_t sampleRate = 0;

	f_read(file, header, 44, &bytesRead);

	if (bytesRead != 44) {
	    UART_SendString(UART_DEV, (const uint8_t*)"Failed to open\r\n");
	}

	if ((header[0] != (uint8_t)'R') || (header[1] != (uint8_t)'I') ||(header[2] != (uint8_t)'F') || (header[3] != (uint8_t)'F')) {
        UART_SendString(UART_DEV, (const uint8_t*)"Wrong format\r\n");
	}

	if ((header[8] != (uint8_t)'W') || (header[9] != (uint8_t)'A') || (header[10] != (uint8_t)'V') || (header[11] != (uint8_t)'E')) {
		UART_SendString(UART_DEV, (const uint8_t*)"File is not .wav\r\n");
	}

    if ((header[12] != (uint8_t)'f') || (header[13] != (uint8_t)'m') ||(header[14] != (uint8_t)'t') || (header[15] != (uint8_t)' ')) {
        UART_SendString(UART_DEV, (const uint8_t*)"Missing fmt\r\n");
    }

    sampleRate = (header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24));

    if (sampleRate != (uint32_t)SAMPLE_RATE) {
        UART_SendString(UART_DEV, (const uint8_t*)"File not in 8kHz\r\n");
    }

    uint32_t offset = 36;

    while (offset < 100U) {
    	if ((header[offset] == (uint8_t)'d') && (header[offset+1U] == (uint8_t)'a') && (header[offset+2U] == (uint8_t)'t') && (header[offset+3U] == (uint8_t)'a')) {
            dataOffset = offset + 8U;
            break;
        }
        offset++;
    }

    if (offset >= 100U) {
        UART_SendString(UART_DEV, (const uint8_t*)"Missing data chunk\r\n");
        return 0;
    }

    f_lseek(file, dataOffset);

    return 1;
}

static void playWavFile(char* filename) {
	BYTE res;
	UINT bytesRead;

    res = f_open(&wavFile, filename, FA_READ);
    if (res != FR_OK) {
        sprintf((char*)buf, "Failed to open file: %s, error: %d\r\n", filename, res);
        UART_Send(UART_DEV, buf, strlen((char*)buf), BLOCKING);
        return;
    }

    if (!parseWavHeader(&wavFile)) {
        UART_SendString(UART_DEV, (const uint8_t*)"Invalid WAV file format\r\n");
        f_close(&wavFile);
        return;
    }

    res = f_read(&wavFile, bufor1, sizeof(bufor1), &bytesRead);
    bufor1_pusty = false;
    __enable_irq();

    while (1) {
    	changeLight();
    	if (bufor1_pusty) {
    		res = f_read(&wavFile, bufor1, sizeof(bufor1), &bytesRead);
    		bufor1_pusty = false;
    	}

    	if (bufor2_pusty) {
    		res = f_read(&wavFile, bufor2, sizeof(bufor2), &bytesRead);
    		bufor2_pusty = false;
    	}
        if ((res != FR_OK) || (bytesRead == 0) || (bytesRead == wavFile.fsize)) {
            break;
        }

    	changeVolume(rotary_read());
    }

    __disable_irq();
    f_close(&wavFile);
}

static void init_amp(void){
	GPIO_SetDir(0, 1UL << 27, 1u); // clock
	GPIO_SetDir(0, 1UL << 28, 1u); // up/down
	GPIO_SetDir(2, 1UL << 13, 1u); // shutdown

	GPIO_ClearValue(2, 1UL << 13);	// 0 na shutdown, czyli włączenie
}

void changeVolume(uint8_t rotaryDir)
{
	// początkowo clock=0 i UP=1
	if (rotaryDir == ROTARY_RIGHT) {
	    GPIO_SetValue(0, 1UL << 28); // up
	}
	else if (rotaryDir == ROTARY_LEFT) {
	    GPIO_ClearValue(0, 1UL << 28); // down
	} else {
	    return;
	}

	GPIO_SetValue(0, 1UL << 27);
	Timer0_us_Wait(100);
	GPIO_ClearValue(0, 1UL << 27);
	GPIO_SetValue(0, 1UL << 28); // up
}

static int chooseSong(uint8_t songIndex)
{
    uint8_t joy = 0;
//	while(1)
//	{
		joy = joystick_read();

		if ((joy & JOYSTICK_CENTER) != 0) {
	    	playWavFile(songsList[songIndex]);
//			break;
		}

		else if ((joy & JOYSTICK_DOWN) != 0) {
			if(songIndex == (songs - 1U)) {
				songIndex = 0;
			}
			else {
				songIndex++;
			}
		}

		else if ((joy & JOYSTICK_UP) != 0) {
			if(songIndex == 0U) {
				songIndex = songs - 1U;
			}
			else {
				songIndex--;
			}
		}
		Timer0_Wait(200);
//	}

	return songIndex;
}

//void selected(uint8_t nameIndex) {
//	oled_putString(0, nameIndex * 8 + 1, (uint8_t*) songsList[nameIndex], OLED_COLOR_WHITE, OLED_COLOR_BLACK);
//}
//
//void unselected(uint8_t nameIndex) {
//	oled_putString(0, nameIndex * 8 + 1, (uint8_t*) songsList[nameIndex], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
//}

void changeLight(void) {
	uint32_t lux = 0;
	lux = light_read(); // pomiar swiatla

	if (lux < 100U) {
		oled_setInvertDisplay();
	} else {
		oled_setNormalDisplay();
	}
	Timer0_Wait(100);
}

int main (void) {
    DSTATUS stat;		// zmienne do obsługi systemu plików FAT
    DWORD p2;
    WORD w1;
    BYTE res;
    BYTE b1;
    DIR dir;
    FILINFO Finfo;			// przechowanie inf o plikach
    FATFS Fatfs[1];			// reprezentacja systemu plików FAT

    int i = 0;

    int32_t xoff = 0;
    int32_t yoff = 0;
    int32_t zoff = 0;
    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;

    int32_t t = 0;
    uint32_t trim = 0;

    init_uart();
    init_i2c();
    init_ssp();
    init_adc();
    init_dac();
    init_rtc();
    init_Timer();
    oled_init();
    light_init();
    acc_init();
    light_init();
	init_amp();
	rotary_init();
	joystick_init();

    UART_SendString(UART_DEV, (const uint8_t*)"MMC/SD example\r\n");

    SysTick_Config(SystemCoreClock / 100);		// konfiguruje SysTick i czeka 500s
    Timer0_Wait(500);

    stat = disk_initialize(0);		// inicjalizacja karty SD

	// sprawdzamy czy karta SD jest dostępna
    if (stat & STA_NOINIT) {
    	UART_SendString(UART_DEV,(const uint8_t*)"MMC: not initialized\r\n");
    }

    if (stat & STA_NODISK) {
    	UART_SendString(UART_DEV,(const uint8_t*)"MMC: No Disk\r\n");
    }

    if (stat != 0) {
        return 1;
    }

    UART_SendString(UART_DEV,(const uint8_t*)"MMC: Initialized\r\n");

	// pobiera liczbę sektorow na karcie SD i wysyła przez UART
    if (disk_ioctl(0, GET_SECTOR_COUNT, &p2) == RES_OK) {
        i = sprintf((char*)buf, "Drive size: %d \r\n", p2);
        UART_Send(UART_DEV, buf, i, BLOCKING);
    }

	// pobiera rozmiar sektora
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

    // montuje system plików FAT
    res = f_mount(0, &Fatfs[0]);
    if (res != FR_OK) {
        i = sprintf((char*)buf, "Failed to mount 0: %d \r\n", res);
        UART_Send(UART_DEV, buf, i, BLOCKING);
        return 1;
    }

    // otwiera katalog główny karty SD
    res = f_opendir(&dir, "/");
    if (res) {
        i = sprintf((char*)buf, "Failed to open /: %d \r\n", res);
        UART_Send(UART_DEV, buf, i, BLOCKING);
        return 1;
    }
    RTC_TIME_Type currentTime;
    uint8_t timeStr[40];

    oled_clearScreen(OLED_COLOR_WHITE);

    for(int i = 0; i < 10; i++) {
		res = f_readdir(&dir, &Finfo);
		if ((Finfo.fname[0] == '_') || (Finfo.fattrib & AM_DIR)) {
			continue;
		}
		if ((res != FR_OK) || !Finfo.fname[0]) break;
		oled_putString(1,1 + (i * 8), Finfo.fname, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		uint8_t nameLen = strnlen(Finfo.fname, sizeof(Finfo.fname));
		strncpy(songsList[songs], Finfo.fname, nameLen+1U);
		songs++;
	};

    acc_read(&x, &y, &z);
    xoff = 0 - x;
    yoff = 0 - y;
    zoff = 64 - z;

    light_enable();						// aktywowanie czujnika swiatla
    light_setRange(LIGHT_RANGE_4000);	// ustawienie zakresu swiatla do 4000 luksow

	RTC_GetFullTime(LPC_RTC, &currentTime);

	sprintf((char*)timeStr, "%02d-%02d-%04d %02d:%02d:%02d", currentTime.DOM, currentTime.MONTH, currentTime.YEAR, currentTime.HOUR, currentTime.MIN, currentTime.SEC);
	oled_putString(0, 49, timeStr, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_horizontalLeftScroll(0x06, 0x07);		// 6 i 7 bo to numery segmentow ekranu OLED (kazdy segment to 8 pikseli)

	init_Timer();

	uint8_t songIndex = 0;
	//selected(songIndex);
    while(1) {
    	changeLight();
    	songIndex = chooseSong(songIndex);
    };
}
