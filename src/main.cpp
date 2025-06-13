#include <Arduino.h>
#include <Wire.h>
#include <math.h> // Ajout pour les calculs de vecteurs (sqrt)
#include "lvgl.h"
#include "lvglDrivers.h"

// --- Définitions (Constantes du jeu) ---
#define MPU6050_ADDR 0x68      // Adresse I2C du capteur MPU6050
#define SCREEN_WIDTH  480      // Largeur de l'écran en pixels
#define SCREEN_HEIGHT 270      // Hauteur de l'écran en pixels
#define BALL_SIZE     20       // Taille de la balle en pixels
#define CENTER_X      (SCREEN_WIDTH / 2 - BALL_SIZE / 2)   // Position X de départ de la balle (centre)
#define CENTER_Y      (SCREEN_HEIGHT / 2 - BALL_SIZE / 2)  // Position Y de départ de la balle (centre)
#define MAX_COLLISIONS 3       // Nombre de vies avant le "Game Over"

#define MAX_OBSTACLES 50       // Nombre maximum d'obstacles simultanés
#define OBSTACLE_SIZE 20       // Taille des obstacles en pixels
#define OBSTACLE_SPEED 1.5f    // Vitesse de déplacement des obstacles

// --- Structures de données ---
// Structure pour représenter un obstacle
typedef struct {
    lv_obj_t* obj;      // Pointeur vers l'objet graphique LVGL de l'obstacle
    float x_pos;        // Position X précise (avec décimales)
    float y_pos;        // Position Y précise (avec décimales)
    float dx;           // Vitesse de déplacement sur l'axe X
    float dy;           // Vitesse de déplacement sur l'axe Y
} Obstacle;


// --- Variables globales ---
// Objets graphiques LVGL
lv_obj_t *ball;
lv_obj_t *gameOverLabel;
lv_obj_t *lifeLabel;
lv_obj_t *scoreLabel;
lv_obj_t *scoreGameOverLabel;
Obstacle obstacles[MAX_OBSTACLES]; // Tableau pour stocker tous les obstacles

// Conteneurs pour les différents écrans du jeu
lv_obj_t *main_menu_container; // Conteneur pour le menu principal
lv_obj_t *color_menu_container;  // Conteneur pour le menu de sélection de couleur
lv_color_t ball_color;         // Variable pour stocker la couleur choisie pour la balle

// États du jeu
bool gameStarted = false;      // Vrai si la partie a commencé
bool isGameOver = false;       // Vrai si le joueur a perdu
int collisionCount = 0;        // Compteur de collisions (vies perdues)
int score = 0;                 // Score du joueur

// Coordonnées de la balle
int ballX = CENTER_X;
int ballY = CENTER_Y;
// Données brutes de l'accéléromètre
int16_t accX = 0, accY = 0;

// Timers (tâches répétitives) LVGL
lv_timer_t* obstacle_spawn_timer = NULL; // Timer pour faire apparaître les obstacles
lv_timer_t* score_timer = NULL;          // Timer pour augmenter le score
lv_timer_t* movement_timer = NULL;       // Timer pour la boucle principale du jeu

// Déclarations anticipées des fonctions (pour que le compilateur les connaisse)
void gameLoop(lv_timer_t *timer);
void createMainMenu();
void createColorMenu();


// --- Fonctions de gestion des obstacles ---

// Initialise le tableau d'obstacles en mettant tous les pointeurs à NULL
void initObstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].obj = NULL;
    }
}

// Supprime tous les obstacles de l'écran et du tableau
void clearObstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].obj != NULL) {
            lv_obj_del(obstacles[i].obj); // Supprime l'objet graphique
            obstacles[i].obj = NULL;      // Réinitialise le pointeur
        }
    }
}

