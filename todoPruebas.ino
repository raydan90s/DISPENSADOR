#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Servo.h>

// ------------ Hardware ------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
Servo servo;

const int SERVO_PIN = 10;
const int PIN_SENAL_SALIDA = 11;   // Señal hora programada
const int PIN_SENAL_ENTRADA = 12;  // Sensor de comida (HIGH = hay suficiente)
const int PIN_D = 13;              // Señal apertura forzada ('D')

// ------------ Keypad ------------
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

// ------------ Estados y datos de configuración ------------
enum AppState {
  WIZARD_NOMBRE,
  WIZARD_TIPO,
  WIZARD_TAMANO,
  WIZARD_VECES,
  WIZARD_HORAS,
  RUN
};

AppState state = WIZARD_NOMBRE;

String petName = "";
int petType = 0;   // 1=perro, 2=gato
int petSize = 0;   // 1=pequeño, 2=cachorro, 3=adulto

// Duraciones por tamaño (ms)
const int D_PEQ = 2500;
const int D_CACH = 3000;
const int D_ADUL = 3500;

int getDispenseDuration() {
  if (petSize == 1) return D_PEQ;
  if (petSize == 2) return D_CACH;
  return D_ADUL; // default adulto
}

// Veces que come y horas programadas
const int MAX_FEEDS = 8; // puedes subirlo si lo necesitas
int feedCount = 0;
int feedHour[MAX_FEEDS];
int feedMinute[MAX_FEEDS];

// Control para no repetir en el mismo minuto
int lastFedCode = -1; // hhmm del último despacho (evita repetir en ese minuto)

// ------------ Auxiliares de UI ------------
void lcdTitle(const char* line1, const char* line2 = nullptr) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(line1);
  if (line2) {
    lcd.setCursor(0,1);
    lcd.print(line2);
  }
}

void beepOK() { /* opcional: puedes poner un buzzer si tienes */ }

// Lee una opción numérica simple con keypad (retorna -1 si no hay tecla aún)
int readDigitOption() {
  char k = keypad.getKey();
  if (k >= '0' && k <= '9') return (k - '0');
  return -1;
}

// Lee un carácter para nombre: A-D y 0-9; 0 lo tomamos como espacio
// * = backspace, # = terminar (se maneja fuera)
bool appendNameChar(char k) {
  if ((k >= '0' && k <= '9') || (k >= 'A' && k <= 'D')) {
    if (petName.length() < 15) {
      if (k == '0') petName += ' ';
      else petName += k;
      return true;
    }
  }
  return false;
}

// Pide un número entre minV y maxV, mostrando prompt en la primera línea.
// Se confirma con '#'. '*' borra.
int promptNumber(const char* prompt, int minV, int maxV) {
  String buf = "";
  while (true) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(prompt);
    lcd.setCursor(0,1);
    lcd.print(buf);

    char k = keypad.getKey();
    if (k == NO_KEY) continue;
    if (k >= '0' && k <= '9') {
      if (buf.length() < 3) { // suficiente para 0-999
        buf += k;
      }
    } else if (k == '*') {
      if (buf.length() > 0) buf.remove(buf.length()-1);
    } else if (k == '#') {
      if (buf.length() == 0) continue;
      int v = buf.toInt();
      if (v >= minV && v <= maxV) {
        return v;
      } else {
        lcdTitle("Valor invalido", "Intentar de nuevo");
        delay(1200);
      }
    }
  }
}

// Pide una hora HHMM validada. Retorna true si OK y coloca h y m.
bool promptTimeHHMM(int &h, int &m, int idx = -1) {
  String buf = "";
  while (true) {
    lcd.clear();
    lcd.setCursor(0,0);
    if (idx >= 0) {
      lcd.print("Hora ");
      lcd.print(idx+1);
      lcd.print(" (HHMM):");
    } else {
      lcd.print("Hora (HHMM):");
    }
    lcd.setCursor(0,1);
    lcd.print(buf);

    char k = keypad.getKey();
    if (k == NO_KEY) continue;

    if (k >= '0' && k <= '9') {
      if (buf.length() < 4) {
        buf += k;
      }
    } else if (k == '*') {
      if (buf.length() > 0) buf.remove(buf.length()-1);
    } else if (k == '#') {
      if (buf.length() == 4) {
        int hh = buf.substring(0,2).toInt();
        int mm = buf.substring(2,4).toInt();
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          h = hh; m = mm;
          return true;
        } else {
          lcdTitle("Hora invalida!", "HH 0-23, MM 0-59");
          delay(1500);
          buf = "";
        }
      } else {
        lcdTitle("Formato: 4 digitos", "Ej: 0730, 1935");
        delay(1500);
      }
    }
  }
}

