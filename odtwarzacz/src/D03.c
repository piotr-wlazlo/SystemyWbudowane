/*****************************************************************************
 *   ODTWARZACZ .WAV
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
#include "lpc17xx_i2c.h"


#include "diskio.h"					// obsługa systemu plików FAT i nośnika SD
#include "ff.h"
#include "oled.h"
#include "light.h"
#include "rotary.h"
#include "string.h"

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
static uint8_t timeStr[9];

void TIMER1_IRQHandler(void);
void SysTick_Handler(void);
void select(uint8_t nameIndex);
void unselect(uint8_t nameIndex);
void updateTime(RTC_TIME_Type *currentTime, uint8_t *timeStr);

/*
 *  @brief:    			Obsługa przerwania timera – przesyła próbkę do DAC i zarządza buforami.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Aktualizuje wartość DAC, przełącza bufory, modyfikuje flagi buforów.
 */
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

/*
 * @brief: 			Inicjalizacja Timera T1
 * @return: 		brak
 * @side effects: 	brak
 * @description: 	Inicjalizacja  zegara T1 w taki sposób, żeby generował przerwania co 125 mikrosekund.
 */
static void init_Timer (void)
{
  TIM_TIMERCFG_Type Config;
  TIM_MATCHCFG_Type Match_Cfg;

  Config.PrescaleOption = TIM_PRESCALE_USVAL;
  Config.PrescaleValue = 1; 				// czyli 1 mikrosekund

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
  Match_Cfg.MatchValue = 125; 				// Czyli 125 mikrosekund
  TIM_ConfigMatch (LPC_TIM1, &Match_Cfg);
  TIM_Cmd (LPC_TIM1, ENABLE);
  // I odblokować przerwania w VIC
  NVIC_EnableIRQ (TIMER1_IRQn);
}

/*
 *  @brief:    			Inicjalizuje zegar RTC i ustawia czas początkowy.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Włącza i ustawia RTC.
 */
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

/*
 *  @brief:    			Inicjalizuje interfejs UART3 do komunikacji szeregowej.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Konfiguruje piny i ustawia parametry transmisji.
 */
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

/*
 *  @brief:    			Inicjalizuje interfejs SPI (SSP1) do komunikacji z kartą SD i OLEDem.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Ustawia piny SPI i włącza SSP1.
 */
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

/*
 *  @brief:    			Inicjalizuje przetwornik cyfrowo-analogowy DAC.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Konfiguruje pin i uruchamia DAC.
 */
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
}

/*
 *  @brief:    			Obsługa przerwania systemowego SysTick.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Wywołuje funkcję obsługi systemu plików.
 */
void SysTick_Handler(void) {
    disk_timerproc();
}

/*
 *  @brief:    			Inicjalizuje magistralę I2C2 do komunikacji z czujnikiem światła.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Konfiguruje piny i uruchamia I2C2.
 */
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

/*
 *  @brief:    		Sprawdza poprawność nagłówka pliku WAV i ustawia wskaźnik pliku na dane audio.
 *  @param:    		file  wskaźnik do uchwytu pliku WAV
 *  @returns:  		1 jeśli plik jest poprawny, 0 w przeciwnym razie
 *  @side effects:	Wysyła komunikaty UART w przypadku błędów, ustawia wskaźnik pliku na początek danych.
 */
