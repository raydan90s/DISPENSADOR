void setup() {
  Serial.begin(9600);
  delay(1000);

  Serial.write(0x7E);
  Serial.write(0xFF);
  Serial.write(0x06);
  Serial.write(0x03);
  Serial.write(0x00);
  Serial.write(0x00);
  Serial.write(0x01);
  Serial.write(0xEF);
}

void loop() {
  // Nada aquí
}
