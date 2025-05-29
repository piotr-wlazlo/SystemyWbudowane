/*****************************************************************************
 *   ODTWARZACZ MP3
 *
 *   zespół D03
 *   Klaudia Łuczak 251575
 *   Julia Rzeźniczak 251624
 *   Hubert Pacyna 251603
 *   Piotr Wlazło 251662
 ******************************************************************************/

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_timer.h"
#include "stdio.h"					// standardowa biblio wejścia wyjścia
#include "lpc17xx_dac.h"
#include "lpc17xx_rtc.h"
#include "lpc17xx_clkpwr.h"
#include "stdbool.h"
#include "joystick.h"

#include "diskio.h"					// obsługa systemu plików FAT i nośnika SD
#include "ff.h"
#include "oled.h"
#include "light.h"
#include "rotary.h"

#define UART_DEV LPC_UART3			// def UART3 jako domyślne urządzenie do komunikacji szeregowej
#define SAMPLE_RATE 8000
#define DELAY 1000000 / 8000

#define BUF_SIZE ((uint32_t)4096)
#define MAX_SONGS ((uint8_t)4)
#define MAX_NAME_LEN 13

static uint8_t songs = 0;
static char songsList[MAX_SONGS][MAX_NAME_LEN];

static uint8_t bufor1[BUF_SIZE];
static uint8_t bufor2[BUF_SIZE];

static volatile bool bufor1_pusty = true;
static volatile bool bufor2_pusty = true;
static volatile int aktualnyBufor = 0;
static volatile uint32_t pozycja = 0;

static RTC_TIME_Type currentTime;
static uint8_t timeStr[40];

void TIMER1_IRQHandler(void);
void SysTick_Handler(void);
void select(uint8_t nameIndex);
void unselect(uint8_t nameIndex);
void updateTime(RTC_TIME_Type *currentTime, uint8_t *timeStr);

void TIMER1_IRQHandler(void) {
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
            } else {
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
  Config.PrescaleValue = 1; 				// czyli 1 mikrosek

  //najpierw włączyć zasilanie
  CLKPWR_SetPCLKDiv (CLKPWR_PCLKSEL_TIMER1, CLKPWR_PCLKSEL_CCLK_DIV_4);
  // Ustawić timer.
  TIM_Cmd (LPC_TIM1, DISABLE); 				// to w zasadzie jest niepotrzebne, ponieważ po włączeniu zasilania timer jest nieczynny,
  TIM_Init (LPC_TIM1, TIM_TIMER_MODE, &Config);

  Match_Cfg.ExtMatchOutputType = TIM_EXTMATCH_NOTHING;
  Match_Cfg.IntOnMatch = TRUE;
  Match_Cfg.ResetOnMatch = TRUE;
  Match_Cfg.StopOnMatch = FALSE;
  Match_Cfg.MatchChannel = 0;
  Match_Cfg.MatchValue = 125; 				// Czyli 125  us.
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

static void init_uart(void)
{
	PINSEL_CFG_Type PinCfg;
	UART_CFG_Type uartCfg;

	// Initialize UART3 pin connect			// konfiguruje UART3 na P0.0 i P0.1
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

static void init_ssp(void)
{
	SSP_CFG_Type SSP_ConfigStruct;			// deklaracja struktury konfiguracyjej dla SPI
	PINSEL_CFG_Type PinCfg;					// i pinów

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
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);
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
	GPIO_SetDir(0, 1UL<<27UL, 1UL); 	// LM4811-clk
	GPIO_SetDir(0, 1UL<<28UL, 1UL); 	// LM4811-up/dn
	GPIO_SetDir(2, 1UL<<1UL, 1UL); 	// LM4811-shutdn

	GPIO_ClearValue(0, 1UL<<27UL); 	// LM4811-clk
	GPIO_ClearValue(0, 1UL<<28UL); 	// LM4811-up/dn
	GPIO_ClearValue(2, 1UL<<13UL); 	// LM4811-shutdn
}

void SysTick_Handler(void) {		/* obsługa przerwania SysTick */
    disk_timerproc();
}



static uint32_t getTicks(void) {
	uint32_t msTicks = 0;

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

	I2C_Init(LPC_I2C2, 100000);
	I2C_Cmd(LPC_I2C2, ENABLE);		// Enable I2C1 operation
}

static uint8_t parseWavHeader(FIL* file) {
	uint8_t header[44];
	UINT bytesRead;
	uint32_t sampleRate = 0;
	uint8_t result = 1;
	uint32_t dataOffset = 0;

	f_read(file, header, 44, &bytesRead);

	if (bytesRead != 44) {
	    UART_SendString(UART_DEV, (const uint8_t*)"Failed to open\r\n");
	    result = 0;
	}

	if ((header[0] != (uint8_t)'R') || (header[1] != (uint8_t)'I') || (header[2] != (uint8_t)'F') || (header[3] != (uint8_t)'F')) {
        UART_SendString(UART_DEV, (const uint8_t*)"Wrong format\r\n");
        result = 0;
	}

	if ((header[8] != (uint8_t)'W') || (header[9] != (uint8_t)'A') || (header[10] != (uint8_t)'V') || (header[11] != (uint8_t)'E')) {
		UART_SendString(UART_DEV, (const uint8_t*)"File is not .wav\r\n");
		result = 0;
	}

    if ((header[12] != (uint8_t)'f') || (header[13] != (uint8_t)'m') || (header[14] !=(uint8_t) 't') || (header[15] != (uint8_t)' ')) {
        UART_SendString(UART_DEV, (const uint8_t*)"Missing fmt\r\n");
        result = 0;
    }

    sampleRate = (header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24));

    if (sampleRate != (uint32_t)SAMPLE_RATE) {
        UART_SendString(UART_DEV, (const uint8_t*)"File not in 8kHz\r\n");
        result = 0;
    }

    uint32_t offset = 36U;

    while (offset < 100U) {
        if ((header[offset] == (uint8_t)'d') && (header[offset + 1UL] == (uint8_t)'a') &&
            (header[offset + 2UL] == (uint8_t)'t') && (header[offset + 3UL] == (uint8_t)'a')) {
            dataOffset = offset + 8U;
            break;
        }
        offset++;
    }

    if (offset >= 100UL) {
        UART_SendString(UART_DEV, (const uint8_t*)"Missing data chunk\r\n");
        result = 0;
    }

    f_lseek(file, dataOffset);

    return result;
}

