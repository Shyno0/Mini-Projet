/******************************************************************************
 * BIBLIOTHÈQUES
 ******************************************************************************/
#include <Arduino.h>      // Inclut la bibliothèque principale d'Arduino pour les fonctions de base.
#include <Wire.h>         // Inclut la bibliothèque pour la communication sur le bus I2C (utilisé par le capteur).
#include <math.h>         // Inclut la bibliothèque mathématique C++ pour les fonctions complexes.
#include "lvgl.h"        // Inclut la bibliothèque graphique LVGL pour créer l'interface utilisateur.
#include "lvglDrivers.h" // Inclut les pilotes pour faire le lien entre LVGL, l'écran et le tactile.

/******************************************************************************
 * CONSTANTES ET DÉFINITIONS
 ******************************************************************************/
#define MPU6050_ADDR 0x68       // Définit l'adresse I2C du capteur MPU6050 comme étant 0x68.
#define SCREEN_WIDTH 480        // Définit la largeur de l'écran à 480 pixels.
#define SCREEN_HEIGHT 270       // Définit la hauteur de l'écran à 270 pixels.
#define BALL_SIZE 20            // Définit la taille (diamètre) de la balle à 20 pixels.
#define CENTER_X (SCREEN_WIDTH / 2 - BALL_SIZE / 2)   // Calcule et définit la coordonnée X de départ pour centrer la balle.
#define CENTER_Y (SCREEN_HEIGHT / 2 - BALL_SIZE / 2)  // Calcule et définit la coordonnée Y de départ pour centrer la balle.
#define MAX_COLLISIONS 3        // Définit le nombre maximum de collisions autorisées (vies du joueur).
#define MAX_OBSTACLES 50        // Définit le nombre maximum d'obstacles qui peuvent exister en même temps.
#define OBSTACLE_SIZE 20        // Définit la taille des obstacles carrés à 20x20 pixels.
#define OBSTACLE_SPEED 1.5f     // Définit la vitesse de déplacement des obstacles (le 'f' indique un nombre à virgule).

/******************************************************************************
 * STRUCTURES DE DONNÉES
 ******************************************************************************/
typedef struct { // Déclare le début d'une nouvelle structure de données personnalisée.
    lv_obj_t* obj;          // Un pointeur ('*') pour stocker l'objet graphique LVGL de cet obstacle.
    float x_pos;            // La position horizontale (X) exacte (avec décimales).
    float y_pos;            // La position verticale (Y) exacte (avec décimales).
    float dx;               // La vitesse de déplacement sur l'axe X.
    float dy;               // La vitesse de déplacement sur l'axe Y.
} Obstacle;                 // Ferme la définition de la structure et lui donne le nom de type 'Obstacle'.

/******************************************************************************
 * VARIABLES GLOBALES
 ******************************************************************************/
// --- Objets graphiques LVGL ---
lv_obj_t *ball;               // Déclare un pointeur pour l'objet graphique de la balle.
lv_obj_t *gameOverLabel;      // Déclare un pointeur pour le texte "GAME OVER".
lv_obj_t *lifeLabel;          // Déclare un pointeur pour le texte affichant les vies.
lv_obj_t *scoreLabel;         // Déclare un pointeur pour le texte affichant le score.
lv_obj_t *scoreGameOverLabel; // Déclare un pointeur pour le texte du score final.
Obstacle obstacles[MAX_OBSTACLES]; // Crée un tableau (liste) pour stocker tous les objets de type 'Obstacle'.
lv_obj_t *greenCube = NULL;   // Déclare un pointeur pour le cube vert, initialisé à NULL (il n'existe pas encore).

// --- Conteneurs d'écrans ---
lv_obj_t *main_menu_container;  // Déclare un pointeur pour le conteneur du menu principal.
lv_obj_t *color_menu_container; // Déclare un pointeur pour le conteneur du menu de sélection de couleur.
lv_color_t ball_color;          // Déclare une variable pour stocker la couleur choisie pour la balle.

// --- États et données du jeu ---
bool gameStarted = false;       // Déclare un booléen (vrai/faux) pour savoir si le jeu a commencé, initialisé à 'faux'.
bool isGameOver = false;        // Déclare un booléen pour savoir si la partie est terminée, initialisé à 'faux'.
int collisionCount = 0;         // Déclare un entier pour compter les collisions (vies perdues), initialisé à 0.
int score = 0;                  // Déclare un entier pour le score du joueur, initialisé à 0.
int ballX = CENTER_X;           // Déclare la position X de la balle et l'initialise au centre.
int ballY = CENTER_Y;           // Déclare la position Y de la balle et l'initialise au centre.
int16_t accX = 0, accY = 0;     // Déclare deux entiers 16-bit pour stocker les données brutes de l'accéléromètre.

// --- Timers LVGL (tâches répétitives) ---
lv_timer_t* obstacle_spawn_timer = NULL;   // Déclare un pointeur de timer pour la création d'obstacles.
lv_timer_t* score_timer = NULL;            // Déclare un pointeur de timer pour l'incrémentation du score.
lv_timer_t* movement_timer = NULL;         // Déclare un pointeur de timer pour la boucle de jeu principale.
lv_timer_t* green_cube_spawn_timer = NULL; // Déclare un pointeur de timer pour l'apparition du cube vert.

