#include <LiquidCrystal.h>
#include <EEPROM.h>
#include "EEPROMAnything.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <Si4703_Breakout.h>
#include <Wire.h>

/* Debounce & ButtonClick */
int reading[5];
int buttonState[5];
int lastButtonState[5] = {LOW,LOW,LOW,LOW,LOW};
int lastButtonAccioned[5] = {LOW,LOW,LOW,LOW,LOW};
long lastDebounceTime[5] = {0,0,0,0,0};
long debounceDelay = 100;

/* Clock */
byte s0 = 0;
byte s1 = 0;
byte m0 = 0;
byte m1 = 0;
byte h0 = 0;
byte h1 = 0;

/* Alarm */
byte as0 = 0;
byte as1 = 0;
byte am0 = 0;
byte am1 = 0;
byte ah0 = 0;
byte ah1 = 0;
boolean alarmaON = false;
boolean alarmaEnMarxa = false;
byte alarmaActivaRadio = 0;
byte durada = 0; 	// 0..5 [5,10,15,20,30,45]
int segonsActiva = 0;
const int duradaArray[6] = {300,600,900,1200,1800,2700};

/* Llum LCD */
byte brightness = 4; 	// 0..4 [0,25,50,75,100]
const int brightnessArray[5] = {0,64,127,191,255};
boolean inactiu = false;
byte inactivitat = 0;
byte duradaLlum = 3; //0..3, 3= INF
const byte duradaLlumArray[3] = {10,30,60};

/* Data */
byte nd = 0; // 0 = Dg... 6 = Ds
byte d0 = 1;
byte d1 = 0;
byte m = 1;
int a = 2012;

/* Radio */
byte estat = 0;
int channel = 0;
byte volume = 1;
byte carregaRDS = 0;
byte mov = 0;
byte emissores = 0;
int emissoresPreferides[8];
char rdsBuffer[64];


/* Menu */
byte menu = 0;
byte submenu = 0;
byte punterHora = 0;

/* Custom characters */
byte upArrow[8] = {0b00100,0b01110,0b11011,0b10001,
	0b00100,0b01110,0b11011,0b10001};

byte downArrow[8] = {0b10001,0b11011,0b01110,0b00100,
	0b10001,0b11011,0b01110,0b00100};

byte infinite0[8] = {0b00000,0b01110,0b10011,0b10001,
	0b10011,0b01110,0b00000,0b00000,};

byte infinite1[8] = {0b00000,0b01110,0b11001,0b10001,
	0b11001,0b01110,0b00000,0b00000};

byte load[8] = {0b00000,0b10000,0b01000,0b00100,
	0b00010,0b00001,0b00000,0b00000}; // "\"

byte play[8] = {0b00000,0b10100,0b10110,0b10111,
	0b10110,0b10100,0b00000,0b00000};

byte loadradio[8] = {0b00000,0b00000,0b11111,0b00000,
	0b11111,0b01110,0b00100,0b00000};

Si4703_Breakout radio(17, 18, 19);
LiquidCrystal lcd(13, 12, 5, 4, 3, 2);

void configureTimer() {
	// initialize Timer1
	cli();		// disable global interrupts
	TCCR1A = 0;		// set entire TCCR1A register to 0
	TCCR1B = 0;		// same for TCCR1B

	// set compare match register to desired timer count:
	// 2^16-1-49910
	OCR1A = 15624; // -1
	// turn on CTC mode:
	TCCR1B |= (1 << WGM12);
	// Set CS10 and CS12 bits for 1024 prescaler:
	TCCR1B |= (1 << CS10);
	TCCR1B |= (1 << CS12);
	// enable timer compare interrupt:
	TIMSK1 |= (1 << OCIE1A);
	// enable global interrupts:
	sei();
}

void clearBuffer() {
	for (byte i = 0; i < 64; ++i) {
		rdsBuffer[i] = ' ';
	}
	rdsBuffer[3] = 'N';
	rdsBuffer[4] = 'O';
	rdsBuffer[6] = 'D';
	rdsBuffer[7] = 'A';
	rdsBuffer[8] = 'T';
	rdsBuffer[9] = 'A';
	rdsBuffer[17] = 'N';
	rdsBuffer[18] = 'O';
	rdsBuffer[20] = 'D';
	rdsBuffer[21] = 'A';
	rdsBuffer[22] = 'T';
	rdsBuffer[23] = 'A';
}

void printHoraSerial() {
	Serial.print("Hora: ");
	Serial.print(h1), Serial.print(h0);
	Serial.print(":");
	Serial.print(m1), Serial.print(m0);
	Serial.print(":");
	Serial.print(s1), Serial.print(s0);
	Serial.print("\n");
}

