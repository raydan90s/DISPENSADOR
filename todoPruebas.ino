  #include <Wire.h>
  #include <RTClib.h>
  #include <LiquidCrystal_I2C.h>
  #include <Keypad.h>
  #include <Servo.h>
  #include <EEPROM.h>

  // ------------ Hardware ------------
  LiquidCrystal_I2C lcd(0x27, 16, 2);
  RTC_DS3231 rtc;
  Servo servo;

  const int SERVO_PIN = 10;
  const int PIN_SENAL_SALIDA = 11;   // Señal hora programada
  const int PIN_SENAL_ENTRADA = 12;  // Sensor de comida (HIGH = hay suficiente)
  const int PIN_D = 13;              // Señal apertura forzada ('D')

  // ------------ EEPROM Layout ------------
  const int EEPROM_USED = 512;
  const int ADDR_CONFIG_FLAG = 0;     // 1 byte: 0xAA si hay config válida
  const int ADDR_PET_NAME = 1;        // 16 bytes para nombre
  const int ADDR_PET_TYPE = 17;       // 1 byte: tipo (1=perro, 2=gato)
  const int ADDR_PET_SIZE = 18;       // 1 byte: tamaño (1=pequeño, 2=cachorro, 3=adulto)
  const int ADDR_FEED_COUNT = 19;     // 1 byte: cantidad de comidas
  const int ADDR_FEED_HOURS = 20;     // 8 bytes: horas de comida
  const int ADDR_FEED_MINUTES = 28;   // 8 bytes: minutos de comida

  const byte CONFIG_FLAG = 0xAA;      // Marca para indicar config válida

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
  const int MAX_FEEDS = 8;
  int feedCount = 0;
  int feedHour[MAX_FEEDS];
  int feedMinute[MAX_FEEDS];

  // Control para no repetir en el mismo minuto
  int lastFedCode = -1;

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

  // ------------ Funciones EEPROM ------------
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

  // Guardar configuración en EEPROM
  void saveConfig() {
    EEPROM.write(ADDR_CONFIG_FLAG, CONFIG_FLAG);
    EEPROM_writeString(ADDR_PET_NAME, petName, 15);
    EEPROM.write(ADDR_PET_TYPE, petType);
    EEPROM.write(ADDR_PET_SIZE, petSize);
    EEPROM.write(ADDR_FEED_COUNT, feedCount);
    
    // Guardar horarios
    for (int i = 0; i < feedCount; i++) {
      EEPROM.write(ADDR_FEED_HOURS + i, feedHour[i]);
      EEPROM.write(ADDR_FEED_MINUTES + i, feedMinute[i]);
    }
  }

  // Cargar configuración desde EEPROM
  bool loadConfig() {
    if (EEPROM.read(ADDR_CONFIG_FLAG) != CONFIG_FLAG) {
      return false; // No hay configuración válida
    }
    
    petName = EEPROM_readString(ADDR_PET_NAME, 15);
    petType = EEPROM.read(ADDR_PET_TYPE);
    petSize = EEPROM.read(ADDR_PET_SIZE);
    feedCount = EEPROM.read(ADDR_FEED_COUNT);
    
    // Validar datos básicos
    if (petName.length() == 0 || petType < 1 || petType > 2 || 
        petSize < 1 || petSize > 3 || feedCount < 1 || feedCount > MAX_FEEDS) {
      return false;
    }
    
    // Cargar horarios
    for (int i = 0; i < feedCount; i++) {
      feedHour[i] = EEPROM.read(ADDR_FEED_HOURS + i);
      feedMinute[i] = EEPROM.read(ADDR_FEED_MINUTES + i);
      
      // Validar horarios
      if (feedHour[i] < 0 || feedHour[i] > 23 || 
          feedMinute[i] < 0 || feedMinute[i] > 59) {
        return false;
      }
    }
    
    return true;
  }

  // Reset de fábrica
  void factoryReset() {
    lcdTitle("Reset de fabrica", "Borrando datos...");
    delay(1000);
    
    // Borrar EEPROM
    borrarEEPROM();
    
    // Reiniciar variables
    petName = "";
    petType = 0;
    petSize = 0;
    feedCount = 0;
    lastFedCode = -1;
    
    lcdTitle("Reset completo", "Reiniciando...");
    delay(2000);
    
    // Reiniciar el asistente
    state = WIZARD_NOMBRE;
  }

  // Lee una opción numérica simple con keypad (retorna -1 si no hay tecla aún)
  int readDigitOption() {
    char k = keypad.getKey();
    if (k >= '0' && k <= '9') return (k - '0');
    return -1;
  }

  // Sistema T9/multitap para escritura de texto
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
  const unsigned long MULTITAP_TIMEOUT = 1000;

  char getMultitapChar(char key) {
    if (key >= '0' && key <= '9') {
      int keyNum = key - '0';
      
      int letterCount = 0;
      for (int i = 0; i < 5; i++) {
        if (t9map[keyNum][i] != ' ') letterCount++;
        else break;
      }
      
      if (letterCount == 0) return 0;
      
      unsigned long now = millis();
      
      if (key == currentKey && (now - lastKeyTime) < MULTITAP_TIMEOUT) {
        currentIndex = (currentIndex + 1) % letterCount;
      } else {
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

  int promptNumber(const char* prompt, int minV, int maxV) {
    String buf = "";
    String lastDisplay = "";
    
    while (true) {
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
        if (buf.length() < 3) {
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
          lastDisplay = "";
        }
      }
    }
  }

  bool promptTimeHHMM(int &h, int &m, int idx = -1) {
    String buf = "";
    String lastDisplay = "";
    
    while (true) {
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
            lastDisplay = "";
          }
        } else {
          lcdTitle("Formato: 4 digitos", "Ej: 0730, 1935");
          delay(1500);
          lastDisplay = "";
        }
      }
    }
  }

  void reproducirPista(int numero) {
    Serial.write(0x7E);
    Serial.write(0xFF);
    Serial.write(0x06);
    Serial.write(0x03);
    Serial.write(0x00);
    Serial.write((numero >> 8) & 0xFF);
    Serial.write(numero & 0xFF);
    Serial.write(0xEF);
  }

  void promptName() {
    petName = "";
    resetMultitap();
    
    lcd.clear();
    lcd.print("Ingresa nombre:");
    lcd.setCursor(0, 1);
    
    while (true) {
      char k = keypad.getKey();
      
      unsigned long now = millis();
      if (currentKey != 0 && (now - lastKeyTime) >= MULTITAP_TIMEOUT) {
        petName += t9map[currentKey - '0'][currentIndex];
        lcd.setCursor(0, 1);
        lcd.print(petName);
        resetMultitap();
      }
      
      if (k == NO_KEY) continue;

      if (k == '#') {
        if (petName.length() > 0) {
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
        if (k >= '1' && k <= '8') {
          if (k == currentKey && (now - lastKeyTime) < MULTITAP_TIMEOUT) {
            int keyNum = k - '0';
            int letterCount = 0;
            for (int i = 0; i < 5; i++) {
              if (t9map[keyNum][i] != ' ') letterCount++;
              else break;
            }
            currentIndex = (currentIndex + 1) % letterCount;
          } else {
            if (currentKey != 0) {
              petName += t9map[currentKey - '0'][currentIndex];
            }
            currentKey = k;
            currentIndex = 0;
          }
          
          lastKeyTime = now;
          
          if (petName.length() < 15) {
            lcd.setCursor(0, 1);
            lcd.print(petName);
            lcd.print(t9map[k - '0'][currentIndex]);
          }
        } else if (k == '0') {
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
    
    lcdTitle("Guardando horarios...", "");
    delay(500);
    
    for (int i = 0; i < feedCount; i++) {
      int hh, mm;
      bool ok = promptTimeHHMM(hh, mm, i);
      
      if (ok) {
        feedHour[i] = hh;
        feedMinute[i] = mm;

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
        i--;
      }
    }

    // Guardar en EEPROM después de configurar todos los horarios
    saveConfig();
    lcdTitle("Horarios listos", "Config guardada");
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
        saveConfig(); // Guardar cambio
      } else if (opt != -1) {
        lcdTitle("Opcion invalida", nullptr);
        delay(800);
        lastDisplay = "";
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
        saveConfig(); // Guardar cambio
        return;
      } else {
        lcdTitle("Opcion invalida", nullptr);
        delay(800);
        lastDisplay = "";
      }
    }
  }

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
          if (buffer[2] == 0x3D) {
            break;
          }
        }
      }
    }
  }

  void doDispense(bool fromSchedule) {
    int duration = getDispenseDuration();
    unsigned long tiempoInicio;
    unsigned long duracionTotal = 60000;
    if (fromSchedule) digitalWrite(PIN_SENAL_SALIDA, HIGH);
    int estado = digitalRead(PIN_SENAL_ENTRADA);

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
        reproducirPista(1);
        delay(4000);
      }
    }
    if (fromSchedule) digitalWrite(PIN_SENAL_SALIDA, LOW);
    delay(1200);
  }

  void configMenu() {
    String lastDisplay = "";
    
    while (true) {
      String currentDisplay = "Ajustes: 1Nomb 2Tiem\n3Tam 4Horas #Salir";
      
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
        saveConfig(); // Guardar después de cambiar nombre
        showSummary(); 
        lastDisplay = "";
      }
      else if (k == '2') { 
        promptType(); 
        showSummary(); 
        lastDisplay = "";
      }
      else if (k == '3') { 
        promptSize(); 
        showSummary(); 
        lastDisplay = "";
      }
      else if (k == '4') { 
        promptFeeds(); 
        showSummary(); 
        lastDisplay = "";
      }
      else {
        lcdTitle("Opcion invalida", nullptr);
        delay(700);
        lastDisplay = "";
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
    servo.write(0);

    if (!rtc.begin()) {
      lcd.print("RTC no encontrado");
      while (1);
    }
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    lcdTitle("Dispensador listo", "Cargando config...");
    delay(800);

    // Intentar cargar configuración
    if (loadConfig()) {
      lcdTitle("Config cargada", "Listo para usar");
      delay(1200);
      showSummary();
      state = RUN;
    } else {
      lcdTitle("Primera vez", "Configurando...");
      delay(1200);
      state = WIZARD_NOMBRE;
    }
  }

  void loop() {
    // --- Reset de fábrica con 'B' ---
    char k = keypad.getKey();
    if (k == 'B') {
      lcdTitle("Reset de fabrica?", "# Confirmar * Cancelar");
      
      while (true) {
        char confirm = keypad.getKey();
        if (confirm == '#') {
          factoryReset();
          return;
        } else if (confirm == '*') {
          lcdTitle("Reset cancelado", nullptr);
          delay(1000);
          break;
        }
      }
      return;
    }

    // --- Apertura forzada con 'D' ---
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

    static unsigned long lastUpdate = 0;
    static unsigned long lastCycle = 0;
    static int currentDisplay = 0;
    static int lastMinute = -1;
    
    unsigned long ms = millis();
    
    if (ms - lastCycle >= 3000) {
      lastCycle = ms;
      if (feedCount > 0) {
        currentDisplay++;
        if (currentDisplay > feedCount) {
          currentDisplay = 0;
        }
      } else {
        currentDisplay = 0;
      }
    }
    
    bool shouldUpdate = false;
    
    if (ms - lastCycle < 100) { 
      shouldUpdate = true;
      if (currentDisplay == 0) {
        lastMinute = mm;
      }
    } else if (currentDisplay == 0 && mm != lastMinute) {
      shouldUpdate = true;
      lastMinute = mm;
    }
    
    if (shouldUpdate) {
      lcd.clear();
      
      lcd.setCursor(0,0);
      lcd.print(petName);
      
      lcd.setCursor(0,1);
      if (currentDisplay == 0) {
        lcd.print("Hora: ");
        if (hh < 10) lcd.print('0');
        lcd.print(hh);
        lcd.print(':');
        if (mm < 10) lcd.print('0');
        lcd.print(mm);
      } else {
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

    // Disparar si coincide alguna hora
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
