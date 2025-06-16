#include <Arduino.h>
#include <Wire.h>
#include <math.h> 
#include "lvgl.h"
#include "lvglDrivers.h"

// --- Définitions (Constantes du jeu) ---
#define MPU6050_ADDR 0x68       // Adresse I2C du capteur MPU6050 (accéléromètre/gyroscope)
#define SCREEN_WIDTH 480        // Largeur de l'écran en pixels
#define SCREEN_HEIGHT 270       // Hauteur de l'écran en pixels
#define BALL_SIZE 20            // Taille de la balle en pixels
#define CENTER_X (SCREEN_WIDTH / 2 - BALL_SIZE / 2)   // Position X de départ de la balle (centre)
#define CENTER_Y (SCREEN_HEIGHT / 2 - BALL_SIZE / 2)  // Position Y de départ de la balle (centre)
#define MAX_COLLISIONS 3        // Nombre de vies avant le "Game Over" (collisions avec les bords ou obstacles bleus)

#define MAX_OBSTACLES 50        // Nombre maximum d'obstacles bleus simultanés
#define OBSTACLE_SIZE 20        // Taille des obstacles bleus et du cube vert en pixels
#define OBSTACLE_SPEED 1.5f     // Vitesse de déplacement des obstacles bleus
// --- Définitions (Constantes du jeu) ---

// --- Structures de données ---
// Structure pour représenter un obstacle bleu
typedef struct {
    lv_obj_t* obj;          // Pointeur vers l'objet graphique LVGL de l'obstacle
    float x_pos;            // Position X précise (avec décimales) de l'obstacle
    float y_pos;            // Position Y précise (avec décimales) de l'obstacle
    float dx;               // Vitesse de déplacement sur l'axe X
    float dy;               // Vitesse de déplacement sur l'axe Y
} Obstacle;
// --- Structures de données ---

// --- Variables globales ---
// Objets graphiques LVGL
lv_obj_t *ball;             // La balle contrôlée par le joueur
lv_obj_t *gameOverLabel;    // Label affichant "GAME OVER"
lv_obj_t *lifeLabel;        // Label affichant le nombre de vies restantes
lv_obj_t *scoreLabel;       // Label affichant le score actuel
lv_obj_t *scoreGameOverLabel; // Label affichant le score final après Game Over
Obstacle obstacles[MAX_OBSTACLES]; // Tableau pour stocker tous les obstacles bleus

lv_obj_t *greenCube = NULL; // Le cube vert qui donne des points (initialisé à NULL)

// Conteneurs pour les différents écrans du jeu
lv_obj_t *main_menu_container;  // Conteneur pour le menu principal
lv_obj_t *color_menu_container; // Conteneur pour le menu de sélection de couleur de la balle
lv_color_t ball_color;          // Variable pour stocker la couleur choisie pour la balle

// États et données du jeu
bool gameStarted = false;       // Vrai si la partie a commencé
bool isGameOver = false;        // Vrai si le joueur a perdu
int collisionCount = 0;         // Compteur de collisions (vies perdues)
int score = 0;                  // Score du joueur

// Coordonnées de la balle
int ballX = CENTER_X; // Position X actuelle de la balle
int ballY = CENTER_Y; // Position Y actuelle de la balle
// Données brutes de l'accéléromètre MPU6050
int16_t accX = 0, accY = 0;

// Timers (tâches répétitives) LVGL
lv_timer_t* obstacle_spawn_timer = NULL; // Timer pour faire apparaître les obstacles bleus
lv_timer_t* score_timer = NULL;          // Timer pour augmenter le score passivement
lv_timer_t* movement_timer = NULL;       // Timer pour la boucle principale du jeu (mouvement et collisions)
lv_timer_t* green_cube_spawn_timer = NULL; // Timer pour l'apparition du cube vert

// Déclarations anticipées des fonctions (pour que le compilateur les connaisse avant leur définition)
void gameLoop(lv_timer_t *timer);
void createMainMenu();
void createColorMenu();
void initGreenCubeObject();
void spawnGreenCube(lv_timer_t *timer);

/**
 * @brief Fonction utilitaire pour contraindre une valeur entre un minimum et un maximum.
 * @param val La valeur à contraindre.
 * @param low La limite inférieure.
 * @param high La limite supérieure.
 * @return La valeur contrainte.
 */