// Pide nombre con keypad: A-D y dígitos; 0 = espacio; * = borrar; # = confirmar
void promptName() {
  petName = "";
  lcdTitle("Nombre mascota:", "(A-D,0-9, 0=esp)");
  delay(1000);
  while (true) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Nombre:");
    lcd.setCursor(0,1);
    if (petName.length() == 0) lcd.print("_");
    else lcd.print(petName);

    char k = keypad.getKey();
    if (k == NO_KEY) continue;

    if (k == '#') {
      if (petName.length() > 0) {
        lcdTitle("Nombre guardado", petName.c_str());
        delay(1200);
        return;
      } else {
        lcdTitle("Nombre vacio", "Intenta de nuevo");
        delay(1000);
      }
    } else if (k == '*') {
      if (petName.length() > 0) petName.remove(petName.length()-1);
    } else {
      appendNameChar(k); // agrega si es válido
    }
  }
}

// Pide tipo: 1 perro, 2 gato
void promptType() {
  while (true) {
    lcdTitle("Tipo mascota:", "1 Perro 2 Gato");
    int opt = readDigitOption();
    if (opt == -1) continue;
    if (opt == 1 || opt == 2) {
      petType = opt;
      lcdTitle("Tipo guardado", (opt==1)?"Perro":"Gato");
      delay(1000);
      return;
    } else {
      lcdTitle("Opcion invalida", nullptr);
      delay(800);
    }
  }
}

// Pide tamaño: 1 pequeño, 2 cachorro, 3 adulto
void promptSize() {
  while (true) {
    lcdTitle("Tamano:", "1 Peq 2 Cach 3 Adu");
    int opt = readDigitOption();
    if (opt == -1) continue;
    if (opt >= 1 && opt <= 3) {
      petSize = opt;
      if (opt == 1) lcdTitle("Tamano guardado", "Pequeno");
      else if (opt == 2) lcdTitle("Tamano guardado", "Cachorro");
      else lcdTitle("Tamano guardado", "Adulto");
      delay(1000);
      return;
    } else {
      lcdTitle("Opcion invalida", nullptr);
      delay(800);
    }
  }
}

// Pide número de veces que come y las horas
void promptFeeds() {
  feedCount = promptNumber("Veces que come (1-8)", 1, MAX_FEEDS);
  for (int i=0;i<feedCount;i++) {
    int hh, mm;
    bool ok = promptTimeHHMM(hh, mm, i);
    if (ok) {
      feedHour[i] = hh;
      feedMinute[i] = mm;
      lcd.clear();
      lcd.print("Guardado: ");
      if (hh < 10) lcd.print('0');
      lcd.print(hh);
      lcd.print(':');
      if (mm < 10) lcd.print('0');
      lcd.print(mm);
      delay(900);
    } else {
      i--; // reintentar
    }
  }
  lcdTitle("Horarios listos", "Presiona A=menu");
  delay(1200);
}

// Muestra resumen de config
void showSummary() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(petName);
  lcd.print(" ");
  lcd.print((petType==1)?"Perro":"Gato");

  lcd.setCursor(0,1);
  if (petSize==1) lcd.print("Peq ");
  else if (petSize==2) lcd.print("Cach ");
  else lcd.print("Adulto ");
  lcd.print(feedCount);
  lcd.print(" veces");
  delay(1500);
}

