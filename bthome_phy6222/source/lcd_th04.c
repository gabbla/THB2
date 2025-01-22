/*
 * lcd_th04.c
 *
 *  Created on: 22 янв. 2025 г.
 *      Author: pvvx, gabbla
 */
#include <string.h>
#include "types.h"
#include "config.h"
#if (DEV_SERVICES & SERVICE_SCREEN) && (DEVICE == DEVICE_TH04)
#include "OSAL.h"
#include "gpio.h"
#include "rom_sym_def.h"
#include "dev_i2c.h"
#include "sensors.h"
#include "lcd.h"
#include "thb2_peripheral.h"

#define LCD_I2C_ADDR	0x3E
#define I2C_WAIT_ms		1

dev_i2c_t i2c_dev1 = {
		.pi2cdev = AP_I2C1,
		.scl = I2C_SCL,
		.sda = I2C_SDA,
		.speed = I2C_100KHZ,
		.i2c_num = 0
};

/* 0,1,2,3,4,5,6,7,8,9,A,b,C,d,E,F*/
const uint8_t display_numbers[] = {
		// 76543210
        0b11111010, // 0
		0b00001010, // 1
		0b10111100, // 2
		0b10011110, // 3
		0b01001110, // 4
		0b11010110, // 5
		0b11110110, // 6
		0b10001010, // 7
		0b11111110, // 8
		0b11011110, // 9
		0b11101110, // A
		0b01110110, // b
		0b11110000, // C
		0b00111110, // d
		0b11110100, // E
		0b11100100, // F
};

#define LCD_SYM_b  0b001111100 // "b"
#define LCD_SYM_H  0b001101110 // "H"
#define LCD_SYM_h  0b001110100 // "h"
#define LCD_SYM_i  0b000100000 // "i"
#define LCD_SYM_L  0b001110000 // "L"
#define LCD_SYM_o  0b000110110 // "o"
#define LCD_SYM_t  0b001110100 // "t"
#define LCD_SYM_0  0b001011111 // "0"
#define LCD_SYM_A  0b011101110 // "A"
#define LCD_SYM_a  0b001101110 // "a"
#define LCD_SYM_P  0b011101100 // "P"

uint8_t lcd_i2c_addr; // = 0x3E

uint8_t display_buff[LCD_BUF_SIZE] = {
		LCD_SYM_o, LCD_SYM_o, LCD_SYM_o,
};
uint8_t display_out_buff[LCD_BUF_SIZE+1] = { 0x0b };


const uint8_t lcd_init_cmd[]	=	{
		// LCD controller initialize:
		0xea, // Set IC Operation(ICSET): Software Reset, Internal oscillator circuit
		0xd8, // Mode Set (MODE SET): Display enable, 1/3 Bias, power saving
		0xbc, // Display control (DISCTL): Power save mode 3, FRAME flip, Power save mode 1
		0x80, // load data pointer
		0xf0, // blink control off,  0xf2 - blink
		0xfc, // All pixel control (APCTL): Normal
		0x60,
		0x00,0x00,000,0x00,0x00,0x00,0x00,0x00,0x00
};


/* 0x0 = "  "
 * 0x1 = "°Г"
 * 0x2 = " _"
 * 0x3 = "°C"
 * 0x4 = " -"
 * 0x5 = "°F"
 * 0x6 = " ="
 * 0x7 = "°E" */
void show_temp_symbol(LCD_TEMP_SYMBOLS symbol) {
    // All off
	display_buff[2] &= ~BIT(0);
	display_buff[3] &= ~(BIT(4) | BIT(5));
	switch (symbol) {
		case LCD_TSYMBOL_C:
			display_buff[2] |= BIT(0);
			display_buff[3] |= BIT(5);
			break;
		case LCD_TSYMBOL_F:
			display_buff[3] |= (BIT(4) | BIT(5));
			break;
		case LCD_TSYMBOL_E:
			display_buff[2] |= BIT(0);
			display_buff[3] |= (BIT(4) | BIT(5));
		default:
			break;
	}
}

/* 0 = "     " off,
 * 1 = " ^_^ " happy
 * 2 = " -^- " sad
 * 3 = " ooo "
 * 4 = "(   )"
 * 5 = "(^_^)" happy
 * 6 = "(-^-)" sad
 * 7 = "(ooo)" */
void show_smiley(LCD_SMILEY_SYMBOLS symbol) {
    if (symbol & 1) display_buff[3] |= BIT(3);
	else display_buff[3] &= ~BIT(3);
	if (symbol & 2) display_buff[3] |= BIT(2);
	else display_buff[3] &= ~BIT(2);
	if(symbol & 4) display_buff[3] |= BIT(7);
	else display_buff[3] &= ~BIT(7);
}