// Crée un nouvel obstacle à un bord aléatoire de l'écran
void createObstacle(lv_timer_t *timer) {
    if (!gameStarted || isGameOver) return; // Ne fait rien si le jeu n'est pas en cours

    // Trouve un emplacement libre dans le tableau d'obstacles
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].obj == NULL) {
            // Crée l'objet graphique pour l'obstacle
            obstacles[i].obj = lv_obj_create(lv_screen_active());
            lv_obj_set_size(obstacles[i].obj, OBSTACLE_SIZE, OBSTACLE_SIZE);
            lv_obj_set_style_bg_color(obstacles[i].obj, lv_color_hex(0x0000FF), 0); // Obstacles en bleu
            lv_obj_clear_flag(obstacles[i].obj, LV_OBJ_FLAG_SCROLLABLE);

            // Détermine un côté d'apparition aléatoire (0:haut, 1:bas, 2:gauche, 3:droite)
            int side = random(0, 4);
            float x, y;
            switch (side) {
                case 0: x = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); y = -OBSTACLE_SIZE; obstacles[i].dx = 0; obstacles[i].dy = OBSTACLE_SPEED; break;
                case 1: x = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); y = SCREEN_HEIGHT; obstacles[i].dx = 0; obstacles[i].dy = -OBSTACLE_SPEED; break;
                case 2: x = -OBSTACLE_SIZE; y = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); obstacles[i].dx = OBSTACLE_SPEED; obstacles[i].dy = 0; break;
                case 3: x = SCREEN_WIDTH; y = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); obstacles[i].dx = -OBSTACLE_SPEED; obstacles[i].dy = 0; break;
            }
            // Positionne l'obstacle et stocke ses coordonnées et sa vitesse
            lv_obj_set_pos(obstacles[i].obj, (lv_coord_t)x, (lv_coord_t)y);
            obstacles[i].x_pos = x;
            obstacles[i].y_pos = y;
            return; // Sort de la fonction après avoir créé un obstacle
        }
    }
}

// --- Fonctions UI (Interface Utilisateur) et Jeu ---

// Met à jour le label affichant le nombre de vies restantes
void updateLifeLabel() {
    if (lifeLabel) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Vies : %d", MAX_COLLISIONS - collisionCount);
        lv_label_set_text(lifeLabel, buf);
    }
}

// Augmente le score à intervalle régulier
void incrementScore(lv_timer_t *timer) {
    if (gameStarted && !isGameOver) {
        score += 10;
        if (scoreLabel) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Score : %d", score);
            lv_label_set_text(scoreLabel, buf);
        }
    }
}

// Fonction appelée pour retourner au menu principal (après un Game Over)
void returnToMenu(lv_timer_t *timer) {
    // Supprime les labels de Game Over
    if (gameOverLabel) { lv_obj_del(gameOverLabel); gameOverLabel = NULL; }
    if (scoreGameOverLabel) { lv_obj_del(scoreGameOverLabel); scoreGameOverLabel = NULL; }

    clearObstacles();

    // Réinitialise la position de la balle et la cache
    ballX = CENTER_X;
    ballY = CENTER_Y;
    lv_obj_set_pos(ball, ballX, ballY);
    lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);

    // Réinitialise les états du jeu
    collisionCount = 0;
    score = 0;
    isGameOver = false;

    // Affiche le conteneur du menu principal
    lv_obj_clear_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN);
}