template <typename T>
T clamp(T val, T low, T high) { // Renommé de 'constrain' à 'clamp'
    if (val < low) return low;
    if (val > high) return high;
    return val;
}

// --- Fonctions utilitaires LVGL ---
/**
 * @brief Crée un objet LVGL de base (rectangle ou cercle) avec une couleur donnée.
 * * @param parent L'objet parent LVGL (ex: lv_screen_active())
 * @param width Largeur de l'objet
 * @param height Hauteur de l'objet
 * @param color Couleur de fond de l'objet
 * @param isCircle Si vrai, l'objet sera rond (radius au maximum)
 * @return Pointeur vers l'objet LVGL créé
 */
lv_obj_t* createBasicLvObject(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, lv_color_t color, bool isCircle) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE); // Désactive le défilement
    if (isCircle) {
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0); // Rend l'objet rond
    }
    return obj;
}

// --- Fonctions de gestion des obstacles bleus ---
/**
 * @brief Initialise le tableau des obstacles en mettant tous les pointeurs à NULL.
 * Cela indique qu'aucun obstacle n'est présent initialement.
 */
void initObstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        obstacles[i].obj = NULL;
    }
}

/**
 * @brief Supprime tous les obstacles bleus actuellement à l'écran et réinitialise leurs pointeurs.
 */
void clearObstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].obj != NULL) {
            lv_obj_del(obstacles[i].obj); // Supprime l'objet graphique de LVGL
            obstacles[i].obj = NULL;       // Réinitialise le pointeur dans le tableau
        }
    }
}

/**
 * @brief Crée un nouvel obstacle bleu et le positionne aléatoirement sur un des bords de l'écran.
 * Les obstacles ont une vitesse et une direction définies pour se déplacer vers l'intérieur de l'écran.
 * @param timer Pointeur vers le timer LVGL qui a déclenché cette fonction (non utilisé directement ici)
 */
void createObstacle(lv_timer_t *timer) {
    // Ne fait rien si le jeu n'a pas commencé ou est terminé
    if (!gameStarted || isGameOver) return;

    // Cherche un emplacement libre dans le tableau d'obstacles
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].obj == NULL) {
            // Crée l'objet graphique pour l'obstacle (cube bleu)
            obstacles[i].obj = createBasicLvObject(lv_screen_active(), OBSTACLE_SIZE, OBSTACLE_SIZE, lv_color_hex(0x0000FF), false);
            
            // Détermine un côté d'apparition aléatoire (0:haut, 1:bas, 2:gauche, 3:droite)
            int side = random(0, 4);
            float x, y;
            switch (side) {
                // Apparition par le haut, se déplace vers le bas
                case 0: x = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); y = -OBSTACLE_SIZE; obstacles[i].dx = 0; obstacles[i].dy = OBSTACLE_SPEED; break;
                // Apparition par le bas, se déplace vers le haut
                case 1: x = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); y = SCREEN_HEIGHT; obstacles[i].dx = 0; obstacles[i].dy = -OBSTACLE_SPEED; break;
                // Apparition par la gauche, se déplace vers la droite
                case 2: x = -OBSTACLE_SIZE; y = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); obstacles[i].dx = OBSTACLE_SPEED; obstacles[i].dy = 0; break;
                // Apparition par la droite, se déplace vers la gauche
                case 3: x = SCREEN_WIDTH; y = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); obstacles[i].dx = -OBSTACLE_SPEED; obstacles[i].dy = 0; break;
            }
            // Positionne l'obstacle et stocke ses coordonnées précises et sa vitesse
            lv_obj_set_pos(obstacles[i].obj, (lv_coord_t)x, (lv_coord_t)y);
            obstacles[i].x_pos = x;
            obstacles[i].y_pos = y;
            return; // Sort de la fonction après avoir créé un obstacle
        }
    }
}

// --- Fonctions UI (Interface Utilisateur) et Jeu ---
/**
 * @brief Met à jour le texte du label affichant le nombre de vies restantes.
 */
void updateLifeLabel() {
    if (lifeLabel) { // Vérifie que le label existe
        char buf[16]; // Buffer pour le texte
        snprintf(buf, sizeof(buf), "Vies : %d", MAX_COLLISIONS - collisionCount); // Formate le texte
        lv_label_set_text(lifeLabel, buf); // Met à jour le label
    }
}

