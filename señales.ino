#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Servo.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
Servo servo;

const int SERVO_PIN = 10;
const int PIN_SENAL_SALIDA = 11;   // Pin 11 para señal hora programada
const int PIN_SENAL_ENTRADA = 12;  // Pin 12 para leer sensor de comida
const int PIN_D = 13;               // Pin 13 para señal apertura forzada ('D')

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

String horaComida = "";
bool ingresandoHora = true;
bool horaListo = false;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  pinMode(PIN_SENAL_SALIDA, OUTPUT);
  pinMode(PIN_D, OUTPUT);
  pinMode(PIN_SENAL_ENTRADA, INPUT);

  servo.attach(SERVO_PIN);
  servo.write(0);  // Servo cerrado

  if (!rtc.begin()) {
    lcd.print("RTC no encontrado");
    while (1);
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  lcd.print("Hora comida HHMM:");
  lcd.setCursor(0, 1);
}

void loop() {
  char key = keypad.getKey();

  if (ingresandoHora) {
    if (key != NO_KEY) {
      if (key >= '0' && key <= '9' && horaComida.length() < 4) {
        horaComida += key;
        lcd.print(key);
      } else if (key == '#') {
        if (horaComida.length() == 4) {
          ingresandoHora = false;
          horaListo = true;
          lcd.clear();
          lcd.print("Hora guardada:");
          lcd.setCursor(0, 1);
          lcd.print(horaComida.substring(0, 2));
          lcd.print(":");
          lcd.print(horaComida.substring(2, 4));
          delay(2000);
          lcd.clear();
        } else {
          lcd.clear();
          lcd.print("Hora invalida!");
          delay(1500);
          horaComida = "";
          lcd.clear();
          lcd.print("Hora comida HHMM:");
          lcd.setCursor(0, 1);
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
  }
  else if (horaListo) {
    // 1. Abrir forzado si se presiona 'D' (sin importar sensor)
    if (key == 'D') {
      lcd.clear();
      lcd.print("Apertura forzada");
      digitalWrite(PIN_D, HIGH);
      servo.write(90);
      delay(3000);
      servo.write(0);
      digitalWrite(PIN_D, LOW);
      lcd.clear();
      lcd.print("Proceso listo");
      delay(2000);
      lcd.clear();
      lcd.print("Esperando hora");
      return;  // Salir para no hacer más en esta iteración
    }

    // 2. Checar hora normal para apertura programada
    DateTime ahora = rtc.now();
    int horaActual = ahora.hour();
    int minutoActual = ahora.minute();

    int horaSet = horaComida.substring(0, 2).toInt();
    int minutoSet = horaComida.substring(2, 4).toInt();

    if (horaActual == horaSet && minutoActual == minutoSet) {
      digitalWrite(PIN_SENAL_SALIDA, HIGH);  // Señal hora programada
      int estado = digitalRead(PIN_SENAL_ENTRADA);  // Leer sensor comida

      if (estado == HIGH) {
        lcd.clear();
        lcd.print("Abriendo servo");
        servo.write(90);
        delay(3000);
        servo.write(0);
        lcd.clear();
        lcd.print("Proceso listo");
        delay(2000);
        lcd.clear();
        lcd.print("Esperando hora");
      } else {
        lcd.clear();
        lcd.print("Poca comida");
        delay(2000);
        lcd.clear();
        lcd.print("Esperando hora");
      }

      digitalWrite(PIN_SENAL_SALIDA, LOW);
      delay(60000);  // Para no repetir en el mismo minuto
    }
    else {
      // Mostrar hora actual y programada
      static unsigned long lastUpdate = 0;
      unsigned long now = millis();
      if (now - lastUpdate >= 1000) {
        lastUpdate = now;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Hora actual:");
        if (horaActual < 10) lcd.print('0');
        lcd.print(horaActual);
        lcd.print(":");
        if (minutoActual < 10) lcd.print('0');
        lcd.print(minutoActual);

        lcd.setCursor(0, 1);
        lcd.print("Dispensar a ");
        lcd.print(horaComida.substring(0, 2));
        lcd.print(":");
        lcd.print(horaComida.substring(2, 4));
      }
    }
  }
}
