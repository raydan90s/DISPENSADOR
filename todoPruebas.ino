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

// Sistema T9/multitap para escritura de texto - A empieza en 1
char t9map[10][5] = {
  { ' ', ' ', ' ', ' ', ' ' },  // 0 = espacio
  { 'A', 'B', 'C', ' ', ' ' },  // 1
  { 'D', 'E', 'F', ' ', ' ' },  // 2  
  { 'G', 'H', 'I', ' ', ' ' },  // 3
  { 'J', 'K', 'L', ' ', ' ' },  // 4
  { 'M', 'N', 'O', ' ', ' ' },  // 5
  { 'P', 'Q', 'R', 'S', ' ' },  // 6
  { 'T', 'U', 'V', ' ', ' ' },  // 7
  { 'W', 'X', 'Y', 'Z', ' ' },  // 8
  { '9', ' ', ' ', ' ', ' ' }    // 9 solo número
};

// Variables para multitap
char currentKey = 0;
int currentIndex = 0;
unsigned long lastKeyTime = 0;
const unsigned long MULTITAP_TIMEOUT = 1000; // 1 segundo timeout

char getMultitapChar(char key) {
  if (key >= '0' && key <= '9') {
    int keyNum = key - '0';
    
    // Contar caracteres válidos en esta tecla
    int letterCount = 0;
    for (int i = 0; i < 5; i++) {
      if (t9map[keyNum][i] != ' ') letterCount++;
      else break;
    }
    
    if (letterCount == 0) return 0; // Tecla sin letras
    
    unsigned long now = millis();
    
    if (key == currentKey && (now - lastKeyTime) < MULTITAP_TIMEOUT) {
      // Misma tecla presionada dentro del timeout - avanzar al siguiente caracter
      currentIndex = (currentIndex + 1) % letterCount;
    } else {
      // Nueva tecla o timeout - empezar desde el primer caracter
      currentKey = key;
      currentIndex = 0;
    }
    
    lastKeyTime = now;
    return t9map[keyNum][currentIndex];
  }
  return 0;
}

void resetMultitap() {
  currentKey = 0;
  currentIndex = 0;
  lastKeyTime = 0;
}

bool appendNameChar(char k) {
  // Esta función ya no se usa, la lógica está en promptName()
  return false;
}

int promptNumber(const char* prompt, int minV, int maxV) {
  String buf = "";
  String lastDisplay = ""; // Para evitar reescribir innecesariamente
  
  while (true) {
    // Solo actualizar pantalla si hay cambios
    String currentDisplay = String(prompt) + "\n" + buf;
    if (currentDisplay != lastDisplay) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print(prompt);
      lcd.setCursor(0,1);
      lcd.print(buf);
      lastDisplay = currentDisplay;
    }

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
        lastDisplay = ""; // Forzar refresco
      }
    }
  }
}

bool promptTimeHHMM(int &h, int &m, int idx = -1) {
  String buf = "";
  String lastDisplay = ""; // Para evitar reescribir innecesariamente
  
  while (true) {
    // Solo actualizar pantalla si hay cambios
    String promptText = (idx >= 0) ? ("Hora " + String(idx+1) + " (HHMM):") : "Hora (HHMM):";
    String currentDisplay = promptText + "\n" + buf;
    
    if (currentDisplay != lastDisplay) {
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
      lastDisplay = currentDisplay;
    }

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
          lastDisplay = ""; // Forzar refresco
        }
      } else {
        lcdTitle("Formato: 4 digitos", "Ej: 0730, 1935");
        delay(1500);
        lastDisplay = ""; // Forzar refresco
      }
    }
  }
}

void reproducirPista(int numero) {
  Serial.write(0x7E);           // Start byte
  Serial.write(0xFF);           // Version
  Serial.write(0x06);           // Length
  Serial.write(0x03);           // Command: play track by index
  Serial.write(0x00);           // No feedback
  Serial.write((numero >> 8) & 0xFF); // High byte
  Serial.write(numero & 0xFF);        // Low byte
  Serial.write(0xEF);           // End byte
}