// Gère la fin de la partie
void gameOver() {
    gameStarted = false;
    isGameOver = true;
    lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);

    // Arrête tous les timers du jeu
    if (obstacle_spawn_timer) { lv_timer_del(obstacle_spawn_timer); obstacle_spawn_timer = NULL; }
    if (score_timer) { lv_timer_del(score_timer); score_timer = NULL; }
    if (movement_timer) { lv_timer_del(movement_timer); movement_timer = NULL; }

    // Supprime les labels de vie et de score
    if (lifeLabel) { lv_obj_del(lifeLabel); lifeLabel = NULL; }
    if (scoreLabel) { lv_obj_del(scoreLabel); scoreLabel = NULL; }

    clearObstacles();

    // Affiche "GAME OVER"
    gameOverLabel = lv_label_create(lv_screen_active());
    lv_label_set_text(gameOverLabel, "GAME OVER");
    lv_obj_set_style_text_font(gameOverLabel, &lv_font_montserrat_14, 0);
    lv_obj_center(gameOverLabel);

    // Affiche le score final
    scoreGameOverLabel = lv_label_create(lv_screen_active());
    char buf[32];
    snprintf(buf, sizeof(buf), "Score final : %d", score);
    lv_label_set_text(scoreGameOverLabel, buf);
    lv_obj_align_to(scoreGameOverLabel, gameOverLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // Crée un timer pour revenir au menu après 3 secondes
    lv_timer_t *t = lv_timer_create(returnToMenu, 3000, NULL);
    lv_timer_set_repeat_count(t, 1);
}

// Démarre une nouvelle partie
void startGame() {
    // Cache les deux conteneurs de menu
    lv_obj_add_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN);

    // Initialise les variables de jeu
    gameStarted = true;
    isGameOver = false;
    ballX = CENTER_X;
    ballY = CENTER_Y;
    collisionCount = 0;
    score = 0;

    // Applique la couleur choisie, affiche la balle et la positionne
    lv_obj_set_style_bg_color(ball, ball_color, 0);
    lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ball, ballX, ballY);

    clearObstacles();
    initObstacles();

    // Crée les labels de vie et de score pour la partie
    lifeLabel = lv_label_create(lv_screen_active());
    lv_obj_align(lifeLabel, LV_ALIGN_TOP_LEFT, 10, 5);
    updateLifeLabel();

    scoreLabel = lv_label_create(lv_screen_active());
    lv_obj_align(scoreLabel, LV_ALIGN_TOP_LEFT, 10, 25);
    lv_label_set_text(scoreLabel, "Score : 0");

    // Démarre les timers du jeu
    obstacle_spawn_timer = lv_timer_create(createObstacle, 2000, NULL);
    score_timer = lv_timer_create(incrementScore, 1000, NULL);
    movement_timer = lv_timer_create(gameLoop, 20, NULL);
}

// Fonction appelée lorsqu'une pastille de couleur est cliquée
void color_select_event_cb(lv_event_t * e) {
    lv_obj_t * swatch = (lv_obj_t *)lv_event_get_target(e); // Récupère l'objet cliqué
    ball_color = lv_obj_get_style_bg_color(swatch, 0);      // Récupère sa couleur
    lv_obj_set_style_bg_color(ball, ball_color, 0);         // Applique la couleur à la balle
}

// Construit les éléments du menu principal
void createMainMenu() {
    // Crée un conteneur qui occupe tout l'écran
    main_menu_container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(main_menu_container); // Enlève le style par défaut (fond, bordure)
    lv_obj_set_size(main_menu_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(main_menu_container);

    // Bouton "JOUER"
    lv_obj_t* playBtn = lv_btn_create(main_menu_container);
    lv_obj_align(playBtn, LV_ALIGN_CENTER, 0, -25);
    lv_obj_t *label = lv_label_create(playBtn);
    lv_label_set_text(label, "JOUER");
    lv_obj_center(label);
    lv_obj_add_event_cb(playBtn, [](lv_event_t *e) {
        startGame(); // Lance le jeu au clic
    }, LV_EVENT_CLICKED, NULL);

    // Bouton "Couleur"
    lv_obj_t* colorBtn = lv_btn_create(main_menu_container);
    lv_obj_align(colorBtn, LV_ALIGN_CENTER, 0, 25);
    lv_obj_t *colorLabel = lv_label_create(colorBtn);
    lv_label_set_text(colorLabel, "Couleur");
    lv_obj_center(colorLabel);
    lv_obj_add_event_cb(colorBtn, [](lv_event_t *e) {
        lv_obj_add_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu principal
        // Positionne la balle pour la prévisualisation dans le menu couleur
        ballX = CENTER_X;
        ballY = CENTER_Y;
        lv_obj_set_pos(ball, ballX, ballY);
        lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN);             // Affiche la balle
        lv_obj_clear_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Affiche le menu couleur
    }, LV_EVENT_CLICKED, NULL);
}

