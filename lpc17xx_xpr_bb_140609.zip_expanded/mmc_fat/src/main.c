/*****************************************************************************
 *   This example shows how to access an MMC/SD card
 *
 *   Copyright(C) 2010, Embedded Artists AB
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"		/* konfiguracja pinów */
#include "lpc17xx_gpio.h"		/* obsługa GPIO */
#include "lpc17xx_ssp.h"		/* obsługa interfejsu SPI (dla karty SD)*/
#include "lpc17xx_uart.h"		/* obsługa UART (komunikacja szeregowa) */
#include "lpc17xx_timer.h"		/* obsługa timera */
#include "stdio.h"		/* standardowa biblio wejścia wyjścia */

#include "diskio.h"			/* obsługa systemu plików FAT i nośnika SD */
#include "ff.h"

#define UART_DEV LPC_UART3		/* def UART3 jako domyślne urządzenie do komunikacji szeregowej */

static FILINFO Finfo;		/* przechowanie inf o plikach */
static FATFS Fatfs[1];		/* reprezentacja systemu plików FAT */
static uint8_t buf[64];		/* bufor do danych wysyłanych przez UART */

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

int main (void) {

    DSTATUS stat;		/* zmienne do obsługi systemu plików FAT */
    DWORD p2;
    WORD w1;
    BYTE res, b1;
    DIR dir;

    int i = 0;


    init_ssp();		/* inicjalizacja SPI i UART */
    init_uart();


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

    /* odczytuje i wypisuje nazwy plików z katalogu głównego */
    for(;;) {
        res = f_readdir(&dir, &Finfo);
        if ((res != FR_OK) || !Finfo.fname[0]) break;

        UART_SendString(UART_DEV,(uint8_t*)&(Finfo.fname[0]));
        UART_SendString(UART_DEV,(uint8_t*)"\r\n");

    }


    while(1);

}

void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}