void printRDS() {
	byte p = 0 + mov;
	lcd.setCursor(2,0);
	for (int i = 2 + mov; i< 16 + mov; ++i) {
		lcd.write(rdsBuffer[p++]);
	}
	p = 14 + mov;
	lcd.setCursor(2,1);
	for (int i = 18 + mov; i< 32 + mov; ++i) {
		lcd.write(rdsBuffer[p++]);
	}
}

void printRDSEspera() {
	lcd.setCursor(2,0);
	lcd.print("Llegint RDS ");
	if (carregaRDS == 0) lcd.print("/");
	else if (carregaRDS == 1) lcd.print("-");
	else if (carregaRDS == 2) lcd.write(uint8_t(4));
	else if (carregaRDS == 3) lcd.print("/");
	else if (carregaRDS == 4) lcd.print("-");
	else if (carregaRDS == 5) lcd.write(uint8_t(4));
}

void printEstatRadio() {
	lcd.setCursor(4,0);
	lcd.print("Radio ");
	if (estat == 0) lcd.print("OFF");
	else lcd.print("ON ");
}

void printFavRadio() {
	int aux = emissores+1;
	lcd.setCursor(2,1);
	lcd.print("E");
	lcd.print(aux);
	lcd.print(" ");
	lcd.write(uint8_t(5));
	lcd.print(" ");
	aux = emissoresPreferides[emissores];
	if (aux < 1000) lcd.print(" ");
	if (aux < 100) lcd.print(" "); // 0
	lcd.print(aux/10);
	lcd.print(".");
	lcd.print(aux%10);
	lcd.print(" ");
	lcd.write(uint8_t(6));
}

void printFreqRadio() {
	lcd.setCursor(2,0);
	lcd.print("Freq ");
	if (channel < 1000) lcd.print(" ");
	if (channel < 100) lcd.print(" "); // 0
	lcd.print(channel/10);
	lcd.print(".");
	lcd.print(channel%10);
	lcd.print(" FM");
}

void printVolumRadio() {
	lcd.setCursor(4,1);
	lcd.print("Volum ");
	if (volume < 10) lcd.print(" ");
	lcd.print(volume);
}

void printDuradaLlumLCD() {
	lcd.setCursor(2,1);
	lcd.print("T.Llum LCD ");
	if (duradaLlum == 0) lcd.print("10s");
	else if (duradaLlum == 1) lcd.print("30s");
	else if (duradaLlum == 2) lcd.print("60s");
	else if (duradaLlum == 3) {
		lcd.write(uint8_t(2)), lcd.write(uint8_t(3));
		lcd.print(" ");
	}
}

void printBrightnessLCD() {
	lcd.setCursor(2,0);
	lcd.print("Llum LCD ");
	if (brightness == 0) lcd.print("  0%");
	else if (brightness == 1) lcd.print(" 25%");
	else if (brightness == 2) lcd.print(" 50%");
	else if (brightness == 3) lcd.print(" 75%");
	else if (brightness == 4) lcd.print("100%");
}

void printAlarmaSelectorLCD() {
	lcd.setCursor(3,0);
	lcd.print("Alarma ");
	if (alarmaON) lcd.print("ON ");
	else lcd.print("OFF");
}

void printAlarmaLCD() {
	lcd.setCursor(4,1);
	lcd.print(ah1), lcd.print(ah0);
	lcd.print(":");
	lcd.print(am1), lcd.print(am0);
	lcd.print(":");
	lcd.print(as1), lcd.print(as0);
}

void printAlarmaDurationLCD() {
	lcd.setCursor(3,0);
	lcd.print("Minuts ");
	if (durada == 0) lcd.print(" 5");
	else if (durada == 1) lcd.print("10");
	else if (durada == 2) lcd.print("15");
	else if (durada == 3) lcd.print("20");
	else if (durada == 4) lcd.print("30");
	else if (durada == 5) lcd.print("45");
}

void printHoraLCD() {
	lcd.setCursor(4,0);
	lcd.print(h1), lcd.print(h0);
	lcd.print(":");
	lcd.print(m1), lcd.print(m0);
	lcd.print(":");
	lcd.print(s1), lcd.print(s0);
}

