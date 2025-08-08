#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
Servo servo;

const int SERVO_PIN = 10;
const int SERVO_ABIERTO = 90;
const int SERVO_CERRADO = 0;

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[ROWS] = { 2, 3, 4, 5 };
byte colPins[COLS] = { 6, 7, 8, 9 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

unsigned long lastUpdate = 0;
const unsigned long updateInterval = 1000;

String nombre = "";
char t9map[10][5] = {
  { ' ', ' ', ' ', ' ', ' ' },
  { ' ', ' ', ' ', ' ', ' ' },
  { 'A', 'B', 'C', '2', ' ' },
  { 'D', 'E', 'F', '3', ' ' },
  { 'G', 'H', 'I', '4', ' ' },
  { 'J', 'K', 'L', '5', ' ' },
  { 'M', 'N', 'O', '6', ' ' },
  { 'P', 'Q', 'R', 'S', '7' },
  { 'T', 'U', 'V', '8', ' ' },
  { 'W', 'X', 'Y', 'Z', '9' }
};
int t9Index = 0;
char lastKey = '\0';
unsigned long lastKeyPressTime = 0;
const unsigned long T9_TIMEOUT = 1000;

String horaComida = "";
bool ingresandoNombre = true;
bool ingresandoHora = false;
bool esperandoDispensar = false;

const int EEPROM_ADDR_NOMBRE = 0;
const int EEPROM_MAX_NOMBRE = 15;
const int EEPROM_ADDR_HORA = EEPROM_ADDR_NOMBRE + EEPROM_MAX_NOMBRE + 1;
const int EEPROM_USED = EEPROM_ADDR_HORA + 5;

bool mostrarProximaHora = true;
unsigned long ultimoCambioMensaje = 0;
const unsigned long alternarIntervalo = 3000;

void borrarEEPROM() {
  for (int i = 0; i < EEPROM_USED; i++) {
    EEPROM.write(i, 0xFF);
  }
}

void EEPROM_writeString(int addr, const String &str, int maxLen) {
  int len = str.length();
  if (len > maxLen) len = maxLen;
  EEPROM.write(addr, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + 1 + i, str[i]);
  }
  for (int i = len; i < maxLen; i++) {
    EEPROM.write(addr + 1 + i, 0xFF);
  }
}

String EEPROM_readString(int addr, int maxLen) {
  int len = EEPROM.read(addr);
  if (len > maxLen || len < 0) return "";
  char buffer[maxLen + 1];
  for (int i = 0; i < len; i++) {
    buffer[i] = EEPROM.read(addr + 1 + i);
  }
  buffer[len] = '\0';
  return String(buffer);
}

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print("Inicializando...");

  if (!rtc.begin()) {
    lcd.clear();
    lcd.print("RTC no encontrado");
    while (1);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC sin energia, ajustando hora...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    delay(2000);
  }

  // Para borrar todo y empezar de nuevo, descomenta esta línea:
  borrarEEPROM();

  servo.attach(SERVO_PIN);
  servo.write(SERVO_CERRADO);

  nombre = EEPROM_readString(EEPROM_ADDR_NOMBRE, EEPROM_MAX_NOMBRE);
  horaComida = EEPROM_readString(EEPROM_ADDR_HORA, 4);

  if (nombre.length() > 0 && horaComida.length() == 4) {
    ingresandoNombre = false;
    ingresandoHora = false;
    esperandoDispensar = true;
    mostrarInfoFinal();
  } else {
    nombre = "";
    horaComida = "";
    ingresandoNombre = true;
    ingresandoHora = false;
    esperandoDispensar = false;
    lcd.clear();
    lcd.print("Ingresa nombre:");
    lcd.setCursor(0, 1);
  }
}