/**
 * @brief Augmente le score du joueur et met à jour le label du score.
 * Cette fonction est appelée par un timer régulier.
 * @param timer Pointeur vers le timer LVGL qui a déclenché cette fonction (non utilisé directement ici)
 */
void incrementScore(lv_timer_t *timer) {
    if (gameStarted && !isGameOver) { // N'incrémente le score que si le jeu est en cours
        score += 10; // Ajoute 10 points
        if (scoreLabel) { // Vérifie que le label existe
            char buf[32];
            snprintf(buf, sizeof(buf), "Score : %d", score);
            lv_label_set_text(scoreLabel, buf);
        }
    }
}

/**
 * @brief Fonction appelée pour réinitialiser le jeu et retourner au menu principal après un "Game Over".
 * @param timer Pointeur vers le timer LVGL qui a déclenché cette fonction (non utilisé directement ici)
 */
void returnToMenu(lv_timer_t *timer) {
    // Supprime les labels de "Game Over" et de score final s'ils existent
    if (gameOverLabel) { lv_obj_del(gameOverLabel); gameOverLabel = NULL; }
    if (scoreGameOverLabel) { lv_obj_del(scoreGameOverLabel); scoreGameOverLabel = NULL; }

    clearObstacles(); // Supprime tous les obstacles bleus

    // Réinitialise la position de la balle au centre et la cache
    ballX = CENTER_X;
    ballY = CENTER_Y;
    if (ball) {
        lv_obj_set_pos(ball, ballX, ballY);
        lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // Cache la balle
    }

    // Cache le cube vert s'il est visible et arrête son timer de réapparition
    if (greenCube) { lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); }
    if (green_cube_spawn_timer) {
        lv_timer_del(green_cube_spawn_timer);
        green_cube_spawn_timer = NULL;
    }

    // Réinitialise les variables d'état du jeu
    collisionCount = 0;
    score = 0;
    isGameOver = false;
    gameStarted = false;

    // Affiche le conteneur du menu principal
    if (main_menu_container) {
        lv_obj_clear_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Gère la fin de la partie (Game Over).
 * Arrête les timers, masque la balle, supprime les labels du jeu et affiche les messages de fin de partie.
 */
void gameOver() {
    gameStarted = false; // Le jeu est terminé
    isGameOver = true;   // Indique l'état "Game Over"
    if (ball) {
        lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // Cache la balle
    }

    // Arrête et supprime tous les timers du jeu pour éviter qu'ils ne continuent à s'exécuter
    if (obstacle_spawn_timer) { lv_timer_del(obstacle_spawn_timer); obstacle_spawn_timer = NULL; }
    if (score_timer) { lv_timer_del(score_timer); score_timer = NULL; }
    if (movement_timer) { lv_timer_del(movement_timer); movement_timer = NULL; }
    if (green_cube_spawn_timer) { lv_timer_del(green_cube_spawn_timer); green_cube_spawn_timer = NULL; }

    // Supprime les labels de vie et de score du jeu s'ils existent
    if (lifeLabel) { lv_obj_del(lifeLabel); lifeLabel = NULL; }
    if (scoreLabel) { lv_obj_del(scoreLabel); scoreLabel = NULL; }

    clearObstacles(); // Supprime tous les obstacles bleus restants

    // Cache le cube vert s'il est visible
    if (greenCube) { lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); }

    // Affiche le label "GAME OVER"
    gameOverLabel = lv_label_create(lv_screen_active());
    lv_label_set_text(gameOverLabel, "GAME OVER");
    lv_obj_set_style_text_font(gameOverLabel, &lv_font_montserrat_14, 0); // Définit la police
    lv_obj_center(gameOverLabel); // Centre le label à l'écran

    // Affiche le label du score final
    scoreGameOverLabel = lv_label_create(lv_screen_active());
    char buf[32];
    snprintf(buf, sizeof(buf), "Score final : %d", score);
    lv_label_set_text(scoreGameOverLabel, buf);
    lv_obj_align_to(scoreGameOverLabel, gameOverLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10); // Aligne sous le label Game Over

    // Crée un timer à usage unique pour revenir au menu principal après 3 secondes
    lv_timer_t *t = lv_timer_create(returnToMenu, 3000, NULL);
    lv_timer_set_repeat_count(t, 1);
}