static uint8_t parseWavHeader(FIL* file) {
	uint8_t header[44];
	UINT bytesRead;
	uint32_t sampleRate = 0;
	uint8_t result = 1;
	uint32_t dataOffset = 0;

	f_read(file, header, 44, &bytesRead);

	if (bytesRead != 44) {
		const char *msg = "Failed to open\r\n";
	    UART_SendString(UART_DEV, msg);
	    result = 0;
	}

	if ((header[0] != (uint8_t)'R') || (header[1] != (uint8_t)'I') || (header[2] != (uint8_t)'F') || (header[3] != (uint8_t)'F')) {
		const char *msg = "Wrong format\r\n";
        UART_SendString(UART_DEV, msg);
        result = 0;
	}

	if ((header[8] != (uint8_t)'W') || (header[9] != (uint8_t)'A') || (header[10] != (uint8_t)'V') || (header[11] != (uint8_t)'E')) {
		const char *msg = "File is not .wav\r\n";
		UART_SendString(UART_DEV, msg);
		result = 0;
	}

    if ((header[12] != (uint8_t)'f') || (header[13] != (uint8_t)'m') || (header[14] !=(uint8_t) 't') || (header[15] != (uint8_t)' ')) {
    	const char *msg = "Missing fmt\r\n";
        UART_SendString(UART_DEV, msg);
        result = 0;
    }

    sampleRate = (header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24));

    if (sampleRate != (uint32_t)SAMPLE_RATE) {
    	const char *msg = "File not in 8kHz\r\n";
        UART_SendString(UART_DEV, msg);
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
    	const char *msg = "Missing data chunk\r\n";
        UART_SendString(UART_DEV, msg);
        result = 0;
    }

    f_lseek(file, dataOffset);

    return result;
}

/*
 *  @brief:    			Zmienia głośność w zależności od kierunku obrotu enkodera.
 *  @param:    			rotaryDir  kierunek obrotu (ROTARY_LEFT / ROTARY_RIGHT)
 *  @returns:  			brak
 *  @side effects:		Steruje pinami GPIO odpowiedzialnymi za zmianę głośności.
 */
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

	}

	if (valid) {
		GPIO_SetValue(0, 1UL<<27UL);
		Timer0_us_Wait(100);
		GPIO_ClearValue(0, 1UL<<27UL);
		GPIO_SetValue(0, 1UL<<28UL); // up
	}
}

/*
 *  @brief:    			Podświetla wybraną pozycję (piosenkę) na wyświetlaczu OLED.
 *  @param:    			nameIndex  indeks piosenki
 *  @returns:  			brak
 *  @side effects:		Zmienia kolory wybranej pozycji na OLED.
 */
void select(uint8_t nameIndex) {
	oled_putString(0, (nameIndex * 8u) + 1u, (uint8_t*) songsList[nameIndex], OLED_COLOR_WHITE, OLED_COLOR_BLACK);
}

/*
 *  @brief:    			Usuwa podświetlenie z wybranej pozycji na wyświetlaczu OLED.
 *  @param:    			nameIndex  indeks piosenki
 *  @returns:  			brak
 *  @side effects:		Przywraca standardowy kolor tła wybranej pozycji.
 */
void unselect(uint8_t nameIndex) {
	oled_putString(0, (nameIndex * 8u) + 1u, (uint8_t*) songsList[nameIndex], OLED_COLOR_BLACK, OLED_COLOR_WHITE);
}

/*
 *  @brief:    			Dostosowuje tryb wyświetlacza OLED w zależności od natężenia światła.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Przełącza OLED między trybem normalnym i odwróconym.
 */
static void changeLight(void) {
	uint32_t lux = 0;
	lux = light_read(); /* pomiar swiatla */

	if (lux < 200U) {
		oled_setInvertedDisplay();
	} else {
		oled_setNormalDisplay();
	}
}