void loop() {
  if (ingresandoNombre) {
    char key = keypad.getKey();
    if (key != NO_KEY) {
      if (key >= '2' && key <= '9') {
        if (key == lastKey) {
          t9Index = (t9Index + 1) % 5;
        } else {
          aceptarLetra();
          lastKey = key;
          t9Index = 0;
        }
        lastKeyPressTime = millis();
        mostrarLetraTemporal(t9map[key - '0'][t9Index]);
      } else if (key == '#') {
        aceptarLetra();
        ingresandoNombre = false;
        ingresandoHora = true;
        lcd.clear();
        lcd.print("Hora comida HHMM:");
        lcd.setCursor(0, 1);
      } else if (key == '*') {
        if (nombre.length() > 0) {
          nombre.remove(nombre.length() - 1);
          lcd.clear();
          lcd.print("Ingresa nombre:");
          lcd.setCursor(0, 1);
          lcd.print(nombre);
        }
      }
    }
    if (lastKey != '\0' && millis() - lastKeyPressTime > T9_TIMEOUT) {
      aceptarLetra();
    }

  } else if (ingresandoHora) {
    char key = keypad.getKey();
    if (key != NO_KEY) {
      if (key >= '0' && key <= '9' && horaComida.length() < 4) {
        horaComida += key;
        lcd.print(key);
      } else if (key == '#') {
        if (horaComida.length() == 4) {
          ingresandoHora = false;
          esperandoDispensar = true;
          mostrarInfoFinal();
          EEPROM_writeString(EEPROM_ADDR_NOMBRE, nombre, EEPROM_MAX_NOMBRE);
          EEPROM_writeString(EEPROM_ADDR_HORA, horaComida, 4);
        } else {
          lcd.clear();
          lcd.print("Hora invalida!");
          delay(1000);
          lcd.clear();
          lcd.print("Hora comida HHMM:");
          lcd.setCursor(0, 1);
          lcd.print(horaComida);
        }
      } else if (key == '*') {
        if (horaComida.length() > 0) {
          horaComida.remove(horaComida.length() - 1);
          lcd.clear();
          lcd.print("Hora comida HHMM:");
          lcd.setCursor(0, 1);
          lcd.print(horaComida);
        }
      }
    }

  } else if (esperandoDispensar) {
    DateTime ahora = rtc.now();
    int horaActual = ahora.hour();
    int minutoActual = ahora.minute();

    int horaSet = horaComida.substring(0, 2).toInt();
    int minutoSet = horaComida.substring(2, 4).toInt();

    if (millis() - lastUpdate >= updateInterval) {
      lastUpdate = millis();

      lcd.setCursor(0, 0);
      lcd.print(nombre);
      lcd.print("      ");

      if (millis() - ultimoCambioMensaje >= alternarIntervalo) {
        mostrarProximaHora = !mostrarProximaHora;
        ultimoCambioMensaje = millis();
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }

      lcd.setCursor(0, 1);
      if (mostrarProximaHora) {
        lcd.print("Comida: ");
        if (horaSet < 10) lcd.print('0');
        lcd.print(horaSet);
        lcd.print(':');
        if (minutoSet < 10) lcd.print('0');
        lcd.print(minutoSet);
        lcd.print("     ");
      } else {
        lcd.print("Ahora: ");
        if (horaActual < 10) lcd.print('0');
        lcd.print(horaActual);
        lcd.print(':');
        if (minutoActual < 10) lcd.print('0');
        lcd.print(minutoActual);
        lcd.print("     ");
      }
    }

    if (horaActual == horaSet && minutoActual == minutoSet) {
      lcd.clear();
      lcd.print("Dispensando comida");
      lcd.setCursor(0, 1);
      lcd.print(nombre);

      servo.write(SERVO_ABIERTO);
      delay(3000); // Mantener abierto
      servo.write(SERVO_CERRADO);

      esperandoDispensar = false;
      lcd.clear();
      lcd.print("Fin del proceso");
      delay(2000);
      lcd.clear();
    }
  }
}

void aceptarLetra() {
  if (lastKey != '\0') {
    nombre += t9map[lastKey - '0'][t9Index];
    lcd.setCursor(0, 1);
    lcd.print(nombre);
    lastKey = '\0';
    t9Index = 0;
  }
}

void mostrarLetraTemporal(char letra) {
  lcd.setCursor(nombre.length(), 1);
  lcd.print(letra);
}

void mostrarInfoFinal() {
  lcd.clear();
  lcd.print(nombre);
  lcd.setCursor(0, 1);
  lcd.print("Comida: ");
  lcd.print(horaComida.substring(0, 2));
  lcd.print(":");
  lcd.print(horaComida.substring(2, 4));
}