/******************************************************************************
 * DÉCLARATIONS ANTICIPÉES DES FONCTIONS
 ******************************************************************************/
// Permet d'utiliser ces fonctions avant leur définition complète plus bas dans le code.
void gameLoop(lv_timer_t *timer);   // Déclaration anticipée de la fonction de la boucle de jeu.
void createMainMenu();              // Déclaration anticipée de la fonction de création du menu principal.
void createColorMenu();             // Déclaration anticipée de la fonction de création du menu des couleurs.
void initGreenCubeObject();         // Déclaration anticipée de la fonction d'initialisation du cube vert.
void spawnGreenCube(lv_timer_t *timer); // Déclaration anticipée de la fonction d'apparition du cube vert.
void returnToMenu(lv_timer_t *timer);   // Déclaration anticipée de la fonction de retour au menu.

/******************************************************************************
 * FONCTIONS UTILITAIRES
 ******************************************************************************/
// Définit une fonction "template" (générique) nommée 'clamp'.
template <typename T>
T clamp(T val, T low, T high) { // La fonction accepte n'importe quel type de nombre et trois arguments : la valeur, une limite basse et une haute.
    if (val < low) return low;      // Si la valeur est plus petite que la limite basse, la fonction retourne la limite basse.
    if (val > high) return high;    // Si la valeur est plus grande que la limite haute, la fonction retourne la limite haute.
    return val;                     // Sinon (si la valeur est entre les limites), la fonction retourne la valeur elle-même.
} // Fin de la fonction clamp.

// Définit la fonction 'createBasicLvObject' qui retourne un pointeur vers un objet LVGL.
lv_obj_t* createBasicLvObject(lv_obj_t* parent, lv_coord_t width, lv_coord_t height, lv_color_t color, bool isCircle) {
    lv_obj_t* obj = lv_obj_create(parent);            // Crée un nouvel objet LVGL comme enfant de l'objet 'parent'.
    lv_obj_set_size(obj, width, height);              // Définit la largeur et la hauteur de l'objet créé.
    lv_obj_set_style_bg_color(obj, color, 0);         // Définit la couleur de fond de l'objet.
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);   // Empêche l'objet de pouvoir être défilé avec le doigt (scroll).
    if (isCircle) {                                   // Vérifie si le booléen 'isCircle' est vrai.
        lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0); // Si c'est vrai, change le style pour que l'objet soit parfaitement rond.
    } // Fin du bloc de condition 'if'.
    return obj;                                       // Retourne le pointeur vers l'objet qui vient d'être créé.
} // Fin de la fonction createBasicLvObject.


/******************************************************************************
 * GESTION DES OBSTACLES BLEUS
 ******************************************************************************/
// Définit la fonction 'initObstacles' qui ne prend pas d'argument et ne retourne rien.
void initObstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {   // Démarre une boucle 'for' qui compte de 0 jusqu'à MAX_OBSTACLES-1.
        obstacles[i].obj = NULL;                // Pour chaque case du tableau, met le pointeur de l'objet à NULL (indiquant un emplacement vide).
    } // Fin de la boucle for.
} // Fin de la fonction initObstacles.

// Définit la fonction 'clearObstacles'.
void clearObstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {   // Démarre une boucle pour parcourir tous les emplacements d'obstacles possibles.
        if (obstacles[i].obj != NULL) {         // Si un objet existe à cet emplacement (le pointeur n'est pas nul)...
            lv_obj_del(obstacles[i].obj);       // ...alors supprime l'objet graphique correspondant de l'écran.
            obstacles[i].obj = NULL;            // ...et remet le pointeur à NULL pour marquer l'emplacement comme libre.
        } // Fin du bloc de condition 'if'.
    } // Fin de la boucle for.
} // Fin de la fonction clearObstacles.

// Définit la fonction 'createObstacle' qui est appelée par un timer.
void createObstacle(lv_timer_t *timer) {
    if (!gameStarted || isGameOver) return; // Si le jeu n'a pas commencé OU s'il est terminé, quitte immédiatement la fonction.

    for (int i = 0; i < MAX_OBSTACLES; i++) { // Boucle pour trouver un emplacement d'obstacle libre.
        if (obstacles[i].obj == NULL) {     // Si l'emplacement 'i' est libre...
            obstacles[i].obj = createBasicLvObject(lv_screen_active(), OBSTACLE_SIZE, OBSTACLE_SIZE, lv_color_hex(0x0000FF), false); // ...crée un cube bleu sur l'écran actif.
            
            int side = random(0, 4);        // Choisit un nombre aléatoire entre 0 et 3 pour le côté d'apparition.
            float x, y;                     // Déclare les variables pour les coordonnées de départ.
            switch (side) {                 // Commence une structure de choix basée sur la variable 'side'.
                case 0: x = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); y = -OBSTACLE_SIZE; obstacles[i].dx = 0; obstacles[i].dy = OBSTACLE_SPEED; break; // Cas 0: Apparition en haut.
                case 1: x = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); y = SCREEN_HEIGHT; obstacles[i].dx = 0; obstacles[i].dy = -OBSTACLE_SPEED; break; // Cas 1: Apparition en bas.
                case 2: x = -OBSTACLE_SIZE; y = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); obstacles[i].dx = OBSTACLE_SPEED; obstacles[i].dy = 0; break; // Cas 2: Apparition à gauche.
                case 3: x = SCREEN_WIDTH; y = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); obstacles[i].dx = -OBSTACLE_SPEED; obstacles[i].dy = 0; break; // Cas 3: Apparition à droite.
            } // Fin du 'switch'.
            lv_obj_set_pos(obstacles[i].obj, (lv_coord_t)x, (lv_coord_t)y); // Positionne l'objet graphique aux coordonnées calculées.
            obstacles[i].x_pos = x;         // Stocke la position X exacte dans la structure de l'obstacle.
            obstacles[i].y_pos = y;         // Stocke la position Y exacte dans la structure de l'obstacle.
            return;                         // Quitte la fonction car un obstacle a été créé (inutile de continuer la boucle).
        } // Fin du bloc 'if'.
    } // Fin de la boucle 'for'.
} // Fin de la fonction createObstacle.