void show_ble_symbol(bool state) {
	if (state)
		display_buff[5] |= BIT(0);
	else
		display_buff[5] &= ~BIT(0);
}

void show_battery_symbol(bool state) {
    // TODO
}

void show_big_number_x10(int16_t number) {
    // Clear all digit and minus but temp unit
	display_buff[0] = 0;
	display_buff[1] = 0;
	display_buff[2] &= BIT(0);
	display_buff[3] &= ~BIT(0);
	if (number > 1999) {
   		display_buff[0] = LCD_SYM_H; // "H"
   		display_buff[1] = LCD_SYM_i; // "i"
	} else if (number < -1999) {
   		display_buff[0] = LCD_SYM_L; // "L"
   		display_buff[1] = LCD_SYM_o; // "o"
	} else {
		/* number: -995..19995 */
		if (number > 1995 || number < -95) {
			if (number < 0){
				number = -number;
				display_buff[3] |= BIT(0); // "-"
			} else 
				display_buff[3] &= BIT(0); // "-"
			number = number / 10;
		} else { // show: -9.9..199.9
			display_buff[1] |= BIT(0); // point,
			if (number < 0){
				number = -number;
				display_buff[3] |= BIT(0); // "-"
			} else
				display_buff[3] &= BIT(0); // "-"
		}
		/* number: -1999..1999 */
		if (number > 999) display_buff[0] |= BIT(0); // "1" 1000..1999
		if (number > 99) display_buff[0] |= display_numbers[number / 100 % 10];
		if (number > 9) display_buff[1] |= display_numbers[number / 10 % 10];
		else display_buff[1] = 0;
	    display_buff[2] = display_numbers[number %10];
    }
}

static void show_small_digit(uint8_t value, uint8_t mask4, uint8_t mask5) {
	display_buff[4] |= (value & mask4);
	display_buff[5] |= (value & mask5);
}

/* -9 .. 99 */
void show_small_number(int16_t number, bool percent) {
    number -= 10;
    static const uint8_t left4mask = 0b11110000;
	static const uint8_t left5mask = 0b00001110;
	static const uint8_t right4mask = 0b00001110;
	static const uint8_t right5mask = 0b11110000;
    // Clear the digits
	display_buff[4] &= BIT(0);
	display_buff[5] &= BIT(0);

	display_buff[4] = percent? BIT(0) : 0;
	if (number > 99) {
		show_small_digit(LCD_SYM_H, left4mask, left5mask);
		show_small_digit(LCD_SYM_i, right4mask, right5mask);
	} else if (number < -9) {
		show_small_digit(LCD_SYM_L, left4mask, left5mask);
		show_small_digit(LCD_SYM_o, right4mask, right5mask);
	} else {
		if (number < 0) {
			number = -number;
			display_buff[5] = BIT(2); // "-"
		}
		if (number > 9) show_small_digit(display_numbers[number / 10 % 10], left4mask, left5mask);
		show_small_digit(display_numbers[number % 10], right4mask, right5mask);
    }
}

void lcd_show_version(void) {
	//display_buff[1] &= BIT(7); // connect
#if OTA_TYPE
	display_buff[0] = LCD_SYM_b;
	display_buff[1] = LCD_SYM_o;
	display_buff[2] = LCD_SYM_t;
#else
	display_buff[0] = LCD_SYM_A;
	display_buff[1] = LCD_SYM_P;
	display_buff[2] = LCD_SYM_P;
#endif
    show_small_number(APP_VERSION, false);
	update_lcd();
}

// TODO
void chow_clock(void) {	uint32_t tmp = clkt.utc_time_sec / 60;
	uint32_t min = tmp % 60;
	uint32_t hrs = (tmp / 60) % 24;
	display_buff[0] = 0;
	display_buff[1] &= BIT(7); // connect
	display_buff[1] |= display_numbers[hrs / 10];
	display_buff[2] = display_numbers[hrs % 10];
	display_buff[3] &= BIT(0) | BIT(4) | BIT(5) | BIT(6) | BIT(7); // bat
	display_buff[4] = display_numbers[min / 10];
	display_buff[5] = display_numbers[min % 10];
	display_buff[6] = 0;
	update_lcd();
}