/**
 * @brief Démarre une nouvelle partie du jeu.
 * Cache les menus, initialise les variables de jeu, crée les labels in-game et démarre les timers du jeu.
 */
void startGame() {
    // Cache les conteneurs du menu principal et du menu de sélection de couleur
    if (main_menu_container) lv_obj_add_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN);
    if (color_menu_container) lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN);

    // Initialise les variables d'état du jeu pour une nouvelle partie
    gameStarted = true;
    isGameOver = false;
    ballX = CENTER_X;
    ballY = CENTER_Y;
    collisionCount = 0;
    score = 0;

    // Applique la couleur sélectionnée à la balle, la rend visible et la positionne au centre
    if (ball) {
        lv_obj_set_style_bg_color(ball, ball_color, 0);
        lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ball, ballX, ballY);
    }

    clearObstacles(); // S'assure qu'aucun ancien obstacle ne persiste
    initObstacles();  // Réinitialise le tableau d'obstacles

    // Crée et positionne le label de vie
    lifeLabel = lv_label_create(lv_screen_active());
    lv_obj_align(lifeLabel, LV_ALIGN_TOP_LEFT, 10, 5);
    updateLifeLabel(); // Met à jour le texte initial des vies

    // Crée et positionne le label de score
    scoreLabel = lv_label_create(lv_screen_active());
    lv_obj_align(scoreLabel, LV_ALIGN_TOP_LEFT, 10, 25);
    lv_label_set_text(scoreLabel, "Score : 0"); // Score initial

    // Démarre les timers principaux du jeu
    obstacle_spawn_timer = lv_timer_create(createObstacle, 2000, NULL); // Les obstacles apparaissent toutes les 2 secondes
    score_timer = lv_timer_create(incrementScore, 1000, NULL);          // Le score augmente toutes les secondes
    movement_timer = lv_timer_create(gameLoop, 20, NULL);                // La boucle de jeu s'exécute toutes les 20 ms (50 FPS)

    // Gère l'apparition du cube vert :
    spawnGreenCube(NULL); // Fait apparaître le premier cube vert immédiatement
    // Annule tout timer de spawn de cube vert précédent s'il existait
    if (green_cube_spawn_timer) {
        lv_timer_del(green_cube_spawn_timer);
        green_cube_spawn_timer = NULL;
    }
    // Programme le timer pour l'apparition du prochain cube vert après 5 secondes
    green_cube_spawn_timer = lv_timer_create(spawnGreenCube, 5000, NULL);
    lv_timer_set_repeat_count(green_cube_spawn_timer, 1); // C'est un timer à usage unique
}

/**
 * @brief Callback appelé lorsque l'utilisateur clique sur une pastille de couleur dans le menu de sélection.
 * Met à jour la couleur de la balle.
 * @param e Événement LVGL (contient des informations sur l'objet cliqué)
 */
void color_select_event_cb(lv_event_t * e) {
    lv_obj_t * swatch = (lv_obj_t *)lv_event_get_target(e); // Récupère l'objet (pastille de couleur) qui a été cliqué
    ball_color = lv_obj_get_style_bg_color(swatch, 0);       // Récupère la couleur de cette pastille
    if (ball) { // Vérifie que la balle existe
        lv_obj_set_style_bg_color(ball, ball_color, 0);      // Applique la couleur sélectionnée à la balle
    }
}

/**
 * @brief Crée les éléments de l'interface utilisateur du menu principal.
 * Contient les boutons "JOUER" et "Couleur".
 */