/******************************************************************************
 * GESTION DE L'INTERFACE UTILISATEUR (UI) ET DE L'ÉTAT DU JEU
 ******************************************************************************/
// Définit la fonction 'updateLifeLabel (pour le nombre de vie).
void updateLifeLabel() {
    if (lifeLabel) { // Vérifie si le pointeur 'lifeLabel' est valide (n'est pas NULL).
        char buf[16]; // Crée un tableau de 16 caractères pour stocker le texte.
        snprintf(buf, sizeof(buf), "Vies : %d", MAX_COLLISIONS - collisionCount); // Formate le texte "Vies : X" et le met dans 'buf'.
        lv_label_set_text(lifeLabel, buf); // Met à jour le texte de l'objet 'lifeLabel' avec le contenu de 'buf'.
    } // Fin du bloc 'if'.
} // Fin de la fonction updateLifeLabel.

// Définit la fonction 'incrementScore', appelée par un timer (ajoute du score par seconde).
void incrementScore(lv_timer_t *timer) {
    if (gameStarted && !isGameOver) { // Si le jeu est en cours ET que ce n'est pas game over...
        score += 10; // ...ajoute 10 points au score.
        if (scoreLabel) { // Vérifie si l'objet 'scoreLabel' existe.
            char buf[32]; // Crée un buffer de 32 caractères pour le texte du score.
            snprintf(buf, sizeof(buf), "Score : %d", score); // Formate le texte "Score : X" et le met dans 'buf'.
            lv_label_set_text(scoreLabel, buf); // Met à jour l'objet texte du score.
        } // Fin du bloc 'if'.
    } // Fin du bloc 'if'.
} // Fin de la fonction incrementScore.

// Définit la fonction 'gameOver'.
void gameOver() {
    gameStarted = false; // Met la variable d'état du jeu à 'faux'.
    isGameOver = true;   // Met la variable d'état de fin de partie à 'vrai'.
    if (ball) { // Si l'objet balle existe...
        lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // ...lui ajoute le drapeau "caché" pour le faire disparaître.
    } // Fin du bloc 'if'.

    // Arrête et supprime tous les timers du jeu pour économiser les ressources.
    if (obstacle_spawn_timer) { lv_timer_del(obstacle_spawn_timer); obstacle_spawn_timer = NULL; } // Supprime le timer des obstacles.
    if (score_timer) { lv_timer_del(score_timer); score_timer = NULL; } // Supprime le timer du score.
    if (movement_timer) { lv_timer_del(movement_timer); movement_timer = NULL; } // Supprime le timer de la boucle de jeu.
    if (green_cube_spawn_timer) { lv_timer_del(green_cube_spawn_timer); green_cube_spawn_timer = NULL; } // Supprime le timer du cube vert.

    // Supprime les labels de l'interface de jeu.
    if (lifeLabel) { lv_obj_del(lifeLabel); lifeLabel = NULL; } // Supprime le label des vies.
    if (scoreLabel) { lv_obj_del(scoreLabel); scoreLabel = NULL; } // Supprime le label du score.

    clearObstacles(); // Appelle la fonction pour supprimer tous les obstacles bleus de l'écran.

    if (greenCube) { lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); } // Cache le cube vert s'il est visible.

    // Affiche le message "GAME OVER".
    gameOverLabel = lv_label_create(lv_screen_active()); // Crée un nouvel objet label sur l'écran actif.
    lv_label_set_text(gameOverLabel, "GAME OVER"); // Définit le texte de ce label.
    lv_obj_set_style_text_font(gameOverLabel, &lv_font_montserrat_14, 0); // Définit la police de ce label.
    lv_obj_center(gameOverLabel); // Centre ce label au milieu de l'écran.

    // Affiche le score final.
    scoreGameOverLabel = lv_label_create(lv_screen_active()); // Crée un autre objet label.
    char buf[32]; // Crée un buffer de 32 caractères.
    snprintf(buf, sizeof(buf), "Score final : %d", score); // Formate le texte du score final.
    lv_label_set_text(scoreGameOverLabel, buf); // Applique ce texte au label.
    lv_obj_align_to(scoreGameOverLabel, gameOverLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 10); // Aligne ce label sous le message "GAME OVER".

    // Crée un timer à usage unique pour revenir au menu après 3 secondes.
    lv_timer_t *t = lv_timer_create(returnToMenu, 3000, NULL); // Crée un timer qui appellera 'returnToMenu' dans 3000 ms.
    lv_timer_set_repeat_count(t, 1); // Configure ce timer pour qu'il ne s'exécute qu'une seule fois.
} // Fin de la fonction gameOver.