// Construit les éléments du menu de sélection de couleur
void createColorMenu() {
    color_menu_container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(color_menu_container);
    lv_obj_set_size(color_menu_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(color_menu_container);

    // Crée un panneau pour contenir les pastilles de couleur
    lv_obj_t* color_panel = lv_obj_create(color_menu_container);
    lv_obj_set_size(color_panel, 300, 50);
    lv_obj_align(color_panel, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_layout(color_panel, LV_LAYOUT_FLEX); // Utilise un layout flexible pour aligner les pastilles
    lv_obj_set_style_flex_flow(color_panel, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_style_flex_main_place(color_panel, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
    lv_obj_set_style_pad_all(color_panel, 5, 0);
    lv_obj_set_style_border_width(color_panel, 0, 0);
    lv_obj_set_style_bg_opa(color_panel, LV_OPA_TRANSP, 0); // Rend le panneau transparent

    // Tableau des couleurs proposées
    lv_color_t colors[] = {
        lv_color_hex(0xFF0000), lv_color_hex(0x00FF00), lv_color_hex(0x00BFFF),
        lv_color_hex(0xFFFF00), lv_color_hex(0xFF00FF), lv_color_hex(0xFFA500)
    };
    // Crée un bouton rond pour chaque couleur
    for (int i = 0; i < 6; i++) {
        lv_obj_t* swatch = lv_btn_create(color_panel);
        lv_obj_set_size(swatch, 40, 40);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(swatch, colors[i], 0);
        lv_obj_add_event_cb(swatch, color_select_event_cb, LV_EVENT_CLICKED, NULL);
    }

    // Bouton "Retour"
    lv_obj_t * backBtn = lv_btn_create(color_menu_container);
    lv_obj_align(backBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_t * backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, "Retour");
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(backBtn, [](lv_event_t *e) {
        lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu couleur
        
        // Réinitialise la position de la balle à son point de départ avant de la cacher
        lv_obj_set_pos(ball, CENTER_X, CENTER_Y);

        lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN);                 // Cache la balle
        lv_obj_clear_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Affiche le menu principal
    }, LV_EVENT_CLICKED, NULL);

    // Au départ, ce menu est caché
    lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN);
}

// Fonction d'initialisation de l'interface graphique
void testLvgl() {
    ball_color = lv_color_hex(0xFF0000); // Couleur par défaut de la balle (rouge)

    // Crée l'objet balle une seule fois au démarrage
    ball = lv_obj_create(lv_screen_active());
    lv_obj_set_size(ball, BALL_SIZE, BALL_SIZE);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0); // La rend ronde
    lv_obj_set_style_bg_color(ball, ball_color, 0);
    lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // La balle est cachée au début
    lv_obj_set_pos(ball, CENTER_X, CENTER_Y);

    initObstacles();

    // Crée les deux menus (qui sont cachés ou affichés au besoin)
    createColorMenu();
    createMainMenu();
}

// --- Fonctions liées au matériel (MPU6050) ---

// Initialise la communication avec le MPU6050
void initMPU6050() {
    Wire.begin();
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x6B); // Registre de gestion de l'alimentation
    Wire.write(0);    // Sort le capteur du mode veille
    Wire.endTransmission(true);
}

// Lit les valeurs de l'accéléromètre
void readMPU6050() {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x3B); // Premier registre des données de l'accéléromètre
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)6, true);
    if (Wire.available() >= 6) {
      // Lit et combine les octets pour chaque axe
      accX = (Wire.read() << 8) | Wire.read();
      accY = (Wire.read() << 8) | Wire.read();
      Wire.read(); Wire.read(); // Ignore les données du gyroscope pour ce projet
    }
}