static void chow_measure(void) {
	#if 1
#if (DEV_SERVICES & SERVICE_THS)
	if(cfg.flg & FLG_SHOW_TF) {
		show_big_number_x10(((int32_t)((int32_t)measured_data.temp * 9)/ 50) + 320); // convert C to F
		show_temp_symbol(LCD_TSYMBOL_F); // "°F"
	} else {
		show_big_number_x10((measured_data.temp + 5)/10);
		show_temp_symbol(LCD_TSYMBOL_C);
	}
	int16_t h = (measured_data.humi + 50)/100;
	if(h > 99)
		h = 99;
	show_small_number(h, true);
	show_battery_symbol(1);
#if (OTA_TYPE == OTA_TYPE_APP)
	if(cfg.flg & FLG_SHOW_SMILEY) {
#if (DEV_SERVICES & SERVICE_TH_TRG)
		if(cfg.flg & FLG_SHOW_TRG) {
			if(measured_data.flg.comfort) {
				if(measured_data.flg.trg_on)
					show_smiley(LD_SSYMBOL_HAPPY);
				else
					show_smiley(LD_SSYMBOL__HAPPY);
			} else {
				if(measured_data.flg.trg_on)
					show_smiley(LD_SSYMBOL_SAD);
				else
					show_smiley(LD_SSYMBOL__SAD);
			}
		} else
#endif // SERVICE_TH_TRG
		{
			if(measured_data.flg.comfort)
				show_smiley(LD_SSYMBOL_HAPPY);
			else
				show_smiley(LD_SSYMBOL_SAD);
		}
#if (DEV_SERVICES & SERVICE_TH_TRG)
	} else if(cfg.flg & FLG_SHOW_TRG) {
		if(measured_data.flg.trg_on)
			show_smiley(LD_SSYMBOL_ALL);
		else
			show_smiley(LD_SSYMBOL_OFF);
	} else
	#else
}
#endif // SERVICE_TH_TRG
#endif // OTA_TYPE
		show_smiley(LD_SSYMBOL_OFF);
#else
	show_big_number_x10(measured_data.battery_mv/100);
	show_small_number((measured_data.battery > 99)? 99 : measured_data.battery, true);
	show_battery_symbol(1);
	show_smiley(LD_SSYMBOL_OFF);
#endif // SERVICE_THS
	show_ble_symbol(gapRole_state == GAPROLE_CONNECTED);
	update_lcd();
	#endif    
}

/* flg != 0 -> chow_measure */
void chow_lcd(int flg) {
#if OTA_TYPE == OTA_TYPE_BOOT
	if(flg)
		chow_measure();
#else
	if(cfg.flg & FLG_DISPLAY_OFF) return;
	if(cfg.flg & FLG_SHOW_TIME) {
		if(wrk.lcd_count++ & 1)
			chow_clock();
		else
			chow_measure();
	} else if(flg) {
		chow_measure();
	}
#endif

}

void send_to_lcd(uint8_t *pbuf, int len) {
	if (lcd_i2c_addr) {
		init_i2c(&i2c_dev1);
		send_i2c_buf(&i2c_dev1, lcd_i2c_addr, pbuf, len);
		deinit_i2c(&i2c_dev1);
	}
}


void update_lcd(void) {
#if (OTA_TYPE == OTA_TYPE_APP)
	if(lcd_i2c_addr == 0 || (cfg.flg & FLG_DISPLAY_OFF) != 0)
		return;
#endif
	if(memcmp(&display_out_buff[1], display_buff, sizeof(display_buff))) {
		memcpy(&display_out_buff[1], display_buff, sizeof(display_buff));
		send_to_lcd(display_out_buff, sizeof(display_out_buff));
	}
}

extern uint32_t osal_sys_tick;
void delay(uint32_t ms) {
	uint32 to = osal_sys_tick;
	while(osal_sys_tick - to < ms) {
		asm("NOP");
	}
}

void init_lcd(void) {
	i2c_dev1.speed = I2C_100KHZ;
    display_out_buff[0] = 0x0b;
	init_i2c(&i2c_dev1);
	if(!send_i2c_buf(&i2c_dev1, LCD_I2C_ADDR, (uint8_t *) lcd_init_cmd, sizeof(lcd_init_cmd))) {
#if (OTA_TYPE == OTA_TYPE_APP)
		if(cfg.flg & FLG_DISPLAY_OFF)
			send_i2c_byte(&i2c_dev1, LCD_I2C_ADDR, 0xd0); // Mode Set (MODE SET): Display disable, 1/3 Bias, power saving
//		else
//			send_i2c_byte(&i2c_dev1, LCD_I2C_ADDR, 0xd8); // Mode Set (MODE SET): Display disable, 1/3 Bias, power saving
#endif
		lcd_i2c_addr = LCD_I2C_ADDR;
	} else
		lcd_i2c_addr = 0;
	deinit_i2c(&i2c_dev1);
//	i2c_dev1.speed = I2C_400KHZ;
}

/****************************************************/
#endif // (DEV_SERVICES & SERVICE_SCREEN)