// Définit la fonction 'returnToMenu', appelée par un timer.
void returnToMenu(lv_timer_t *timer) {
    if (gameOverLabel) { lv_obj_del(gameOverLabel); gameOverLabel = NULL; } // Si le label "GAME OVER" existe, le supprime.
    if (scoreGameOverLabel) { lv_obj_del(scoreGameOverLabel); scoreGameOverLabel = NULL; } // Si le label du score final existe, le supprime.

    clearObstacles(); // Appelle la fonction pour effacer tous les obstacles.

    ballX = CENTER_X; // Réinitialise la coordonnée X de la balle.
    ballY = CENTER_Y; // Réinitialise la coordonnée Y de la balle.
    if (ball) { // Si l'objet balle existe...
        lv_obj_set_pos(ball, ballX, ballY); // ...le repositionne au centre.
        lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // ...et le cache.
    } // Fin du bloc 'if'.

    if (greenCube) { lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); } // Cache le cube vert.
    if (green_cube_spawn_timer) { // Si le timer du cube vert existe...
        lv_timer_del(green_cube_spawn_timer); // ...le supprime.
        green_cube_spawn_timer = NULL; // ...et met son pointeur à NULL.
    } // Fin du bloc 'if'.

    // Réinitialise toutes les variables d'état du jeu à leurs valeurs par défaut.
    collisionCount = 0; // Remet le compteur de collisions à zéro.
    score = 0; // Remet le score à zéro.
    isGameOver = false; // Indique que la partie n'est plus terminée.
    gameStarted = false; // Indique que le jeu n'est plus en cours.

    if (main_menu_container) { // Si le conteneur du menu principal existe...
        lv_obj_clear_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // ...enlève son drapeau "caché" pour le rendre visible.
    } // Fin du bloc 'if'.
} // Fin de la fonction returnToMenu.


// Définit la fonction 'startGame'.
void startGame() {
    if (main_menu_container) lv_obj_add_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu principal.
    if (color_menu_container) lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu des couleurs.

    // Met à jour les variables d'état pour une nouvelle partie.
    gameStarted = true; // Indique que le jeu a commencé.
    isGameOver = false; // Indique que ce n'est pas encore la fin de la partie.
    ballX = CENTER_X; // Réinitialise la position X de la balle.
    ballY = CENTER_Y; // Réinitialise la position Y de la balle.
    collisionCount = 0; // Réinitialise le compteur de collisions.
    score = 0; // Réinitialise le score.

    if (ball) { // Si l'objet balle existe...
        lv_obj_set_style_bg_color(ball, ball_color, 0); // ...applique la couleur choisie par le joueur.
        lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN); // ...le rend visible en enlevant le drapeau "caché".
        lv_obj_set_pos(ball, ballX, ballY); // ...le place au centre de l'écran.
    } // Fin du bloc 'if'.

    clearObstacles(); // Efface les éventuels obstacles d'une partie précédente.
    initObstacles();  // Réinitialise le tableau des obstacles.

    lifeLabel = lv_label_create(lv_screen_active()); // Crée le label pour les vies.
    lv_obj_align(lifeLabel, LV_ALIGN_TOP_LEFT, 10, 5); // Le positionne en haut à gauche.
    updateLifeLabel(); // Met à jour son texte initial.

    scoreLabel = lv_label_create(lv_screen_active()); // Crée le label pour le score.
    lv_obj_align(scoreLabel, LV_ALIGN_TOP_LEFT, 10, 25); // Le positionne sous le label des vies.
    lv_label_set_text(scoreLabel, "Score : 0"); // Met son texte initial à "Score : 0".

    // Crée et démarre tous les timers nécessaires au déroulement du jeu.
    obstacle_spawn_timer = lv_timer_create(createObstacle, 2000, NULL); // Un obstacle apparaîtra toutes les 2 secondes.
    score_timer = lv_timer_create(incrementScore, 1000, NULL);          // Le score augmentera toutes les secondes.
    movement_timer = lv_timer_create(gameLoop, 20, NULL);               // La boucle de jeu s'exécutera toutes les 20 ms (50 FPS).

    spawnGreenCube(NULL); // Fait apparaître le premier cube vert immédiatement.
    if (green_cube_spawn_timer) { // S'assure qu'aucun ancien timer n'existe.
        lv_timer_del(green_cube_spawn_timer); // Le supprime le cas échéant.
        green_cube_spawn_timer = NULL; // Met le pointeur à NULL.
    } // Fin du bloc 'if'.
    green_cube_spawn_timer = lv_timer_create(spawnGreenCube, 5000, NULL); // Programme l'apparition du prochain cube dans 5 secondes.
    lv_timer_set_repeat_count(green_cube_spawn_timer, 1); // Ce timer ne s'exécutera qu'une fois.
} // Fin de la fonction startGame.

/******************************************************************************
 * CRÉATION DES MENUS
 ******************************************************************************/
