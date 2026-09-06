#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// CONFIGURATION DES PINS
// ==========================================
const int PIN_AV_PLUS  = 18; // GPIO18 - P-MOS Avant +
const int PIN_AV_MOINS = 17; // GPIO17 - N-MOS Avant -
const int PIN_AR_PLUS  = 16; // GPIO16 - P-MOS Arrière +
const int PIN_AR_MOINS = 19; // GPIO19 - N-MOS Arrière -

const int PIN_RELAIS   = 23; // GPIO23 - Lampe Alarme / Défaut

// HX711 - Capteurs de poids
const int HX1_DT  = 25;
const int HX1_SCK = 26;
const int HX2_DT  = 27;
const int HX2_SCK = 33;

// Calibration des jauges (Gain/Pente)
const float CALIB_HX1 = 102.5f;
const float CALIB_HX2 = 102.07f;

// VALEURS PAR DÉFAUT
const float DEFAULT_POIDS_CIBLE_G          = 150.0;     // 150 grammes
const unsigned long DEFAULT_TIMEOUT_MS     = 60000;     // 60 secondes
const float DEFAULT_EMPTY_THRESHOLD_G      = 100.0;     // Seuil de tolérance pour EMPTY (100g)
const unsigned long DEFAULT_EMPTY_INTERVAL_SEC = 30;    // Temps entre 2 mesures pour EMPTY (30s)

// Paramètres PWM
const int PWM_FREQ = 500;
const int PWM_RES  = 8;
const int PWM_DEMI_VITESSE = 128;

#endif