// --- Fonctions principales Arduino ---

// Fonction d'initialisation, exécutée une seule fois
void mySetup() {
    Serial.begin(115200);
    testLvgl();     // Initialise l'interface graphique
    initMPU6050();  // Initialise le capteur
}

// Boucle principale du microcontrôleur
void loop() {
    lv_timer_handler(); // Gère les tâches LVGL (timers, animations, etc.)
    delay(5);           // Petite pause pour laisser le temps à d'autres processus
}

// Tâche inutilisée dans ce code, peut être supprimée
void myTask(void *pvParameters) {
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Boucle principale du jeu, appelée par un timer LVGL
void gameLoop(lv_timer_t *timer) {
    if (!gameStarted || isGameOver) return; // Ne s'exécute pas si le jeu n'est pas en cours

    readMPU6050(); // Lit les nouvelles valeurs du capteur

    // Met à jour la position de la balle en fonction de l'accélération
    float factor = 0.0006; // Facteur pour ajuster la sensibilité du mouvement
    ballX += accY * factor;
    ballY += accX * factor;

    // Vérifie les collisions avec les bords de l'écran
    if (ballX <= 0 || ballX >= SCREEN_WIDTH - BALL_SIZE ||
        ballY <= 0 || ballY >= SCREEN_HEIGHT - BALL_SIZE)
    {
        collisionCount++;
        updateLifeLabel();
        clearObstacles();

        if (collisionCount >= MAX_COLLISIONS) {
            gameOver();
        } else {
            // Replace la balle au centre si le joueur a encore des vies
            ballX = CENTER_X;
            ballY = CENTER_Y;
            lv_obj_set_pos(ball, ballX, ballY);
        }
        return; // Sort de la fonction pour cette frame
    }

    lv_obj_set_pos(ball, ballX, ballY); // Applique la nouvelle position à l'objet balle

    // Récupère la zone (coordonnées) de la balle pour la détection de collision
    lv_area_t ballArea;
    lv_obj_get_coords(ball, &ballArea);

    // Boucle pour vérifier les collisions avec chaque obstacle
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].obj != NULL) {
            // Met à jour la position de l'obstacle
            obstacles[i].x_pos += obstacles[i].dx;
            obstacles[i].y_pos += obstacles[i].dy;

            // Fait rebondir l'obstacle sur les bords de l'écran (non implémenté ici)
            if ((obstacles[i].x_pos <= 0 && obstacles[i].dx < 0) || (obstacles[i].x_pos >= SCREEN_WIDTH - OBSTACLE_SIZE && obstacles[i].dx > 0)) {
                obstacles[i].dx *= -1;
            }
            if ((obstacles[i].y_pos <= 0 && obstacles[i].dy < 0) || (obstacles[i].y_pos >= SCREEN_HEIGHT - OBSTACLE_SIZE && obstacles[i].dy > 0)) {
                obstacles[i].dy *= -1;
            }

            lv_obj_set_pos(obstacles[i].obj, (lv_coord_t)obstacles[i].x_pos, (lv_coord_t)obstacles[i].y_pos);

            // Récupère la zone de l'obstacle
            lv_area_t obsArea;
            lv_obj_get_coords(obstacles[i].obj, &obsArea);

            // Détection de collision (boîte englobante)
            bool collision_x = ballArea.x2 >= obsArea.x1 && obsArea.x2 >= ballArea.x1;
            bool collision_y = ballArea.y2 >= obsArea.y1 && obsArea.y2 >= ballArea.y1;
            if (collision_x && collision_y) {
                collisionCount++;
                updateLifeLabel();
                clearObstacles();

                if (collisionCount >= MAX_COLLISIONS) {
                    gameOver();
                } else {
                    ballX = CENTER_X;
                    ballY = CENTER_Y;
                    lv_obj_set_pos(ball, ballX, ballY);
                }
                return;
            }
        }
    }
}