void printDataLCD() {
	lcd.setCursor(1,1);
	if (nd == 0) lcd.print("Dg");
	else if (nd == 1) lcd.print("Dl");
	else if (nd == 2) lcd.print("Dt");
	else if (nd == 3) lcd.print("Dc");
	else if (nd == 4) lcd.print("Dj");
	else if (nd == 5) lcd.print("Dv");
	else if (nd == 6) lcd.print("Ds");
	else lcd.print("ER");
	lcd.print(" ");
	lcd.print(d1), lcd.print(d0);
	lcd.print("/");
	if (m< 10) lcd.print("0");
	lcd.print(m);
	lcd.print("/");
	lcd.print(a);
}

boolean alarmaActiva() {
	boolean ret = (ah1 == h1 and ah0 == h0 and am1 == m1 and
	am0 == m0 and as1 == s1 and as0 == s0);
	return ret;
}

int diaSetmana(int dia, int mes, int any) {
	int y1, y2, c, w;
	if (mes < 3) y1 = any-1;
	else y1 = any;
	y2=y1%100;
	c=y1/100;
	w = dia+(int)(2.6*((mes+9)%12+1)-0.2)+y2+(int)y2/4+(int)c/4-2*c;
	while (w < 0) w +=7;
	w = (w)%7;
	if (mes == 2) w = (w+1)%7;
	return w;
}

boolean diaCorrecte(int D, int M, int A) {
	bool correcta = true;
	if ((D <= 0) or (M > 12) or (M <= 0)) correcta = false;
	else if ((M == 4 or M == 6 or M == 9 or M == 11) and (D > 30)) correcta = false;
	else if ((M == 1 or M == 3 or M == 5 or M == 7 or M == 8 or M == 10 or M == 12) and (D > 31)) correcta = false;
	else if ((A % 4 == 0) and ((A % (4 * 100) == 0) or (A % 100) != 0)) {if ((D > 29) and (M == 2)) correcta = false;}
	else if ((M == 2) and (D > 28)) correcta = false;
	return correcta;
}

void guardarRadioEEPROM() {
	/* Adreces; 16: channel, 20: emissoresPreferides, 52: volume */
	EEPROM_writeAnything(16, channel);
	EEPROM_writeAnything(20, emissoresPreferides);
	EEPROM_writeAnything(52, volume);
}

void llegirRadioEEPROM() {
	/* Adreces; 16: channel, 20: emissoresPreferides, 52: volume */
	EEPROM_readAnything(16, channel);
	EEPROM_readAnything(20, emissoresPreferides);
	EEPROM_readAnything(52, volume);
}

void guardarPrefEEPROM() {
	/* Adreces; 14: brightness, 15: duradaLlum */
	EEPROM_writeAnything(14, brightness);
	EEPROM_writeAnything(15, duradaLlum);
}

void llegirPrefEEPROM() {
	/* Adreces; 14: brightness, 15: duradaLlum */
	EEPROM_readAnything(14, brightness);
	EEPROM_readAnything(15, duradaLlum);
}

void guardarAlarmaEEPROM() {
	/* Adreces: 6: hora, 7:minuts, 8: segons, 9: alarmaON, 10: durada */
	byte aux;
	EEPROM_writeAnything(6, ah1*10+ah0);
	EEPROM_writeAnything(7, am1*10+am0);
	EEPROM_writeAnything(8, as1*10+as0);
	if (alarmaON) aux = 1;
	else aux = 0;
	EEPROM_writeAnything(9, aux);
	EEPROM_writeAnything(10, durada);
}

void llegirAlarmaEEPROM() {
	/* Adreces: 6: hora, 7:minuts, 8: segons, 9: alarmaON, 10: durada */
	byte aux;
	EEPROM_readAnything(6, aux);
	ah1 = aux/10;
	ah0 = aux%10;
	EEPROM_readAnything(7, aux);
	am1 = aux/10;
	am0 = aux%10;
	EEPROM_readAnything(8, aux);
	as1 = aux/10;
	as0 = aux%10;
	EEPROM_readAnything(9, aux);
	if (aux == 1) alarmaON = true;
	else alarmaON = false;
	EEPROM_readAnything(10, aux);
	durada = aux;
}

void guardarDataEEPROM() {
	/* Adreces: 0: dia, 1:mes, 2: any*/
	EEPROM_writeAnything(0, d1*10+d0);
	EEPROM_writeAnything(1, m);
	EEPROM_writeAnything(2, a);
}

void llegirDataEEPROM() {
	/* Adreces: 0: dia, 1:mes, 2: any*/
	byte aux;
	EEPROM_readAnything(0, aux);
	EEPROM_readAnything(1, m);
	EEPROM_readAnything(2, a);
	d1 = aux/10;
	d0 = aux%10;
}

void desactivaLCD() {
	lcd.noDisplay();
	analogWrite(11,0);
}