void promptName() {
  petName = "";
  resetMultitap();
  
  lcd.clear();
  lcd.print("Ingresa nombre:");
  lcd.setCursor(0, 1);
  
  while (true) {
    char k = keypad.getKey();
    
    // Verificar timeout de multitap y aceptar letra pendiente
    unsigned long now = millis();
    if (currentKey != 0 && (now - lastKeyTime) >= MULTITAP_TIMEOUT) {
      // Aceptar la letra que estaba pendiente
      petName += t9map[currentKey - '0'][currentIndex];
      lcd.setCursor(0, 1);
      lcd.print(petName);
      resetMultitap();
    }
    
    if (k == NO_KEY) continue;

    if (k == '#') {
      if (petName.length() > 0) {
        // Aceptar última letra si estaba en multitap
        if (currentKey != 0) {
          petName += t9map[currentKey - '0'][currentIndex];
          resetMultitap();
        }
        lcdTitle("Nombre guardado", petName.c_str());
        delay(1200);
        return;
      } else {
        lcdTitle("Nombre vacio", "Intenta de nuevo");
        delay(1000);
        lcd.clear();
        lcd.print("Ingresa nombre:");
        lcd.setCursor(0, 1);
        lcd.print(petName);
      }
    } else if (k == '*') {
      if (petName.length() > 0) {
        petName.remove(petName.length()-1);
        resetMultitap();
        lcd.clear();
        lcd.print("Ingresa nombre:");
        lcd.setCursor(0, 1);
        lcd.print(petName);
      }
    } else if (k >= '0' && k <= '9') {
      if (k >= '1' && k <= '8') { // Solo teclas con letras
        if (k == currentKey && (now - lastKeyTime) < MULTITAP_TIMEOUT) {
          // Misma tecla - ciclar al siguiente caracter
          int keyNum = k - '0';
          int letterCount = 0;
          for (int i = 0; i < 5; i++) {
            if (t9map[keyNum][i] != ' ') letterCount++;
            else break;
          }
          currentIndex = (currentIndex + 1) % letterCount;
        } else {
          // Nueva tecla - aceptar letra anterior si había
          if (currentKey != 0) {
            petName += t9map[currentKey - '0'][currentIndex];
          }
          currentKey = k;
          currentIndex = 0;
        }
        
        lastKeyTime = now;
        
        // Mostrar letra temporal
        if (petName.length() < 15) {
          lcd.setCursor(0, 1);
          lcd.print(petName);
          lcd.print(t9map[k - '0'][currentIndex]);
        }
      } else if (k == '0') {
        // Espacio
        if (currentKey != 0) {
          petName += t9map[currentKey - '0'][currentIndex];
          resetMultitap();
        }
        if (petName.length() < 15) {
          petName += ' ';
          lcd.setCursor(0, 1);
          lcd.print(petName);
        }
      }
    }
  }
}

void promptFeeds() {
  feedCount = promptNumber("Veces que come (1-8)", 1, MAX_FEEDS);
  
  // Limpiar pantalla y mostrar mensaje inicial
  lcdTitle("Guardando horarios...", "");
  delay(500); // Pausa breve para mostrar el mensaje
  
  for (int i = 0; i < feedCount; i++) {
    int hh, mm;
    bool ok = promptTimeHHMM(hh, mm, i);
    
    if (ok) {
      feedHour[i] = hh;
      feedMinute[i] = mm;

      // Mostrar confirmación sin parpadeo
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Guardando...");
      lcd.setCursor(0, 1);
      lcd.print("Guardado: ");
      if (hh < 10) lcd.print('0');
      lcd.print(hh);
      lcd.print(':');
      if (mm < 10) lcd.print('0');
      lcd.print(mm);
      delay(900);
    } else {
      i--; // Si la hora es inválida, vuelve a intentar
    }
  }

  // Mensaje final
  lcdTitle("Horarios listos", "Presiona A=menu");
  delay(1200);
}

void promptType() {
  String lastDisplay = "";
  int opt = -1;
  
  while (opt == -1) {
    String currentDisplay = "Tipo mascota:\n1 Perro 2 Gato";
    
    if (currentDisplay != lastDisplay) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Tipo mascota:");
      lcd.setCursor(0, 1);
      lcd.print("1 Perro 2 Gato");
      lastDisplay = currentDisplay;
    }

    opt = readDigitOption();

    if (opt == 1 || opt == 2) {
      petType = opt;
      lcdTitle("Tipo guardado", (opt == 1) ? "Perro" : "Gato");
      delay(1000);
    } else if (opt != -1) {
      lcdTitle("Opcion invalida", nullptr);
      delay(800);
      lastDisplay = ""; // Forzar refresco
    }
  }
}

void promptSize() {
  String lastDisplay = "";
  
  while (true) {
    String currentDisplay = "Tamano:\n1 Peq 2 Cach 3 Adu";
    
    if (currentDisplay != lastDisplay) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Tamano:");
      lcd.setCursor(0, 1);
      lcd.print("1 Peq 2 Cach 3 Adu");
      lastDisplay = currentDisplay;
    }

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
      lastDisplay = ""; // Forzar refresco
    }
  }
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
void esperarFinPista() {
  while (true) {
    if (Serial.available() >= 10) {
      if (Serial.read() == 0x7E) {
        byte buffer[9];
        Serial.readBytes(buffer, 9);
        if (buffer[2] == 0x3D) { // Código de fin de pista
          break;
        }
      }
    }
  }
}
// Abre el servo según duración configurada, con chequeo del sensor
void doDispense(bool fromSchedule) {
  int duration = getDispenseDuration();
  unsigned long tiempoInicio;
  unsigned long duracionTotal = 60000;
  if (fromSchedule) digitalWrite(PIN_SENAL_SALIDA, HIGH);
  int estado = digitalRead(PIN_SENAL_ENTRADA);  // HIGH = hay comida

  if (estado == HIGH) {
    Serial.begin(9600);
    delay(1000);
    reproducirPista(3);
    lcdTitle("Abriendo servo", nullptr);
    servo.write(90);
    delay(duration);
    servo.write(0);
    lcdTitle("Proceso listo", nullptr);
  } else {
    tiempoInicio = millis();
    while (millis() - tiempoInicio < duracionTotal) {
      lcdTitle("Poca comida", "Revisar tolva");
      Serial.begin(9600);
      delay(1000);
      reproducirPista(1); // Reproduce la pista
      delay(4000);
    }
  }
  if (fromSchedule) digitalWrite(PIN_SENAL_SALIDA, LOW);
  delay(1200);
}