/*
 *  @brief:    			Odtwarza plik WAV o zadanej nazwie.
 *  @param:    			filename  wskaźnik do nazwy pliku WAV
 *  @returns:  			brak
 *  @side effects:		Otwiera, odczytuje i przetwarza plik WAV. Steruje DAC, obsługuje buforowanie i odświeżanie ekranu.
 */
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
    	const char *msg = "Failed to open file: %s, error: %d\r\n";
    	UART_SendString(UART_DEV, msg);

    	continuePlayback = false;
    }

    if (continuePlayback && (!parseWavHeader(&wavFile))) {
    	const char *msg = "Invalid WAV file format\r\n";
        UART_SendString(UART_DEV, msg);
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

/*
 *  @brief:    			Inicjalizuje piny i włącza wzmacniacz audio.
 *  @param:    			brak
 *  @returns:  			brak
 *  @side effects:		Ustawia piny GPIO odpowiedzialne za sterowanie wzmacniaczem.
 */
static void init_amp(void){
	  GPIO_SetDir(0, 1UL<<27UL, 1UL); 	// clock
	  GPIO_SetDir(0, 1UL<<28UL, 1UL); 	// up/down
	  GPIO_SetDir(2, 1UL<<13UL, 1UL); 	// shutdown

	  //0 na shutdown, czyli włączenie
	  GPIO_ClearValue(2, 1UL<<13UL);
}

/*
 *  @brief:    			Pozwala użytkownikowi wybrać i odtworzyć piosenkę za pomocą joysticka.
 *  @param:    			songIndex  bieżący indeks wybranej piosenki
 *  @returns:  			nowy indeks piosenki po ewentualnej zmianie
 *  @side effects:		Obsługuje joystick, aktualizuje wyświetlacz OLED, może uruchomić odtwarzanie pliku.
 */
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

/**
 * @brief Aktualizuje tekstowy zapis bieżącego czasu i wyświetla go na ekranie OLED.
 *
 * Funkcja pobiera bieżący czas z modułu RTC, konwertuje go do postaci tekstowej
 * w formacie HH:MM:SS i wyświetla go na ekranie OLED na współrzędnych (0, 49).
 *
 * @param[in,out] currentTime Wskaźnik na strukturę RTC_TIME_Type, do której zostanie
 *                            zapisany aktualny czas z RTC.
 * @param[out]    timeStr     Wskaźnik na bufor typu uint8_t o długości co najmniej 9 bajtów,
 *                            w którym zostanie zapisany sformatowany czas jako łańcuch znaków zakończony '\0'.
 */
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

/*!
 *  @brief    Główna procedura programu odtwarzacza plików WAV.
 *
 *  Inicjalizuje wszystkie niezbędne moduły (UART, DAC, Timer, RTC, I2C, SSP, wzmacniacz),
 *  wczytuje listę utworów, wyświetla ją na OLED, oraz zarządza wyborem i odtwarzaniem
 *  utworów za pomocą joysticka i enkodera.
 *
 *  @param    brak
 *
 *  @returns  int
 *            Zwraca 0 po zakończeniu działania programu (zwykle program działa w pętli nieskończonej).
 *
 *  @side effects:
 *            - Konfiguruje sprzętowe przerwania i steruje urządzeniami peryferyjnymi.
 *            - Steruje wyświetlaczem OLED.
 *            - Obsługuje odczyt z karty SD i odtwarzanie dźwięku.
 */
int main (void) {
    DSTATUS stat;		// zmienne do obsługi systemu plików FAT
    BYTE res;
    DIR dir;
    FILINFO Finfo;
    FATFS Fatfs[1];

    uint8_t i = 0;
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
    	const char *msg = "SD: not initialized\r\n";
    	UART_SendString(UART_DEV, msg);
    }

    if (stat & STA_NODISK) {
    	const char *msg = "SD: No Disk\r\n";
    	UART_SendString(UART_DEV, msg);
    }

    if (stat != 0) {
    	returnCode = 1u;
    }

    res = f_mount(0, &Fatfs[0]);				// montuje system plików FAT
    if (res != FR_OK) {
    	const char *msg = "Failed to mount 0: %d \r\n";
    	UART_SendString(UART_DEV, msg);
    	returnCode = 1u;
    }

    res = f_opendir(&dir, "/");				    // otwiera katalog główny karty SD
    if (res) {
    	const char *msg = "Failed to open /: %d \r\n";
    	UART_SendString(UART_DEV, msg);
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
		oled_putString(1, 1u + (songs * 8u), (uint8_t*)Finfo.fname, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
		uint8_t nameLen = strnlen(Finfo.fname, sizeof(Finfo.fname));
		(void)strncpy(songsList[songs], Finfo.fname, nameLen+1UL);
		songs++;
		i++;
    }

    light_enable();
    light_setRange(LIGHT_RANGE_4000);		// ustawienie zakresu swiatla do 4000 luksow

	RTC_GetFullTime(LPC_RTC, &currentTime);

	static uint8_t szlaczek[] = { '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '=', '\0' };
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