void activaLCD() {
	lcd.display();
	analogWrite(11,brightnessArray[brightness]);
}

ISR(TIMER1_COMPA_vect) {
	boolean canvidia = false;
	++s0;
	if (s0 == 10) {
		s0 = 0;
		++s1;
		if (s1 == 6) {
			s1 = 0;
			++m0;
			if (m0 == 10) {
				m0 = 0;
				++m1;
				if (m1 == 6) {
					m1 = 0;
					++h0;
					if (h0 == 10 or (h0 == 4 and h1 == 2)) {
						h0 = 0;
						++h1;
						if (h1 == 3) h1=0, canvidia = true;
					}
				}
			}
		}
	}
	if (canvidia) {
		++d0;
		if (!diaCorrecte(d1*10+d0,m,a)) {
			d0 = 0;
			++d1;
			if (!diaCorrecte(d1*10+d0,m,a)) {
				d1 = 0, d0 = 1;
				++m;
				if (!diaCorrecte(d1*10+d0,m,a)) {
					m = 1;
					++a;
				}
			}
		}
		guardarDataEEPROM();
		nd = diaSetmana(d1*10+d0,m,a);
	}
	if (alarmaON) {
		if (alarmaEnMarxa and segonsActiva < duradaArray[durada]) {
			if (segonsActiva == 0) alarmaActivaRadio = 1;
			++segonsActiva;
			// Fer cosa d'alarma;
		}
		else {  // aturar cosa alarma i segonsActiva = 0;
			if (segonsActiva != 0) alarmaActivaRadio = 2;
			alarmaEnMarxa = alarmaActiva(), segonsActiva = 0;
		}
	}
	if (inactiu and duradaLlum != 3) {
		if (inactivitat >= duradaLlumArray[duradaLlum]) desactivaLCD();
		else ++inactivitat;
	}

	if (menu == 0 and submenu == 10) {
		// Requereix actualització cada segon
		printHoraLCD();
		if (canvidia) printDataLCD();
	}
	else if (menu == 1 and submenu == 20) {
		// Requereix actualització cada segon
		printHoraLCD();
		printDataLCD();
		lcd.setCursor(punterHora%16,punterHora/16);
	}
	else if (menu == 3 and submenu == 42) {
		if (carregaRDS != 6 and carregaRDS != 7) {
			carregaRDS = (carregaRDS+1)%6;
			printRDSEspera();
			lcd.setCursor(punterHora%16,punterHora/16);
		}
		else {
			if (carregaRDS == 6) {
				++mov;
				printRDS();
				if (mov == 35) carregaRDS = 7;
			}
			else if (carregaRDS == 7) {
				--mov;
				printRDS();
				if (mov == 0) carregaRDS = 6;
			}
			lcd.setCursor(punterHora%16,punterHora/16);
		}
	}
}

void printMenuLCD() {
	lcd.clear();
	lcd.setCursor(3,0);
	if (menu == 0) lcd.print(" Rellotge");
	else if (menu == 1) {
		lcd.print(" Modificar");
		lcd.setCursor(3,1), lcd.print("   Hora");
	}
	else if (menu == 2) {
		lcd.print("Configurar");
		lcd.setCursor(3,1), lcd.print("  Alarma");
	}
	else if (menu == 3) lcd.print(" Radio FM");
	else if (menu == 4) lcd.print("Preferen.");
	lcd.setCursor(0,0), lcd.print("<<");
	lcd.setCursor(0,1), lcd.print("<<");
	lcd.setCursor(14,0), lcd.print(">>");
	lcd.setCursor(14,1), lcd.print(">>");
}

void setup() {
	//Serial.begin(9600);
	lcd.begin(16, 2);
	lcd.createChar(0, upArrow);
	lcd.createChar(1, downArrow);
	lcd.createChar(2, infinite0);
	lcd.createChar(3, infinite1);
	lcd.createChar(4, load);
	lcd.createChar(5, play);
	lcd.createChar(6, loadradio);
	lcd.setCursor(0,0);
	pinMode(11, OUTPUT);
	pinMode(10, INPUT);
	pinMode(9, INPUT);
	pinMode(8, INPUT);
	pinMode(7, INPUT);
	pinMode(6, INPUT);
	llegirDataEEPROM();
	llegirAlarmaEEPROM();
	llegirPrefEEPROM();
	llegirRadioEEPROM();
	nd = diaSetmana(d1*10+d0,m,a);
	analogWrite(11,brightnessArray[brightness]);
	configureTimer();
	printMenuLCD();
}

