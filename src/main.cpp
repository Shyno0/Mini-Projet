#include <Arduino.h>
#include <Wire.h>

#define MPU6050_ADDR 0x68

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Initialisation...");

  Wire.begin();
  Serial.println("I2C lancé");

  // Initialiser MPU6050 (wake up)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);  // registre power management
  Wire.write(0);     // sortir du mode sleep
  byte error = Wire.endTransmission();
  if (error == 0) {
    Serial.println("MPU6050 réveillé");
  } else {
    Serial.print("Erreur init MPU6050: ");
    Serial.println(error);
  }
}

void loop() {
  // Demander lecture à partir du registre 0x3B (accélération X)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) { // restart condition
    Serial.println("Erreur I2C read");
    delay(1000);
    return;
  }

  // Lire 14 octets: AccX(2), AccY(2), AccZ(2), Temp(2), GyroX(2), GyroY(2), GyroZ(2)
  Wire.requestFrom(MPU6050_ADDR, 14);
  if (Wire.available() < 14) {
    Serial.println("Pas assez de données");
    delay(1000);
    return;
  }

  int16_t accX = Wire.read() << 8 | Wire.read();
  int16_t accY = Wire.read() << 8 | Wire.read();
  int16_t accZ = Wire.read() << 8 | Wire.read();
  /*int16_t gyroX = Wire.read() << 8 | Wire.read();
  int16_t gyroY = Wire.read() << 8 | Wire.read();
  int16_t gyroZ = Wire.read() << 8 | Wire.read();*/

  Serial.print(" | Acc X:   "); Serial.print(accX);
  Serial.print(" | Acc Y:  "); Serial.print(accY);
  Serial.print(" | Acc Z:   "); Serial.println(accZ);

  /*Serial.print(" | Gyro X: "); Serial.print(gyroX);
  Serial.print(" | Gyro Y: "); Serial.print(gyroY);
  Serial.print(" | Gyro Z: "); Serial.println(gyroZ);*/

  delay(1000);
}