void createMainMenu() {
    // Crée un conteneur qui occupe tout l'écran pour le menu principal
    main_menu_container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(main_menu_container); // Supprime le style par défaut (arrière-plan, bordure)
    lv_obj_set_size(main_menu_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(main_menu_container); // Centre le conteneur sur l'écran

    // Crée le bouton "JOUER"
    lv_obj_t* playBtn = lv_btn_create(main_menu_container);
    lv_obj_align(playBtn, LV_ALIGN_CENTER, 0, -25); // Positionne le bouton légèrement au-dessus du centre
    lv_obj_t *label = lv_label_create(playBtn);
    lv_label_set_text(label, "JOUER"); // Texte du bouton
    lv_obj_center(label); // Centre le texte dans le bouton
    // Ajoute un callback pour gérer le clic sur le bouton "JOUER"
    lv_obj_add_event_cb(playBtn, [](lv_event_t *e) {
        startGame(); // Lance la fonction de démarrage du jeu
    }, LV_EVENT_CLICKED, NULL);

    // Crée le bouton "Couleur"
    lv_obj_t* colorBtn = lv_btn_create(main_menu_container);
    lv_obj_align(colorBtn, LV_ALIGN_CENTER, 0, 25); // Positionne le bouton légèrement en dessous du centre
    lv_obj_t *colorLabel = lv_label_create(colorBtn);
    lv_label_set_text(colorLabel, "Couleur"); // Texte du bouton
    lv_obj_center(colorLabel); // Centre le texte dans le bouton
    // Ajoute un callback pour gérer le clic sur le bouton "Couleur"
    lv_obj_add_event_cb(colorBtn, [](lv_event_t *e) {
        if (main_menu_container) lv_obj_add_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu principal
        // Positionne la balle au centre et la rend visible pour la prévisualisation dans le menu couleur
        if (ball) {
            ballX = CENTER_X;
            ballY = CENTER_Y;
            lv_obj_set_pos(ball, ballX, ballY);
            lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN);
        }
        if (color_menu_container) lv_obj_clear_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Affiche le menu de sélection de couleur
    }, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief Crée les éléments de l'interface utilisateur du menu de sélection de couleur.
 * Permet de choisir la couleur de la balle via des pastilles colorées.
 */
void createColorMenu() {
    // Crée un conteneur pour le menu de sélection de couleur
    color_menu_container = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(color_menu_container); // Supprime le style par défaut
    lv_obj_set_size(color_menu_container, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(color_menu_container); // Centre le conteneur

    // Crée un panneau pour contenir les pastilles de couleur
    lv_obj_t* color_panel = lv_obj_create(color_menu_container);
    lv_obj_set_size(color_panel, 300, 50);
    lv_obj_align(color_panel, LV_ALIGN_CENTER, 0, 30); // Positionne le panneau au centre
    lv_obj_set_layout(color_panel, LV_LAYOUT_FLEX); // Utilise un layout flexible (alignement automatique)
    lv_obj_set_style_flex_flow(color_panel, LV_FLEX_FLOW_ROW, 0); // Les éléments s'alignent en ligne
    lv_obj_set_style_flex_main_place(color_panel, LV_FLEX_ALIGN_SPACE_EVENLY, 0); // Espacement égal entre les éléments
    lv_obj_set_style_pad_all(color_panel, 5, 0); // Padding interne
    lv_obj_set_style_border_width(color_panel, 0, 0); // Pas de bordure
    lv_obj_set_style_bg_opa(color_panel, LV_OPA_TRANSP, 0); // Rend le panneau transparent

    // Tableau des couleurs prédéfinies pour les pastilles
    lv_color_t colors[] = {
        lv_color_hex(0xFF0000), lv_color_hex(0x00FF00), lv_color_hex(0x00BFFF),
        lv_color_hex(0xFFFF00), lv_color_hex(0xFF00FF), lv_color_hex(0xFFA500)
    };
    // Crée un bouton rond (pastille) pour chaque couleur
    for (int i = 0; i < 6; i++) {
        lv_obj_t* swatch = lv_btn_create(color_panel);
        lv_obj_set_size(swatch, 40, 40);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0); // Rend le bouton rond
        lv_obj_set_style_bg_color(swatch, colors[i], 0); // Définit la couleur de fond
        lv_obj_add_event_cb(swatch, color_select_event_cb, LV_EVENT_CLICKED, NULL); // Attache le callback de sélection de couleur
    }

    // Crée le bouton "Retour"
    lv_obj_t * backBtn = lv_btn_create(color_menu_container);
    lv_obj_align(backBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10); // Positionne le bouton en bas à gauche
    lv_obj_t * backLabel = lv_label_create(backBtn);
    lv_label_set_text(backLabel, "Retour"); // Texte du bouton
    lv_obj_center(backLabel); // Centre le texte
    // Ajoute un callback pour gérer le clic sur le bouton "Retour"
    lv_obj_add_event_cb(backBtn, [](lv_event_t *e) {
        if (color_menu_container) lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN);
        
        // Réinitialise la position de la balle au centre avant de la cacher (pour le jeu)
        if (ball) {
            lv_obj_set_pos(ball, CENTER_X, CENTER_Y);
            lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // Cache la balle après la prévisualisation
        }
        if (main_menu_container) lv_obj_clear_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Affiche le menu principal
    }, LV_EVENT_CLICKED, NULL);

    // Par défaut, ce menu est caché au démarrage
    lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Initialise l'objet LVGL représentant le cube vert.
 * Cette fonction est appelée une seule fois au démarrage de l'application.
 */
void initGreenCubeObject() {
    // Crée l'objet graphique pour le cube vert (taille OBSTACLE_SIZE, couleur verte, non-rond)
    greenCube = createBasicLvObject(lv_screen_active(), OBSTACLE_SIZE, OBSTACLE_SIZE, lv_color_hex(0x00FF00), false);
    lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); // Le cube est caché par défaut
}

/**
 * @brief Fait apparaître le cube vert à une position aléatoire sur l'écran.
 * @param timer Pointeur vers le timer LVGL qui a déclenché cette fonction (non utilisé directement ici)
 */
void spawnGreenCube(lv_timer_t *timer) {
    // Ne fait rien si le jeu n'est pas en cours ou est terminé
    if (!gameStarted || isGameOver) return;

    // S'assure que l'objet greenCube a été initialisé
    if (greenCube == NULL) return; 

    // Génère des coordonnées aléatoires pour le cube vert, en s'assurant qu'il reste dans les limites de l'écran
    int greenCubeX = random(0, SCREEN_WIDTH - OBSTACLE_SIZE);
    int greenCubeY = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE);
    
    lv_obj_set_pos(greenCube, greenCubeX, greenCubeY); // Positionne le cube
    lv_obj_clear_flag(greenCube, LV_OBJ_FLAG_HIDDEN); // Rend le cube vert visible

    // Réinitialise le pointeur du timer de spawn. Puisqu'il est à usage unique, LVGL le supprime après exécution.
    // Le remettre à NULL permet de savoir qu'aucun nouveau timer n'est programmé.
    green_cube_spawn_timer = NULL; 
}