// Menú de reconfiguración (A)
// Menú de reconfiguración (A) - VERSIÓN CORREGIDA
void configMenu() {
  String lastDisplay = "";
  
  while (true) {
    String currentDisplay = "Ajustes: 1Nomb 2Tiem\n3Tam 4Horas #Salir";
    
    // Solo actualizar pantalla si hay cambios
    if (currentDisplay != lastDisplay) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Ajustes: 1Nom 2Tip");
      lcd.setCursor(0,1);
      lcd.print("3Tam 4Horas #Salir");
      lastDisplay = currentDisplay;
    }
    
    char k = keypad.getKey();
    if (k == NO_KEY) continue;
    
    if (k == '#') {
      lcdTitle("Saliendo", nullptr);
      delay(600);
      return;
    }
    if (k == '1') { 
      promptName(); 
      showSummary(); 
      lastDisplay = ""; // Forzar refresco al volver
    }
    else if (k == '2') { 
      promptType(); 
      showSummary(); 
      lastDisplay = ""; // Forzar refresco al volver
    }
    else if (k == '3') { 
      promptSize(); 
      showSummary(); 
      lastDisplay = ""; // Forzar refresco al volver
    }
    else if (k == '4') { 
      promptFeeds(); 
      showSummary(); 
      lastDisplay = ""; // Forzar refresco al volver
    }
    else {
      lcdTitle("Opcion invalida", nullptr);
      delay(700);
      lastDisplay = ""; // Forzar refresco después del mensaje de error
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
  // --- Estado RUN: mostrar reloj y próximos horarios, y disparar si corresponde ---
  DateTime now = rtc.now();
  int hh = now.hour();
  int mm = now.minute();

  // Variables estáticas para controlar la rotación de pantalla
  static unsigned long lastUpdate = 0;
  static unsigned long lastCycle = 0;
  static int currentDisplay = 0; // 0 = hora actual, 1-n = comidas
  static int lastMinute = -1; // Para detectar cambio de minuto
  
  unsigned long ms = millis();
  
  // Cambiar pantalla cada 3 segundos
  if (ms - lastCycle >= 3000) {
    lastCycle = ms;
    if (feedCount > 0) {
      currentDisplay++;
      if (currentDisplay > feedCount) {
        currentDisplay = 0; // Volver a mostrar hora
      }
    } else {
      currentDisplay = 0; // Solo hora si no hay comidas
    }
  }
  
  // Actualizar pantalla solo cuando:
  // - Cambiamos de pantalla (recién rotamos)
  // - Estamos en hora y cambió el minuto
  bool shouldUpdate = false;
  
  if (ms - lastCycle < 100) { 
    // Acabamos de cambiar pantalla
    shouldUpdate = true;
    if (currentDisplay == 0) {
      lastMinute = mm; // Actualizar referencia de minuto
    }
  } else if (currentDisplay == 0 && mm != lastMinute) {
    // Estamos en modo hora y cambió el minuto
    shouldUpdate = true;
    lastMinute = mm;
  }
  
  if (shouldUpdate) {
    lcd.clear();
    
    // Primera línea: siempre el nombre de la mascota
    lcd.setCursor(0,0);
    lcd.print(petName);
    
    // Segunda línea: hora actual o comida según currentDisplay
    lcd.setCursor(0,1);
    if (currentDisplay == 0) {
      // Mostrar hora actual
      lcd.print("Hora: ");
      if (hh < 10) lcd.print('0');
      lcd.print(hh);
      lcd.print(':');
      if (mm < 10) lcd.print('0');
      lcd.print(mm);
    } else {
      // Mostrar comida específica (currentDisplay 1-n corresponde a índice 0-(n-1))
      int comidaIndex = currentDisplay - 1;
      lcd.print("Comida ");
      lcd.print(currentDisplay);
      lcd.print(": ");
      if (feedHour[comidaIndex] < 10) lcd.print('0');
      lcd.print(feedHour[comidaIndex]);
      lcd.print(':');
      if (feedMinute[comidaIndex] < 10) lcd.print('0');
      lcd.print(feedMinute[comidaIndex]);
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
