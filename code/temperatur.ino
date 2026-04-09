const int sensorPin = A0;

unsigned long lastTime = 0;
const unsigned long interval = 10000; // 10 Sekunden

void setup() {
  Serial.begin(9600);
  
  // CSV Header
  Serial.println("Zeit_s,Temperatur_C");
}

void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastTime >= interval) {
    lastTime = currentTime;

    // Zeit in Sekunden
    float time_s = currentTime / 1000.0;

    // Mehrfach messen für weniger Rauschen
    int samples = 10;
    float sum = 0;

    for (int i = 0; i < samples; i++) {
      sum += analogRead(sensorPin);
      delay(5);
    }

    float adcValue = sum / samples;

    // Spannung berechnen (5V Arduino)
    float voltage = adcValue * (5.0 / 1023.0);

    // Temperatur berechnen
    float temperatureC = (voltage - 0.5) * 100.0;

    // CSV Ausgabe
    Serial.print(time_s, 0);
    Serial.print(",");
    Serial.println(temperatureC, 2);
  }
}