/**
 * @brief Fonction d'initialisation de toute l'interface graphique du jeu.
 * Appelle la création de la balle, des obstacles et des menus.
 */
void testLvgl() {
    ball_color = lv_color_hex(0xFF0000); // Définit la couleur par défaut de la balle (rouge)

    // Crée l'objet balle
    ball = createBasicLvObject(lv_screen_active(), BALL_SIZE, BALL_SIZE, ball_color, true);
    lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // La balle est cachée au démarrage (elle apparaît avec le jeu)
    lv_obj_set_pos(ball, CENTER_X, CENTER_Y); // Positionne la balle au centre

    initObstacles();        // Initialise le tableau d'obstacles
    initGreenCubeObject(); // Initialise l'objet cube vert

    createColorMenu();      // Crée le menu de sélection de couleur
    createMainMenu();       // Crée le menu principal
}

// --- Fonctions liées au matériel (MPU6050) ---
/**
 * @brief Initialise le capteur MPU6050 via la communication I2C.
 * Envoie une commande pour sortir le capteur du mode veille.
 */
void initMPU6050() {
    Wire.begin(); // Initialise la bibliothèque Wire (I2C)
    Wire.beginTransmission(MPU6050_ADDR); // Commence la transmission avec l'adresse du MPU6050
    Wire.write(0x6B); // Registre "Power Management 1"
    Wire.write(0);    // Met ce registre à 0 pour sortir le MPU6050 du mode veille
    Wire.endTransmission(true); // Termine la transmission et libère le bus
}

/**
 * @brief Lit les valeurs brutes de l'accéléromètre (X et Y) du MPU6050.
 */
void readMPU6050() {
    Wire.beginTransmission(MPU6050_ADDR); // Commence la transmission
    Wire.write(0x3B); // Registre de départ pour les données de l'accéléromètre (ACCEL_XOUT_H)
    Wire.endTransmission(false); // Termine la transmission mais garde la connexion ouverte pour la lecture

    // Demande 6 octets de données à partir de l'adresse du MPU6050
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)6, true);
    
    if (Wire.available() >= 6) { // Vérifie si 6 octets sont disponibles
      // Lit les 6 octets et combine les paires (High byte, Low byte) pour obtenir les valeurs 16 bits
      accX = (Wire.read() << 8) | Wire.read(); // Accélération sur l'axe X
      accY = (Wire.read() << 8) | Wire.read(); // Accélération sur l'axe Y
      Wire.read(); Wire.read(); // Lit et ignore les 2 octets suivants (ACCEL_ZOUT_H/L) pour ce projet
    }
}