// Définit la fonction 'color_select_event_cb', appelée lors d'un clic sur une couleur.
void color_select_event_cb(lv_event_t * e) {
    lv_obj_t * swatch = (lv_obj_t *)lv_event_get_target(e); // Récupère l'objet (la pastille de couleur) qui a été cliqué.
    ball_color = lv_obj_get_style_bg_color(swatch, 0);       // Récupère la couleur de fond de cet objet.
    if (ball) { // Si la balle existe...
        lv_obj_set_style_bg_color(ball, ball_color, 0);       // ...applique cette nouvelle couleur à la balle.
    } // Fin du bloc 'if'.
} // Fin de la fonction color_select_event_cb.

// Définit la fonction 'createMainMenu'.
void createMainMenu() {
    main_menu_container = lv_obj_create(lv_screen_active()); // Crée le conteneur du menu principal sur l'écran actif.
    lv_obj_remove_style_all(main_menu_container); // Supprime tout style par défaut (bordure, fond) pour qu'il soit transparent.
    lv_obj_set_size(main_menu_container, SCREEN_WIDTH, SCREEN_HEIGHT); // Lui donne la taille de l'écran.
    lv_obj_center(main_menu_container); // Le centre sur l'écran.

    lv_obj_t* playBtn = lv_btn_create(main_menu_container); // Crée un bouton "JOUER" comme enfant du conteneur.
    lv_obj_align(playBtn, LV_ALIGN_CENTER, 0, -25); // L'aligne au centre, légèrement décalé vers le haut.
    lv_obj_t *label = lv_label_create(playBtn); // Crée un label pour le texte du bouton.
    lv_label_set_text(label, "JOUER"); // Définit son texte.
    lv_obj_center(label); // Centre le texte à l'intérieur du bouton.
    lv_obj_add_event_cb(playBtn, [](lv_event_t *e) { startGame(); }, LV_EVENT_CLICKED, NULL); // Ajoute une action : au clic, appeler la fonction 'startGame'.

    lv_obj_t* colorBtn = lv_btn_create(main_menu_container); // Crée un bouton "Couleur".
    lv_obj_align(colorBtn, LV_ALIGN_CENTER, 0, 25); // L'aligne au centre, légèrement décalé vers le bas.
    lv_obj_t *colorLabel = lv_label_create(colorBtn); // Crée le label pour ce bouton.
    lv_label_set_text(colorLabel, "Couleur"); // Définit son texte.
    lv_obj_center(colorLabel); // Centre le texte dans le bouton.
    lv_obj_add_event_cb(colorBtn, [](lv_event_t *e) { // Ajoute une action pour le clic sur le bouton "Couleur".
        if (main_menu_container) lv_obj_add_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu principal.
        if (ball) { // Si la balle existe...
            ballX = CENTER_X; // ...réinitialise sa position X au centre.
            ballY = CENTER_Y; // ...réinitialise sa position Y au centre.
            lv_obj_set_pos(ball, ballX, ballY); // ...applique cette position.
            lv_obj_clear_flag(ball, LV_OBJ_FLAG_HIDDEN); // ...et la rend visible pour la prévisualisation.
        } // Fin du bloc 'if'.
        if (color_menu_container) lv_obj_clear_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Affiche le menu de sélection de couleur.
    }, LV_EVENT_CLICKED, NULL); // Fin de la définition de l'action de clic.
} // Fin de la fonction createMainMenu.

