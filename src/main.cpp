#include <Arduino.h>
#include <Wire.h>
#include "lvgl.h"
#include "lvglDrivers.h"

#define MPU6050_ADDR 0x68
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 270
#define BALL_SIZE     20
#define CENTER_X      (SCREEN_WIDTH / 2 - BALL_SIZE / 2)
#define CENTER_Y      (SCREEN_HEIGHT / 2 - BALL_SIZE / 2)
#define MAX_COLLISIONS 3

// Variables globales
lv_obj_t *ball;
lv_obj_t *playBtn;
lv_obj_t *gameOverLabel;
lv_obj_t *lifeLabel;

bool gameStarted = false;
bool isGameOver = false;
int collisionCount = 0;

int ballX = CENTER_X;
int ballY = CENTER_Y;
int16_t accX = 0, accY = 0, accZ = 0;
int16_t gyroX = 0, gyroY = 0, gyroZ = 0;

// --- Fonctions UI ---

void updateLifeLabel() {
  if (lifeLabel) {
    char buf[16];
    snprintf(buf, sizeof(buf), "Vies : %d", MAX_COLLISIONS - collisionCount);
    lv_label_set_text(lifeLabel, buf);
  }
}

void returnToMenu(lv_timer_t *timer);

void gameOver() {
  gameStarted = false;
  isGameOver = true;
  lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);

  if (lifeLabel) {
    lv_obj_del(lifeLabel);
    lifeLabel = NULL;
  }

  gameOverLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(gameOverLabel, "GAME OVER");
  lv_obj_set_style_text_font(gameOverLabel, &lv_font_montserrat_14, 0);
  lv_obj_center(gameOverLabel);

  lv_timer_t *t = lv_timer_create(returnToMenu, 3000, NULL);
  lv_timer_set_repeat_count(t, 1);
}

void returnToMenu(lv_timer_t *timer) {
  if (gameOverLabel) {
    lv_obj_del(gameOverLabel);
    gameOverLabel = NULL;
  }

  ballX = CENTER_X;
  ballY = CENTER_Y;
  lv_obj_set_pos(ball, ballX, ballY);
  lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);

  collisionCount = 0;
  isGameOver = false;

  playBtn = lv_btn_create(lv_screen_active());
  lv_obj_center(playBtn);
  lv_obj_t *label = lv_label_create(playBtn);
  lv_label_set_text(label, "JOUER");
  lv_obj_center(label);
  lv_obj_add_event_cb(playBtn, [](lv_event_t *e) {
    lv_obj_del(playBtn);
    gameStarted = true;

    ballX = CENTER_X;
    ballY = CENTER_Y;
    collisionCount = 0;
    isGameOver = false;

    lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ball, ballX, ballY);

    // Créer le label des vies
    lifeLabel = lv_label_create(lv_screen_active());
    lv_obj_align(lifeLabel, LV_ALIGN_TOP_LEFT, 10, 5);
    updateLifeLabel();
  }, LV_EVENT_CLICKED, NULL);
}

void testLvgl() {
  ball = lv_obj_create(lv_screen_active());
  lv_obj_set_size(ball, BALL_SIZE, BALL_SIZE);
  lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ball, lv_color_hex(0xFF0000), 0);
  lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(ball, ballX, ballY);

  playBtn = lv_btn_create(lv_screen_active());
  lv_obj_center(playBtn);
  lv_obj_t *label = lv_label_create(playBtn);
  lv_label_set_text(label, "JOUER");
  lv_obj_center(label);
  lv_obj_add_event_cb(playBtn, [](lv_event_t *e) {
    lv_obj_del(playBtn);
    gameStarted = true;

    ballX = CENTER_X;
    ballY = CENTER_Y;
    collisionCount = 0;
    isGameOver = false;

    lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ball, ballX, ballY);

    // Créer le label des vies
    lifeLabel = lv_label_create(lv_screen_active());
    lv_obj_align(lifeLabel, LV_ALIGN_TOP_LEFT, 10, 5);
    updateLifeLabel();
  }, LV_EVENT_CLICKED, NULL);
}

// --- MPU6050 ---

void initMPU6050() {
  Wire.begin();
  Serial.println("I2C lancé");

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  byte error = Wire.endTransmission();
  if (error == 0) {
    Serial.println("MPU6050 réveillé");
  } else {
    Serial.print("Erreur init MPU6050: ");
    Serial.println(error);
  }
}

void readMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) {
    Serial.println("Erreur I2C read");
    return;
  }

  Wire.requestFrom(MPU6050_ADDR, 14);
  if (Wire.available() < 14) {
    Serial.println("Pas assez de données");
    return;
  }

  accX = Wire.read() << 8 | Wire.read();
  accY = Wire.read() << 8 | Wire.read();
  accZ = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read(); // température
  gyroX = Wire.read() << 8 | Wire.read();
  gyroY = Wire.read() << 8 | Wire.read();
  gyroZ = Wire.read() << 8 | Wire.read();
}

// --- SETUP / LOOP / TASK ---

void mySetup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Initialisation...");

  testLvgl();
  initMPU6050();

  xTaskCreate(myTask, "MPUtask", 4096, NULL, 1, NULL);
}

void loop() {
  // Vide
}

void myTask(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (1) {
    readMPU6050();

    if (gameStarted) {
      float factor = 0.005;
      ballX += accY * factor;
      ballY += accX * factor;

      if (ballX < 0 || ballX > SCREEN_WIDTH - BALL_SIZE ||
          ballY < 0 || ballY > SCREEN_HEIGHT - BALL_SIZE) {
        collisionCount++;
        Serial.print("⚠️ Bord touché ! Total : ");
        Serial.println(collisionCount);

        ballX = CENTER_X;
        ballY = CENTER_Y;

        updateLifeLabel();

        if (collisionCount >= MAX_COLLISIONS && !isGameOver) {
          gameOver();
        }
      }

      ballX = constrain(ballX, 0, SCREEN_WIDTH - BALL_SIZE);
      ballY = constrain(ballY, 0, SCREEN_HEIGHT - BALL_SIZE);
      lv_obj_set_pos(ball, ballX, ballY);
    }

    lv_timer_handler();
    delay(5);
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
  }
}