// Abre el servo según duración configurada, con chequeo del sensor
void doDispense(bool fromSchedule) {
  int duration = getDispenseDuration();

  if (fromSchedule) digitalWrite(PIN_SENAL_SALIDA, HIGH);
  int estado = digitalRead(PIN_SENAL_ENTRADA);  // HIGH = hay comida

  if (estado == HIGH) {
    lcdTitle("Abriendo servo", nullptr);
    servo.write(90);
    delay(duration);
    servo.write(0);
    lcdTitle("Proceso listo", nullptr);
  } else {
    lcdTitle("Poca comida", "Revisar tolva");
  }

  if (fromSchedule) digitalWrite(PIN_SENAL_SALIDA, LOW);
  delay(1200);
}

// Menú de reconfiguración (A)
void configMenu() {
  while (true) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Ajustes: 1Nom 2Tip");
    lcd.setCursor(0,1);
    lcd.print("3Tam 4Horas #Salir");
    char k = keypad.getKey();
    if (k == NO_KEY) continue;
    if (k == '#') {
      lcdTitle("Saliendo", nullptr);
      delay(600);
      return;
    }
    if (k == '1') { promptName(); showSummary(); }
    else if (k == '2') { promptType(); showSummary(); }
    else if (k == '3') { promptSize(); showSummary(); }
    else if (k == '4') { promptFeeds(); showSummary(); }
    else {
      lcdTitle("Opcion invalida", nullptr);
      delay(700);
    }
  }
}

// ------------ Setup & Loop ------------
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

  lcdTitle("Dispensador listo", "Iniciando...");
  delay(800);

  // Arrancamos el asistente:
  state = WIZARD_NOMBRE;
}

void loop() {
  // --- Apertura forzada con 'D' ---
  char k = keypad.getKey();
  if (k == 'D') {
    lcdTitle("Apertura forzada", nullptr);
    digitalWrite(PIN_D, HIGH);
    doDispense(false);
    digitalWrite(PIN_D, LOW);
    return;
  }

  // --- Menú de configuración con 'A' en cualquier momento ---
  if (k == 'A') {
    configMenu();
    return;
  }

  // --- Estado: Asistente de configuración ---
  if (state != RUN) {
    switch (state) {
      case WIZARD_NOMBRE:
        promptName();
        state = WIZARD_TIPO;
        break;
      case WIZARD_TIPO:
        promptType();
        state = WIZARD_TAMANO;
        break;
      case WIZARD_TAMANO:
        promptSize();
        state = WIZARD_VECES;
        break;
      case WIZARD_VECES:
        // Reutilizamos promptFeeds que incluye veces + horas
        promptFeeds();
        state = RUN;
        break;
      default: break;
    }
    if (state == RUN) {
      showSummary();
    }
    return;
  }

  // --- Estado RUN: mostrar reloj y próximos horarios, y disparar si corresponde ---
  DateTime now = rtc.now();
  int hh = now.hour();
  int mm = now.minute();

  // Mostrar cada 1 segundo
  static unsigned long lastUpdate = 0;
  unsigned long ms = millis();
  if (ms - lastUpdate >= 1000) {
    lastUpdate = ms;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Hora ");
    if (hh < 10) lcd.print('0');
    lcd.print(hh); lcd.print(':');
    if (mm < 10) lcd.print('0');
    lcd.print(mm);

    lcd.setCursor(0,1);
    // Mostrar primer horario o "Ajustes: A"
    if (feedCount > 0) {
      lcd.print("Comidas: ");
      for (int i=0;i<feedCount && i<2;i++) { // muestra hasta dos por espacio
        if (feedHour[i] < 10) lcd.print('0');
        lcd.print(feedHour[i]);
        lcd.print(':');
        if (feedMinute[i] < 10) lcd.print('0');
        lcd.print(feedMinute[i]);
        if (i==0 && feedCount>1) lcd.print(' ');
      }
    } else {
      lcd.print("Ajustes: A");
    }
  }

  // Disparar si coincide alguna hora (una sola vez por minuto)
  if (feedCount > 0) {
    int codeNow = hh*100 + mm;
    if (codeNow != lastFedCode) {
      bool match = false;
      for (int i=0;i<feedCount;i++) {
        if (feedHour[i] == hh && feedMinute[i] == mm) {
          match = true;
          break;
        }
      }
      if (match) {
        lastFedCode = codeNow;
        doDispense(true);
      }
    }
  }
}