// Définit la fonction 'createColorMenu'.
void createColorMenu() {
    color_menu_container = lv_obj_create(lv_screen_active()); // Crée le conteneur pour le menu couleur.
    lv_obj_remove_style_all(color_menu_container); // Supprime son style par défaut.
    lv_obj_set_size(color_menu_container, SCREEN_WIDTH, SCREEN_HEIGHT); // Lui donne la taille de l'écran.
    lv_obj_center(color_menu_container); // Le centre.
    lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Le cache par défaut.

    lv_obj_t* color_panel = lv_obj_create(color_menu_container); // Crée un panneau pour les pastilles de couleur.
    lv_obj_set_size(color_panel, 300, 50); // Définit sa taille.
    lv_obj_align(color_panel, LV_ALIGN_CENTER, 0, 30); // L'aligne au centre de l'écran.
    lv_obj_set_layout(color_panel, LV_LAYOUT_FLEX); // Utilise un layout flexible pour aligner les enfants automatiquement.
    lv_obj_set_style_flex_flow(color_panel, LV_FLEX_FLOW_ROW, 0); // Les enfants seront alignés en ligne.
    lv_obj_set_style_flex_main_place(color_panel, LV_FLEX_ALIGN_SPACE_EVENLY, 0); // Espace égal entre les enfants.
    lv_obj_set_style_pad_all(color_panel, 5, 0); // Ajoute une petite marge intérieure.
    lv_obj_set_style_border_width(color_panel, 0, 0); // Supprime la bordure.
    lv_obj_set_style_bg_opa(color_panel, LV_OPA_TRANSP, 0); // Rend le fond du panneau transparent.

    lv_color_t colors[] = { // Crée un tableau de couleurs.
        lv_color_hex(0xFF0000), lv_color_hex(0x00FF00), lv_color_hex(0x00BFFF), // Rouge, Vert, Bleu Ciel
        lv_color_hex(0xFFFF00), lv_color_hex(0xFF00FF), lv_color_hex(0xFFA500)  // Jaune, Magenta, Orange
    }; // Fin du tableau de couleurs.
    for (int i = 0; i < 6; i++) { // Boucle pour créer les 6 pastilles de couleur.
        lv_obj_t* swatch = lv_btn_create(color_panel); // Crée une pastille (qui est un bouton).
        lv_obj_set_size(swatch, 40, 40); // Lui donne une taille de 40x40 pixels.
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0); // La rend parfaitement ronde.
        lv_obj_set_style_bg_color(swatch, colors[i], 0); // Applique une couleur du tableau.
        lv_obj_add_event_cb(swatch, color_select_event_cb, LV_EVENT_CLICKED, NULL); // Attache la fonction de sélection de couleur à l'événement de clic.
    } // Fin de la boucle 'for'.

    lv_obj_t * backBtn = lv_btn_create(color_menu_container); // Crée un bouton "Retour".
    lv_obj_align(backBtn, LV_ALIGN_BOTTOM_LEFT, 10, -10); // Le positionne en bas à gauche.
    lv_obj_t * backLabel = lv_label_create(backBtn); // Crée un label pour le texte du bouton.
    lv_label_set_text(backLabel, "Retour"); // Définit le texte.
    lv_obj_center(backLabel); // Centre le texte dans le bouton.
    lv_obj_add_event_cb(backBtn, [](lv_event_t *e) { // Ajoute une action de clic pour le bouton "Retour".
        if (color_menu_container) lv_obj_add_flag(color_menu_container, LV_OBJ_FLAG_HIDDEN); // Cache le menu des couleurs.
        if (ball) { // Si la balle existe...
            lv_obj_set_pos(ball, CENTER_X, CENTER_Y); // ...la replace au centre.
            lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // ...et la cache, car on retourne au menu principal.
        } // Fin du bloc 'if'.
        if (main_menu_container) lv_obj_clear_flag(main_menu_container, LV_OBJ_FLAG_HIDDEN); // Affiche le menu principal.
    }, LV_EVENT_CLICKED, NULL); // Fin de l'action de clic.
} // Fin de la fonction createColorMenu.


/******************************************************************************
 * GESTION DU CUBE VERT
 ******************************************************************************/
// Définit la fonction 'initGreenCubeObject'.
void initGreenCubeObject() {
    greenCube = createBasicLvObject(lv_screen_active(), OBSTACLE_SIZE, OBSTACLE_SIZE, lv_color_hex(0x00FF00), false); // Crée le cube vert.
    lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); // Le cache immédiatement après sa création.
} // Fin de la fonction initGreenCubeObject.

// Définit la fonction 'spawnGreenCube', appelée par un timer.
void spawnGreenCube(lv_timer_t *timer) {
    if (!gameStarted || isGameOver || greenCube == NULL) return; // Ne fait rien si le jeu n'est pas en cours ou si le cube n'a pas été initialisé.

    int greenCubeX = random(0, SCREEN_WIDTH - OBSTACLE_SIZE); // Choisit une coordonnée X aléatoire sur l'écran.
    int greenCubeY = random(0, SCREEN_HEIGHT - OBSTACLE_SIZE); // Choisit une coordonnée Y aléatoire sur l'écran.
    
    lv_obj_set_pos(greenCube, greenCubeX, greenCubeY); // Positionne le cube à ces coordonnées.
    lv_obj_clear_flag(greenCube, LV_OBJ_FLAG_HIDDEN); // Le rend visible en enlevant son drapeau "caché".

    green_cube_spawn_timer = NULL; // Réinitialise le pointeur du timer, car ce timer est à usage unique et s'est exécuté.
} // Fin de la fonction spawnGreenCube.


/******************************************************************************
 * INITIALISATION GRAPHIQUE PRINCIPALE
 ******************************************************************************/
// Définit la fonction 'testLvgl'.
void testLvgl() {
    ball_color = lv_color_hex(0xFF0000); // Définit la couleur par défaut de la balle (rouge).

    ball = createBasicLvObject(lv_screen_active(), BALL_SIZE, BALL_SIZE, ball_color, true); // Crée l'objet balle.
    lv_obj_add_flag(ball, LV_OBJ_FLAG_HIDDEN); // La cache par défaut, elle ne sera visible qu'en jeu.
    lv_obj_set_pos(ball, CENTER_X, CENTER_Y); // La positionne au centre.

    initObstacles(); // Appelle la fonction pour initialiser le tableau d'obstacles.
    initGreenCubeObject(); // Appelle la fonction pour créer l'objet cube vert.

    createColorMenu(); // Appelle la fonction pour créer les objets du menu couleur (ils sont cachés).
    createMainMenu();  // Appelle la fonction pour créer et afficher le menu principal.
} // Fin de la fonction testLvgl.

/******************************************************************************
 * GESTION DU CAPTEUR (MPU6050)
 ******************************************************************************/
// Définit la fonction 'initMPU6050'.
void initMPU6050() {
    Wire.begin(); // Initialise la bibliothèque Wire pour la communication I2C.
    Wire.beginTransmission(MPU6050_ADDR); // Commence une transmission vers l'adresse du capteur.
    Wire.write(0x6B); // Envoie l'adresse du registre de gestion de l'alimentation (Power Management 1).
    Wire.write(0);    // Envoie la valeur 0 à ce registre pour sortir le capteur du mode veille.
    Wire.endTransmission(true); // Termine la transmission et libère le bus I2C.
} // Fin de la fonction initMPU6050.