void carregaMarcador() {
	if (estat == 1) {
		if (channel != emissoresPreferides[emissores]) {
			channel = emissoresPreferides[emissores];
			radio.setChannel(channel);
			printFreqRadio();
		}
	}
}

void guardaRadioMarcador() {
	if (estat == 1) {
		emissoresPreferides[emissores] = channel;
		printFavRadio();
	}
}

void canviaEmissoraMarcadors(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (sentit == 1) emissores = (emissores+1)%8;
	if (sentit == -1) {
		if (emissores == 0) emissores = 7;
		else --emissores;
	}
	printFavRadio();
}

void canviaEstatRadio() {
	if (estat == 0) {
		radio.powerOn();
		estat = 1;
		radio.setVolume(volume);
		radio.setChannel(channel);
		//channel = radio.getChannel();
	}
	else {
		radio.powerOff();
		estat = 0;
		delay(1500);
	}
	if (submenu == 40) printEstatRadio();
}

void canviaVolumRadio(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (sentit == 1) volume = (volume+1)%16;
	if (sentit == -1) {
		if (volume == 0) volume = 15;
		else --volume;
	}
	if (estat == 1) radio.setVolume(volume);
	printVolumRadio();
}

void canviaCanalRadio(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (estat == 1) {
		if (sentit == 1) channel = radio.seekUp();
		else channel = radio.seekDown();
		printFreqRadio();
	}
}

void canviaDuradaLlum(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (sentit == 1) duradaLlum = (duradaLlum+1)%4;
	if (sentit == -1) {
		if (duradaLlum == 0) duradaLlum = 3;
		else --duradaLlum;
	}
	printDuradaLlumLCD();
}

void canviaBrillantor(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (sentit == 1) brightness = (brightness+1)%5;
	if (sentit == -1) {
		if (brightness == 0) brightness = 4;
		else --brightness;
	}
	analogWrite(11,brightnessArray[brightness]);
	printBrightnessLCD();
}

void canviaDuradaAlarma(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (sentit == 1) durada = (durada+1)%6;
	if (sentit == -1) {
		if (durada == 0) durada = 5;
		else --durada;
	}
	printAlarmaDurationLCD();
}

void canviaAlarma(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar, sentit = 0 toggleAlarma
	if (punterHora > 9 and punterHora < 13) {
		if (sentit == 0) alarmaON = !alarmaON;
	}
	else if (punterHora == 20) {
		if (ah1 == 1 and ah0 >= 4 and sentit == 1) ah1 = 0;
		else if (ah1 == 0 and ah0 >= 4 and sentit == -1) ah1 = 1;
		else if (ah1 == 0 and sentit == -1) ah1 = 2;
		else if (ah1 == 2 and sentit == 1) ah1 = 0;
		else ah1 += sentit;
	}
	else if (punterHora == 21) {
		if (ah0 == 0 and ah1 == 2 and sentit == -1) ah0 = 3;
		else if (ah0 >= 3 and ah1 == 2 and sentit == 1) ah0 = 0;
		else if (ah0 == 9 and sentit == 1) ah0 = 0;
		else if (ah0 == 0 and sentit == -1) ah0 = 9;
		else ah0 += sentit;
	}
	else if (punterHora == 23) {
		if (am1 == 0 and sentit == -1) am1 = 5;
		else if (am1 == 5 and sentit == 1) am1 = 0;
		else am1 += sentit;
	}
	else if (punterHora == 24) {
		if (am0 == 0 and sentit == -1) am0 = 9;
		else if (am0 == 9 and sentit == 1) am0 = 0;
		else am0 += sentit;
	}
	else if (punterHora == 26) {
		if (as1 == 0 and sentit == -1) as1 = 5;
		else if (as1 == 5 and sentit == 1) as1 = 0;
		else as1 += sentit;
	}
	else if (punterHora == 27) {
		if (as0 == 0 and sentit == -1) as0 = 9;
		else if (as0 == 9 and sentit == 1) as0 = 0;
		else as0 += sentit;
	}
	printAlarmaSelectorLCD();
	printAlarmaLCD();
}

