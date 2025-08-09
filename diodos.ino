  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>

  LiquidCrystal_I2C lcd(0x27, 16, 2);

  const int SENSOR_PIN = 12;

  void setup() {
    pinMode(SENSOR_PIN, INPUT_PULLUP); // Activar pull-up interna
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("Esperando...");
  }

  void loop() {
    int estadoSensor = digitalRead(SENSOR_PIN);

    lcd.setCursor(0, 0);
    if (estadoSensor == LOW) { // LOW = activado porque pull-up invierte la lógica
      lcd.print("Poca comida   ");
    } else {
      lcd.print("Comida OK     ");
    }

    delay(500);
  }