// Définit la fonction 'readMPU6050'.
void readMPU6050() {
    Wire.beginTransmission(MPU6050_ADDR); // Commence une transmission vers le capteur.
    Wire.write(0x3B); // Envoie l'adresse du premier registre de données de l'accéléromètre (ACCEL_XOUT_H).
    Wire.endTransmission(false); // Termine la transmission mais garde la connexion active pour une lecture.
    
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)6, true); // Demande 6 octets de données au capteur.
    
    if (Wire.available() >= 6) { // Si au moins 6 octets sont disponibles dans le buffer de réception...
        accX = (Wire.read() << 8) | Wire.read(); // Lit l'octet de poids fort, le décale de 8 bits à gauche, et le combine avec l'octet de poids faible pour former la valeur 16-bit de l'axe X.
        accY = (Wire.read() << 8) | Wire.read(); // Fait de même pour la valeur de l'axe Y.
        Wire.read(); Wire.read(); // Lit et ignore les deux octets de l'axe Z, qui ne sont pas utilisés dans ce projet.
    } // Fin du bloc 'if'.
} // Fin de la fonction readMPU6050.

/******************************************************************************
 * BOUCLE PRINCIPALE DU JEU
 ******************************************************************************/
// Définit la fonction 'gameLoop', appelée par un timer.
void gameLoop(lv_timer_t *timer) {
    if (!gameStarted || isGameOver) return; // Quitte si le jeu n'est pas en cours.

    readMPU6050(); // Lit les dernières valeurs du capteur d'inclinaison.

    float factor = 0.0006; // Définit un facteur de sensibilité pour le mouvement.
    ballX += accY * factor; // Met à jour la position X de la balle en fonction de l'inclinaison sur l'axe Y du capteur (axes inversés).
    ballY += accX * factor; // Met à jour la position Y de la balle en fonction de l'inclinaison sur l'axe X du capteur.

    if (ballX <= 0 || ballX >= SCREEN_WIDTH - BALL_SIZE || ballY <= 0 || ballY >= SCREEN_HEIGHT - BALL_SIZE) { // Vérifie si la balle touche un des quatre bords de l'écran.
        collisionCount++; // Incrémente le compteur de vies perdues.
        updateLifeLabel(); // Met à jour l'affichage des vies.
        clearObstacles(); // Efface tous les obstacles.

        if (collisionCount >= MAX_COLLISIONS) { // Si le joueur n'a plus de vies...
            gameOver(); // ...déclenche la fin de la partie.
        } else { // Sinon...
            ballX = CENTER_X; // ...replace la balle au centre.
            ballY = CENTER_Y; // ...replace la balle au centre.
            if (ball) lv_obj_set_pos(ball, ballX, ballY); // Applique la nouvelle position.
        } // Fin du bloc if/else.
        return; // Quitte la fonction pour cette frame, car la balle a été réinitialisée.
    } // Fin du bloc if pour la collision avec les bords.

    if (ball) lv_obj_set_pos(ball, ballX, ballY); // Applique la nouvelle position de la balle à son objet graphique.

    float ballCenterX = ballX + BALL_SIZE / 2.0f; // Calcule la coordonnée X du centre de la balle.
    float ballCenterY = ballY + BALL_SIZE / 2.0f; // Calcule la coordonnée Y du centre de la balle.
    float ballRadius = BALL_SIZE / 2.0f; // Calcule le rayon de la balle.

    // --- Détection de collision avec le cube vert ---
    if (greenCube != NULL && !lv_obj_has_flag(greenCube, LV_OBJ_FLAG_HIDDEN)) { // Si le cube vert existe et est visible...
        lv_area_t greenCubeArea; // Crée une structure pour stocker les coordonnées du cube.
        lv_obj_get_coords(greenCube, &greenCubeArea); // Récupère les coordonnées du cube.

        float closestXGreen = clamp(ballCenterX, (float)greenCubeArea.x1, (float)greenCubeArea.x2); // Trouve le point X sur le cube le plus proche du centre de la balle.
        float closestYGreen = clamp(ballCenterY, (float)greenCubeArea.y1, (float)greenCubeArea.y2); // Trouve le point Y sur le cube le plus proche du centre de la balle.
        float distXGreen = ballCenterX - closestXGreen; // Calcule la distance en X entre la balle et ce point.
        float distYGreen = ballCenterY - closestYGreen; // Calcule la distance en Y entre la balle et ce point.
        float distanceSquaredGreen = (distXGreen * distXGreen) + (distYGreen * distYGreen); // Calcule la distance au carré pour éviter une racine carrée coûteuse.

        if (distanceSquaredGreen < (ballRadius * ballRadius)) { // Si la distance au carré est inférieure au rayon au carré, il y a collision.
            score += 100; // Ajoute 100 points au score.
            if (scoreLabel) { // Si le label du score existe...
                char buf[32]; // ...crée un buffer.
                snprintf(buf, sizeof(buf), "Score : %d", score); // ...formate le nouveau score.
                lv_label_set_text(scoreLabel, buf); // ...et met à jour le texte.
            } // Fin du bloc 'if'.
            lv_obj_add_flag(greenCube, LV_OBJ_FLAG_HIDDEN); // Cache le cube vert.

            if (green_cube_spawn_timer) { // S'il y a un timer d'apparition en attente...
                lv_timer_del(green_cube_spawn_timer); // ...le supprime.
                green_cube_spawn_timer = NULL; // ...et réinitialise le pointeur.
            } // Fin du bloc 'if'.
            green_cube_spawn_timer = lv_timer_create(spawnGreenCube, 5000, NULL); // Programme l'apparition du prochain cube dans 5 secondes.
            lv_timer_set_repeat_count(green_cube_spawn_timer, 1); // Ce timer ne s'exécutera qu'une fois.
        } // Fin du bloc 'if' de collision.
    } // Fin du bloc 'if' de vérification du cube vert.

    // --- Mouvement et collision des obstacles bleus ---
    for (int i = 0; i < MAX_OBSTACLES; i++) { // Boucle pour parcourir tous les obstacles possibles.
        if (obstacles[i].obj != NULL) { // Si un obstacle existe à cet emplacement...
            obstacles[i].x_pos += obstacles[i].dx; // ...met à jour sa position X en fonction de sa vitesse.
            obstacles[i].y_pos += obstacles[i].dy; // ...met à jour sa position Y en fonction de sa vitesse.

            if ((obstacles[i].x_pos <= 0 && obstacles[i].dx < 0) || (obstacles[i].x_pos >= SCREEN_WIDTH - OBSTACLE_SIZE && obstacles[i].dx > 0)) { // S'il touche un bord vertical...
                obstacles[i].dx *= -1; // ...inverse sa direction horizontale pour le faire rebondir.
            } // Fin du bloc 'if'.
            if ((obstacles[i].y_pos <= 0 && obstacles[i].dy < 0) || (obstacles[i].y_pos >= SCREEN_HEIGHT - OBSTACLE_SIZE && obstacles[i].dy > 0)) { // S'il touche un bord horizontal...
                obstacles[i].dy *= -1; // ...inverse sa direction verticale.
            } // Fin du bloc 'if'.

            lv_obj_set_pos(obstacles[i].obj, (lv_coord_t)obstacles[i].x_pos, (lv_coord_t)obstacles[i].y_pos); // Applique la nouvelle position à l'objet graphique.

            lv_area_t obsArea; // Crée une structure pour les coordonnées de l'obstacle.
            lv_obj_get_coords(obstacles[i].obj, &obsArea); // Récupère ses coordonnées.

            float closestX = clamp(ballCenterX, (float)obsArea.x1, (float)obsArea.x2); // Trouve le point X sur l'obstacle le plus proche du centre de la balle.
            float closestY = clamp(ballCenterY, (float)obsArea.y1, (float)obsArea.y2); // Trouve le point Y sur l'obstacle le plus proche du centre de la balle.
            float distX = ballCenterX - closestX; // Calcule la distance en X.
            float distY = ballCenterY - closestY; // Calcule la distance en Y.
            float distanceSquared = (distX * distX) + (distY * distY); // Calcule la distance au carré.

            if (distanceSquared < (ballRadius * ballRadius)) { // S'il y a collision...
                collisionCount++; // ...incrémente le compteur de vies perdues.
                updateLifeLabel(); // ...met à jour l'affichage.
                clearObstacles(); // ...efface tous les obstacles.

                if (collisionCount >= MAX_COLLISIONS) { // Si le joueur n'a plus de vies...
                    gameOver(); // ...déclenche la fin du jeu.
                } else { // Sinon...
                    ballX = CENTER_X; // ...replace la balle au centre.
                    ballY = CENTER_Y; // ...replace la balle au centre.
                    if (ball) lv_obj_set_pos(ball, ballX, ballY); // Applique sa nouvelle position.
                } // Fin du bloc if/else.
                return; // Quitte la fonction pour cette frame.
            } // Fin du bloc 'if' de collision.
        } // Fin du bloc 'if' de vérification de l'obstacle.
    } // Fin de la boucle 'for' des obstacles.
} // Fin de la fonction gameLoop.


/******************************************************************************
 * FONCTIONS PRINCIPALES ARDUINO
 ******************************************************************************/
// Définit la fonction de configuration 'mySetup', qui s'exécute une seule fois au démarrage de la carte.
void mySetup() {
    Serial.begin(115200); // Initialise la communication série (pour le débogage via le moniteur série) à une vitesse de 115200 bauds.
    randomSeed(analogRead(0)); // Initialise le générateur de nombres aléatoires avec une valeur imprévisible lue sur une broche analogique non connectée.
    testLvgl();      // Appelle la fonction qui met en place toute l'interface graphique initiale.
    initMPU6050();   // Appelle la fonction qui configure et réveille le capteur MPU6050.
} // Fin de la fonction mySetup.

// Définit la fonction 'loop', qui s'exécute en continu après 'mySetup'.
void loop() {
    // Cette fonction est intentionnellement laissée vide dans ce projet.
    // Toute la logique du jeu est gérée par les 'timers' de LVGL (comme 'gameLoop').
    // Le framework qui utilise ce code est responsable d'appeler périodiquement 'lv_timer_handler()' pour que LVGL fonctionne.
} // Fin de la fonction loop.