void canviaHora(int sentit) {
	// sentit = 1 sumar, sentit = -1 restar
	if (punterHora == 4) {
		if (h1 == 1 and h0 >= 4 and sentit == 1) h1 = 0;
		else if (h1 == 0 and h0 >= 4 and sentit == -1) h1 = 1;
		else if (h1 == 0 and sentit == -1) h1 = 2;
		else if (h1 == 2 and sentit == 1) h1 = 0;
		else h1 += sentit;
	}
	else if (punterHora == 5) {
		if (h0 == 0 and h1 == 2 and sentit == -1) h0 = 3;
		else if (h0 >= 3 and h1 == 2 and sentit == 1) h0 = 0;
		else if (h0 == 9 and sentit == 1) h0 = 0;
		else if (h0 == 0 and sentit == -1) h0 = 9;
		else h0 += sentit;
	}
	else if (punterHora == 7) {
		if (m1 == 0 and sentit == -1) m1 = 5;
		else if (m1 == 5 and sentit == 1) m1 = 0;
		else m1 += sentit;
	}
	else if (punterHora == 8) {
		if (m0 == 0 and sentit == -1) m0 = 9;
		else if (m0 == 9 and sentit == 1) m0 = 0;
		else m0 += sentit;
	}
	else if (punterHora == 10) {
		if (s1 == 0 and sentit == -1) s1 = 5;
		else if (s1 == 5 and sentit == 1) s1 = 0;
		else s1 += sentit;
	}
	else if (punterHora == 11) {
		if (s0 == 0 and sentit == -1) s0 = 9;
		else if (s0 == 9 and sentit == 1) s0 = 0;
		else s0 += sentit;
	}
	else if (punterHora == 20) {
		int aux;
		if (d1 == 0 and sentit == -1) aux = 3;
		else if (d1 == 3 and sentit == 1) aux = 0;
		else aux = d1 + sentit;
		if (diaCorrecte(aux*10+d0,m,a)) d1 = aux;
	}
	else if (punterHora == 21) {
		int aux;
		if (d0 == 0 and sentit == -1) aux = 9;
		else if (d0 == 9 and sentit == 1) aux = 0;
		else aux = d0 + sentit;
		if (diaCorrecte(d1*10+aux,m,a)) d0 = aux;
	}
	else if (punterHora == 23 or punterHora == 24) {
		int aux;
		if (m == 0 and sentit == -1) aux = 12;
		else if (m == 12 and sentit == 1) aux = 0;
		else aux = m + sentit;
		if (diaCorrecte(d1*10+d0,aux,a)) m = aux;
	}
	else if (punterHora > 25 and punterHora < 30) {
		int aux;
		if (a == 0 and sentit == -1) aux = 9999;
		else if (a == 9999 and sentit == 1) aux = 0;
		else aux = a+sentit;
		if (diaCorrecte(d1*10+d0,m,aux)) a = aux;
	}
	nd = diaSetmana(d1*10+d0,m,a);
	printHoraLCD();
	printDataLCD();
}

void printSubmenu10() {
	submenu = 10;
	lcd.clear();
	printHoraLCD();
	printDataLCD();
}