// --- Fonctions principales Arduino ---
/**
 * @brief Fonction de configuration initiale, exécutée une seule fois au démarrage de la carte.
 */
void mySetup() {
    Serial.begin(115200); // Initialise la communication série (utile pour le débogage si réactivé)
    randomSeed(analogRead(0)); // Initialise le générateur de nombres aléatoires avec une valeur imprévisible
    testLvgl();    // Appelle la fonction d'initialisation de l'interface graphique
    initMPU6050(); // Appelle la fonction d'initialisation du capteur MPU6050
}

/**
 * @brief Boucle principale du microcontrôleur, exécutée en continu.
 * Gère le rafraîchissement de LVGL et introduit un petit délai.
 */
void loop() {

}

/**
 * @brief Boucle principale du jeu, appelée régulièrement par un timer LVGL.
 * Gère le mouvement de la balle, la détection des collisions et la mise à jour des objets du jeu.
 * @param timer Pointeur vers le timer LVGL qui a déclenché cette fonction (non utilisé directement ici)
 */
void gameLoop(lv_timer_t *timer) {
    // Ne s'exécute pas si le jeu n'est pas en cours ou est en état "Game Over"
    if (!gameStarted || isGameOver) return;

    readMPU6050(); // Lit les nouvelles données de l'accéléromètre

    // Met à jour la position de la balle en fonction des valeurs d'accélération
    // Le facteur ajuste la sensibilité du mouvement
    float factor = 0.0006; 
    ballX += accY * factor; // L'accélération Y du capteur influence le mouvement en X de la balle
    ballY += accX * factor; // L'accélération X du capteur influence le mouvement en Y de la balle

    // Vérifie les collisions de la balle avec les bords de l'écran
    if (ballX <= 0 || ballX >= SCREEN_WIDTH - BALL_SIZE ||
        ballY <= 0 || ballY >= SCREEN_HEIGHT - BALL_SIZE)
    {
        collisionCount++;     // Incrémente le compteur de collisions (vies perdues)
        updateLifeLabel();    // Met à jour l'affichage des vies
        clearObstacles();     // Supprime tous les obstacles bleus après une collision (reset du "terrain")

        if (collisionCount >= MAX_COLLISIONS) { // Si le nombre de vies est épuisé
            gameOver(); // Déclenche la fonction de fin de partie
        } else {
            // Replace la balle au centre si le joueur a encore des vies
            ballX = CENTER_X;
            ballY = CENTER_Y;
            if (ball) lv_obj_set_pos(ball, ballX, ballY); // Applique la nouvelle position à l'objet LVGL de la balle
        }
        return; // Sort de la fonction pour cette frame, la balle ayant été réinitialisée ou le jeu terminé
    }

    if (ball) lv_obj_set_pos(ball, ballX, ballY); // Applique la nouvelle position à l'objet balle LVGL

    // Calcul du centre de la balle et de son rayon pour la détection de collision cercle-rectangle
    float ballCenterX = ballX + BALL_SIZE / 2.0f;
    float ballCenterY = ballY + BALL_SIZE / 2.0f;
    float ballRadius = BALL_SIZE / 2.0f;

    // Détection de collision avec le cube vert
    if (greenCube != NULL && !lv_obj_has_flag(greenCube, LV_OBJ_FLAG_HIDDEN)) { // Si le cube vert existe et est visible
        lv_area_t greenCubeArea;
        lv_obj_get_coords(greenCube, &greenCubeArea); // Récupère les coordonnées du cube vert

        // Calcul du point le plus proche sur le cube vert par rapport au centre de la balle
        float closestXGreen = clamp(ballCenterX, (float)greenCubeArea.x1, (float)greenCubeArea.x2); // Utilisation de clamp
        float closestYGreen = clamp(ballCenterY, (float)greenCubeArea.y1, (float)greenCubeArea.y2); // Utilisation de clamp

        // Calcul de la distance au carré entre le centre de la balle et ce point le plus proche
        float distXGreen = ballCenterX - closestXGreen;
        float distYGreen = ballCenterY - closestYGreen;
        float distanceSquaredGreen = (distXGreen * distXGreen) + (distYGreen * distYGreen);

        // Si la distance au carré est inférieure au rayon de la balle au carré, il y a collision
        if (distanceSquaredGreen < (ballRadius * ballRadius)) { // S'il y a collision
            score += 100; // Le joueur gagne 100 points
            if (scoreLabel) { // Met à jour le label du score
                char buf[32];
                snprintf(buf, sizeof(buf), "Score : %d", score);
                lv_label_set_text(scoreLabel, buf);
            }
            lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); // Cache le cube vert après avoir été touché

            // Annule tout timer de spawn de cube vert précédent s'il existait
            if (green_cube_spawn_timer) {
                lv_timer_del(green_cube_spawn_timer);
                green_cube_spawn_timer = NULL;
            }
            // Programme l'apparition du prochain cube vert dans 5 secondes après cette collision
            green_cube_spawn_timer = lv_timer_create(spawnGreenCube, 5000, NULL);
            lv_timer_set_repeat_count(green_cube_spawn_timer, 1); // C'est un timer à usage unique
        }
    }

    // Boucle pour vérifier les collisions avec chaque obstacle bleu et gérer leur mouvement
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].obj != NULL) { // Si un obstacle existe à cet index
            // Met à jour la position précise de l'obstacle en fonction de sa vitesse
            obstacles[i].x_pos += obstacles[i].dx;
            obstacles[i].y_pos += obstacles[i].dy;

            // Gère les rebonds sur les bords de l'écran pour les obstacles
            if ((obstacles[i].x_pos <= 0 && obstacles[i].dx < 0) || (obstacles[i].x_pos >= SCREEN_WIDTH - OBSTACLE_SIZE && obstacles[i].dx > 0)) {
                obstacles[i].dx *= -1; // Inverse la direction sur l'axe X
            }
            if ((obstacles[i].y_pos <= 0 && obstacles[i].dy < 0) || (obstacles[i].y_pos >= SCREEN_HEIGHT - OBSTACLE_SIZE && obstacles[i].dy > 0)) {
                obstacles[i].dy *= -1; // Inverse la direction sur l'axe Y
            }

            // Applique la nouvelle position à l'objet LVGL de l'obstacle
            lv_obj_set_pos(obstacles[i].obj, (lv_coord_t)obstacles[i].x_pos, (lv_coord_t)obstacles[i].y_pos);

            // Récupère les coordonnées (zone) de l'obstacle pour la détection de collision
            lv_area_t obsArea;
            lv_obj_get_coords(obstacles[i].obj, &obsArea);

            // --- NOUVELLE DÉTECTION DE COLLISION CERCLE-RECTANGLE POUR LES OBSTACLES BLEUS ---
            // 1. Calcul du point le plus proche sur l'obstacle (rectangle) par rapport au centre de la balle
            float closestX = clamp(ballCenterX, (float)obsArea.x1, (float)obsArea.x2); // Utilisation de clamp
            float closestY = clamp(ballCenterY, (float)obsArea.y1, (float)obsArea.y2); // Utilisation de clamp

            // 2. Calcul de la distance au carré entre le centre de la balle et ce point le plus proche
            float distX = ballCenterX - closestX;
            float distY = ballCenterY - closestY;
            float distanceSquared = (distX * distX) + (distY * distY);

            // 3. Vérifie si la distance au carré est inférieure au rayon de la balle au carré
            if (distanceSquared < (ballRadius * ballRadius)) { // S'il y a collision
                collisionCount++;     // Incrémente le compteur de collisions
                updateLifeLabel();    // Met à jour l'affichage des vies
                clearObstacles();     // Supprime tous les obstacles bleus après la collision

                if (collisionCount >= MAX_COLLISIONS) { // Si les vies sont épuisées
                    gameOver(); // Déclenche la fin de partie
                } else {
                    // Replace la balle au centre si le joueur a encore des vies
                    ballX = CENTER_X;
                    ballY = CENTER_Y;
                    if (ball) lv_obj_set_pos(ball, ballX, ballY);
                }
                return; // Sort de la fonction pour cette frame
            }
        }
    }
}