static void changeVolume(uint8_t rotaryDir) {
	bool valid = false;

	/* początkowo clock=0 i UP=1*/
	if (rotaryDir == ROTARY_RIGHT) {
		  GPIO_SetValue(0, 1UL<<28UL); 		// up
		  valid = true;
	}
	else if (rotaryDir == ROTARY_LEFT) {
		  GPIO_ClearValue(0, 1UL<<28UL); 	// down
		  valid = true;
	} else {
//		gówno
	}

	if (valid) {
		GPIO_SetValue(0, 1UL<<27UL);
		Timer0_us_Wait(100);
		GPIO_ClearValue(0, 1UL<<27UL);
		GPIO_SetValue(0, 1UL<<28UL); // up
	}
}


void select(uint8_t nameIndex) {
	oled_putString(0, (nameIndex * 8u) + 1u, (uint8_t*) songsList[nameIndex], OLED_COLOR_WHITE, OLED_COLOR_BLACK);
}

void unselect(uint8_t nameIndex) {
	oled_putString(0, (nameIndex * 8u) + 1u, (uint8_t*) songsList[nameIndex], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

static void changeLight(void) {
	uint32_t lux = 0;
	lux = light_read(); /* pomiar swiatla */

	if (lux < 100U) {
		oled_setInvertDisplay();
	} else {
		oled_setNormalDisplay();
	}
}

static void playWavFile(char* filename) {
	BYTE res;
	UINT bytesRead;
	FIL wavFile;
	bool continuePlayback = true;

    bufor1_pusty = true;
    bufor2_pusty = true;
    aktualnyBufor = 0;
    pozycja = 0;

    (void)memset(bufor1, 0, BUF_SIZE);
    (void)memset(bufor2, 0, BUF_SIZE);

    res = f_open(&wavFile, filename, FA_READ);
    if (res != FR_OK) {
    	UART_SendString(UART_DEV,(const uint8_t*)"Failed to open file: %s, error: %d\r\n");

    	continuePlayback = false;
    }

    if (continuePlayback && (!parseWavHeader(&wavFile))) {
        UART_SendString(UART_DEV, (const uint8_t*)"Invalid WAV file format\r\n");
        f_close(&wavFile);

        continuePlayback = false;
    }

    if (continuePlayback) {
		res = f_read(&wavFile, bufor1, sizeof(bufor1), &bytesRead);
		bufor1_pusty = false;
		__enable_irq();

		while (continuePlayback) {
			changeLight();
			updateTime(&currentTime, timeStr);

			if (bufor1_pusty) {
				res = f_read(&wavFile, bufor1, sizeof(bufor1), &bytesRead);
				bufor1_pusty = false;
			}

			if (bufor2_pusty) {
				res = f_read(&wavFile, bufor2, sizeof(bufor2), &bytesRead);
				bufor2_pusty = false;
			}

			if ((res != FR_OK) || (bytesRead == 0U) || (bytesRead == wavFile.fsize)) {
				break;
			}

			changeVolume(rotary_read());
		}

		__disable_irq();
		f_close(&wavFile);
    }
}

static void init_amp(void){
	  GPIO_SetDir(0, 1UL<<27UL, 1UL); 	// clock
	  GPIO_SetDir(0, 1UL<<28UL, 1UL); 	// up/down
	  GPIO_SetDir(2, 1UL<<13UL, 1UL); 	// shutdown

	  //0 na shutdown, czyli włączenie
	  GPIO_ClearValue(2, 1UL<<13UL);
}

static int chooseSong(uint8_t songIndex) {
    uint8_t joy = 0;
	uint8_t currentSongIndex = songIndex;
	bool temp = false;

	joy = joystick_read();

	if ((joy & JOYSTICK_CENTER) != 0) {
	playWavFile(songsList[songIndex]);
	}

	else if (((joy & JOYSTICK_DOWN) != 0)) {
		unselect(currentSongIndex);

		if(currentSongIndex == (songs - 1UL)) {
			currentSongIndex = 0;
		} else {
			currentSongIndex++;
		}
		select(currentSongIndex);
	}
	else if ((joy & JOYSTICK_UP) != 0) {
		unselect(currentSongIndex);

		if(currentSongIndex == 0UL) {
			currentSongIndex = songs - 1u;
		} else {
			currentSongIndex--;
		}

		select(currentSongIndex);
	} else {
		temp = true;
	}

	if (!temp) {
		Timer0_Wait(200);
	}

	return currentSongIndex;
}

void updateTime(RTC_TIME_Type *currentTime, uint8_t *timeStr) {
    RTC_GetFullTime(LPC_RTC, currentTime);

    timeStr[0] = (uint8_t)((currentTime->HOUR / 10) + (int)'0');
    timeStr[1] = (uint8_t)((currentTime->HOUR % 10) + (int)'0');
    timeStr[2] = ':';
    timeStr[3] = (uint8_t)((currentTime->MIN / 10) + (int)'0');
    timeStr[4] = (uint8_t)((currentTime->MIN % 10) + (int)'0');
    timeStr[5] = ':';
    timeStr[6] = (uint8_t)((currentTime->SEC / 10) + (int)'0');
    timeStr[7] = (uint8_t)((currentTime->SEC % 10) + (int)'0');
    timeStr[8] = '\0';

    (void)oled_putString(0, 49, timeStr, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

int main (void) {
    DSTATUS stat;		// zmienne do obsługi systemu plików FAT
    BYTE res;
    DIR dir;
    FILINFO Finfo;
    FATFS Fatfs[1];

    uint8_t i = 0;
    int32_t t = 0;
    uint32_t trim = 0;
	uint8_t songIndex = 0;

	uint8_t returnCode = 0u;

    init_uart();
    init_i2c();
    init_ssp();
    init_dac();
    init_rtc();
    init_Timer();
    oled_init();
    light_init();
    init_amp();
    rotary_init();
   	joystick_init();

    SysTick_Config(SystemCoreClock / 100);		// konfiguruje SysTick i czeka 500s
    Timer0_Wait(500);

    stat = disk_initialize(0);					// inicjalizacja karty SD

    if (stat & STA_NOINIT) {
    	UART_SendString(UART_DEV,(const uint8_t*)"SD: not initialized\r\n");
    }

    if (stat & STA_NODISK) {
    	UART_SendString(UART_DEV,(const uint8_t*)"SD: No Disk\r\n");
    }

    if (stat != 0) {
    	returnCode = 1u;
    }

    res = f_mount(0, &Fatfs[0]);				// montuje system plików FAT
    if (res != FR_OK) {
    	UART_SendString(UART_DEV,(const uint8_t*)"Failed to mount 0: %d \r\n");
    	returnCode = 1u;
    }

    res = f_opendir(&dir, "/");				    // otwiera katalog główny karty SD
    if (res) {
    	UART_SendString(UART_DEV,(const uint8_t*)"Failed to open /: %d \r\n");
    	returnCode = 1u;
    }

    oled_clearScreen(OLED_COLOR_WHITE);

    // odczytuje i wypisuje nazwy plików z katalogu głównego
    while((songs <= MAX_SONGS) && (i < 20u)) {
		res = f_readdir(&dir, &Finfo);
		if ((Finfo.fname[0] == '_') || (Finfo.fattrib & AM_DIR)) {
			i++;
			continue;
		}
		if ((res != FR_OK) || !Finfo.fname[0]) {
			break;
		}
		oled_putString(1, 1u + (songs * 8u), Finfo.fname, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		uint8_t nameLen = strnlen(Finfo.fname, sizeof(Finfo.fname));
		(void)strncpy(songsList[songs], Finfo.fname, nameLen+1UL);
		songs++;
		i++;
    }

    light_enable();
    light_setRange(LIGHT_RANGE_4000);		// ustawienie zakresu swiatla do 4000 luksow

	RTC_GetFullTime(LPC_RTC, &currentTime);

	static const uint8_t szlaczek[] = "----------------";
	oled_putString(0, 41, szlaczek, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
	oled_scroll(0x05, 0x05);


	select(songIndex);
    while(1) {
    	changeLight();
    	updateTime(&currentTime, timeStr);
    	songIndex = chooseSong(songIndex);
    };

    return returnCode;
}