void printSubmenu20() {
	submenu = 20;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	printHoraLCD();
	lcd.setCursor(15,0);
	lcd.print("*");
	printDataLCD();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void printSubmenu30() {
	submenu = 30;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	lcd.setCursor(0,1);
	lcd.write(uint8_t(1));
	printAlarmaSelectorLCD();
	printAlarmaLCD();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void printSubmenu31() {
	submenu = 31;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	printAlarmaDurationLCD();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void printSubmenu40() {
	submenu = 40;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	lcd.setCursor(0,1);
	lcd.write(uint8_t(1));
	printEstatRadio();
	printVolumRadio();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void printSubmenu41() {
	submenu = 41;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	lcd.setCursor(0,1);
	lcd.write(uint8_t(1));
	printFreqRadio();
	printFavRadio();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void printSubmenu42() {
	carregaRDS = 0;
	mov = 0;
	submenu = 42;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	printRDSEspera();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	clearBuffer();
	if (estat == 1) {
		radio.llegeixRDS(rdsBuffer,15000);
	}
	carregaRDS = 6;
	//Serial.print(rdsBuffer);
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	printRDS();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void printSubmenu50() {
	submenu = 50;
	lcd.clear();
	lcd.setCursor(0,0);
	lcd.write(uint8_t(0));
	printBrightnessLCD();
	printDuradaLlumLCD();
	punterHora = 0;
	lcd.setCursor(punterHora%16,punterHora/16);
	lcd.blink();
}

void accionaMenu() {
	boolean ordenat = false;
	for (int i=0;i<5 and not ordenat;++i) {
		if (reading[i] == HIGH and buttonState[i] == HIGH and lastButtonAccioned[i] == LOW) {
			lastButtonAccioned[i] = HIGH;
			ordenat = true;
			if (i == 0) {   		//	Esquerra					<--
				if (submenu == 0) {
					if (menu == 0) menu = 4;
					else --menu;
					printMenuLCD();
				}
				else {
					if (punterHora > 0) --punterHora;
					else punterHora = 31;
					lcd.setCursor(punterHora%16,punterHora/16);
				}
			}
			else if (i == 1) {		//	Dreta						<--
				if (submenu == 0) {
					if (menu == 4) menu = 0;
					else ++menu;
					printMenuLCD();
				}
				else {
					if (punterHora < 31) ++punterHora;
					else punterHora = 0;
					lcd.setCursor(punterHora%16,punterHora/16);
				}
			}
			else if (i == 2) {		//	Cancelar / Amunt			<--
				if (menu == 0) {
					if (submenu == 10) {
						submenu = 0;
						lcd.clear();
						printMenuLCD();
					}
				}
				else if (menu == 1) {
					if (submenu == 20) {
						if (punterHora == 0) {
							submenu = 0;
							lcd.clear();
							lcd.noBlink();
							guardarDataEEPROM();
							printMenuLCD();
						}
						if (punterHora > 3 and punterHora < 12) canviaHora(1);
						else if (punterHora > 19 and punterHora < 30) canviaHora(1);
						else {
							byte aux = punterHora-16;
							while (aux < 0) aux +=32;
							punterHora = (aux)%32;
						}
						lcd.setCursor(punterHora%16,punterHora/16);
					}
				}
				else if (menu == 2) {
					if (submenu == 30) {
						if (punterHora == 0) {
							submenu = 0;
							lcd.clear();
							lcd.noBlink();
							guardarAlarmaEEPROM();
							printMenuLCD();
						}
						if (punterHora > 9 and punterHora < 13) canviaAlarma(0);
						else if (punterHora > 19 and punterHora < 28) canviaAlarma(1);
						else {
							byte aux = punterHora-16;
							while (aux < 0) aux +=32;
							punterHora = (aux)%32;
						}
						lcd.setCursor(punterHora%16,punterHora/16);
					}
					if (submenu == 31) {
						if (punterHora == 0) {
							printSubmenu30();
							punterHora = 16;
							lcd.setCursor(punterHora%16,punterHora/16);
						}
						else {
							if (punterHora > 9 and punterHora < 13) canviaDuradaAlarma(1);
							else {
								byte aux = punterHora-16;
								while (aux < 0) aux +=32;
								punterHora = (aux)%32;
							}
							lcd.setCursor(punterHora%16,punterHora/16);
						}
					}
				}
				else if (menu == 3) {
					if (submenu == 40) {
						if (punterHora == 0) {
							submenu = 0;
							lcd.clear();
							guardarRadioEEPROM();
							lcd.noBlink();
							printMenuLCD();
						}
						else {
							if (punterHora > 9 and punterHora < 13) canviaEstatRadio();
							else if (punterHora > 25 and punterHora < 28) canviaVolumRadio(1);
							else {
								byte aux = punterHora-16;
								while (aux < 0) aux +=32;
								punterHora = (aux)%32;
							}
							lcd.setCursor(punterHora%16,punterHora/16);
						}
					}
					else if (submenu == 41) {
						if (punterHora == 0) {
							printSubmenu40();
							punterHora = 16;
							lcd.setCursor(punterHora%16,punterHora/16);
						}
						else {
							if (punterHora > 6 and punterHora < 12) canviaCanalRadio(1);
							else if (punterHora > 17 and punterHora < 20) canviaEmissoraMarcadors(1);
							else if (punterHora == 21) carregaMarcador();
							else if (punterHora == 29) guardaRadioMarcador();
							else {
								byte aux = punterHora-16;
								while (aux < 0) aux +=32;
								punterHora = (aux)%32;
							}
							lcd.setCursor(punterHora%16,punterHora/16);
						}
					}
					else if (submenu == 42) {
						if (punterHora == 0) {
							printSubmenu41();
							punterHora = 16;
						}
						else {
							byte aux = punterHora-16;
							while (aux < 0) aux +=32;
							punterHora = (aux)%32;
						}
						lcd.setCursor(punterHora%16,punterHora/16);
					}
				}
				else if (menu == 4) {
					if (submenu == 50) {
						if (punterHora == 0) {
							submenu = 0;
							lcd.clear();
							lcd.noBlink();
							guardarPrefEEPROM();
							printMenuLCD();
						}
						else {
							if (punterHora > 10 and punterHora < 15) canviaBrillantor(1);
							else if (punterHora > 28 and punterHora < 32) canviaDuradaLlum(1);
							else {
								byte aux = punterHora-16;
								while (aux < 0) aux +=32;
								punterHora = (aux)%32;
							}
							lcd.setCursor(punterHora%16,punterHora/16);
						}
					}
				}
			}
			else if (i == 3) {		//	Entrar / Avall				<--
				if (menu == 0) {
					if (submenu == 0) {
						printSubmenu10();
					}
				}
				else if (menu == 1) {
					if (submenu == 0) {
						printSubmenu20();
					}
					else if (submenu == 20) {
						if (punterHora > 3 and punterHora < 12) canviaHora(-1);
						else if (punterHora > 19 and punterHora < 30) canviaHora(-1);
						else punterHora = (punterHora+16)%32;
					}
					lcd.setCursor(punterHora%16,punterHora/16);
				}
				else if (menu == 2) {
					if (submenu == 0) {
						printSubmenu30();
					}
					else if (submenu == 30) {
						if (punterHora > 9 and punterHora < 13) canviaAlarma(0);
						else if (punterHora > 19 and punterHora < 28) canviaAlarma(-1);
						else if (punterHora == 16) {
							printSubmenu31();
						}
						else punterHora = (punterHora+16)%32;
						lcd.setCursor(punterHora%16,punterHora/16);
					}
					else if (submenu == 31) {
						if (punterHora > 9 and punterHora < 13) canviaDuradaAlarma(-1);
						else punterHora = (punterHora+16)%32;
						lcd.setCursor(punterHora%16,punterHora/16);
					}
				}
				else if (menu == 3) {
					if (submenu == 0) {
						printSubmenu40();
					}
					else if (submenu == 40) {
						if (punterHora > 9 and punterHora < 13) canviaEstatRadio();
						else if (punterHora > 25 and punterHora < 28) canviaVolumRadio(-1);
						else if (punterHora == 16) {
							printSubmenu41();
						}
						else punterHora = (punterHora+16)%32;
						lcd.setCursor(punterHora%16,punterHora/16);
					}
					else if (submenu == 41) {
						if (punterHora > 6 and punterHora < 12) canviaCanalRadio(-1);
						else if (punterHora > 17 and punterHora < 20) canviaEmissoraMarcadors(-1);
						else if (punterHora == 21) carregaMarcador();
						else if (punterHora == 29) guardaRadioMarcador();
						else if (punterHora == 16) {
							printSubmenu42();
						}
						else punterHora = (punterHora+16)%32;
						lcd.setCursor(punterHora%16,punterHora/16);
					}
					else if (submenu == 42) {
						punterHora = (punterHora+16)%32;
						lcd.setCursor(punterHora%16,punterHora/16);
					}
				}
				else if (menu == 4) {
					if (submenu == 0) {
						printSubmenu50();
					}
					else if (submenu == 50) {
						if (punterHora > 10 and punterHora < 15) canviaBrillantor(-1);
						else if (punterHora > 28 and punterHora < 32) canviaDuradaLlum(-1);
						else punterHora = (punterHora+16)%32;
						lcd.setCursor(punterHora%16,punterHora/16);
					}
				}
			}
			else if (i == 4) { 	//	?
			}
		}
	}
	if (ordenat) inactiu = false, inactivitat = 0;
	else inactiu = true;
}

void llegirBotons() {
	for (int i=0;i<5;++i){
		reading[i] = digitalRead(10-i);
		if (reading[i] != lastButtonState[i]) {
			lastDebounceTime[i] = millis();
		}
		if ((millis() - lastDebounceTime[i]) > debounceDelay) {
			buttonState[i] = reading[i];
		}
	}
	if (duradaLlum == 3 or (duradaLlum != 3 and inactivitat < duradaLlumArray[duradaLlum])) {
		accionaMenu();
	}
	else {
		boolean ordenat = false;
		for (int i=0;i<5 and not ordenat;++i) {
			if (reading[i] == HIGH and buttonState[i] == HIGH and lastButtonAccioned[i] == LOW) {
				lastButtonAccioned[i] = HIGH;
				ordenat = true;
			}
		}
		inactiu = !ordenat;
		if (!inactiu) {
			activaLCD();
			inactivitat = 0;
		}
	}
	for (int i=0;i<5;++i){
		if (buttonState[i] == LOW or reading[i] == LOW) lastButtonAccioned[i] = LOW;
		lastButtonState[i] = reading[i];
	}
}

void loop() {
	llegirBotons();
	if (alarmaActivaRadio ==  1) {
		if (estat == 0) canviaEstatRadio();
	}
	else if (alarmaActivaRadio ==  2) {
		alarmaActivaRadio = 0;
		if (estat == 1) canviaEstatRadio();
	}
}

