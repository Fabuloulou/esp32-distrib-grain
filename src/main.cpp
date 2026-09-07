#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "HX711.h"
#include "config.h"

// UUIDs standards pour le service Série BLE (Nordic UART Service)
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

const String STRING_AIDE_COMMANDES = "Commandes : DISTRIBUTE | EMPTY | WEIGH | ZERO | CLEAR | SHOW | SET_TARGET=x | SET_TIMEOUT=x | SET_EMPTY_THRESHOLD=x | SET_EMPTY_INTERVAL=x";

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool nouvelleConnexionBLE = false;
String commandeRecue = "";

Preferences preferences;
HX711 scale1;
HX711 scale2;

// Variables globales modifiables en BLE
long offsetHX1 = 0;
long offsetHX2 = 0;
float poidsCibleG = DEFAULT_POIDS_CIBLE_G;
unsigned long timeoutDistribMs = DEFAULT_TIMEOUT_MS;
float seuilEmptyG = DEFAULT_EMPTY_THRESHOLD_G;
unsigned long delaiEmptySec = DEFAULT_EMPTY_INTERVAL_SEC;

// Compteurs globaux (À vie)
unsigned long nbDistributions = 0;
float poidsTotalDistribueG = 0.0;

// Compteurs partiels (Réinitialisables via CLEAR ou ZERO)
unsigned long nbDistributionsPartiel = 0;
float poidsPartielDistribueG = 0.0;

// Prototypes
void executerCycleDistribution();
void viderTremie();
void envoyerResumeBluetooth();

// ==========================================
// CALLBACKS BLE
// ==========================================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      nouvelleConnexionBLE = true;
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        commandeRecue = rxValue;
        commandeRecue.trim();
      }
    }
};

void logBT(String msg) {
  Serial.println(msg);
  if (deviceConnected && pTxCharacteristic != NULL) {
    String msgBT = msg + "\n";
    pTxCharacteristic->setValue(msgBT.c_str());
    pTxCharacteristic->notify();
    delay(20);
  }
}

void reinitialiserCompteursPartiels() {
  nbDistributionsPartiel = 0;
  poidsPartielDistribueG = 0.0;

  preferences.begin("calibration", false);
  preferences.putULong("nb_partiel", nbDistributionsPartiel);
  preferences.putFloat("tot_partiel", poidsPartielDistribueG);
  preferences.end();
}

// ==========================================
// FONCTIONS MOTEUR ET PESÉE SÉCURISÉE
// ==========================================
void stopperMoteur() {
  analogWrite(PIN_AV_PLUS, 0);
  analogWrite(PIN_AV_MOINS, 0);
  analogWrite(PIN_AR_PLUS, 0);
  analogWrite(PIN_AR_MOINS, 0);

  pinMode(PIN_AV_PLUS, OUTPUT);
  pinMode(PIN_AV_MOINS, OUTPUT);
  pinMode(PIN_AR_PLUS, OUTPUT);
  pinMode(PIN_AR_MOINS, OUTPUT);

  digitalWrite(PIN_AV_PLUS, LOW);
  digitalWrite(PIN_AV_MOINS, LOW);
  digitalWrite(PIN_AR_PLUS, LOW);
  digitalWrite(PIN_AR_MOINS, LOW);
  delay(100);
}

void moteurAvant(uint8_t vitesse) {
  stopperMoteur();
  digitalWrite(PIN_AV_PLUS, HIGH);
  analogWrite(PIN_AV_MOINS, vitesse);
}

void moteurArriere(uint8_t vitesse) {
  stopperMoteur();
  digitalWrite(PIN_AR_PLUS, HIGH);
  analogWrite(PIN_AR_MOINS, vitesse);
}

float lirePoidsTotal(uint8_t lectures = 3) {
  float p1 = scale1.get_units(lectures);
  float p2 = scale2.get_units(lectures);
  float total = p1 + p2;

  // Filtrage anti-aberration : re-synchro si valeur folle
  if (total > 30000.0 || total < -5000.0) {
    Serial.printf("[AVERTISSEMENT] Pesée aberrante (%.1fg). Re-synchro HX711...\n", total);
    
    scale1.power_down();
    scale2.power_down();
    delay(10);
    scale1.power_up();
    scale2.power_up();
    
    p1 = scale1.get_units(lectures);
    p2 = scale2.get_units(lectures);
    total = p1 + p2;
  }

  return total;
}

// ==========================================
// ENVOI DU RÉSUMÉ BLE À LA CONNEXION
// ==========================================
void envoyerResumeBluetooth() {
  float grainRestantG = lirePoidsTotal(5);
  
  logBT("\n==========================================");
  logBT("    CONNECTÉ AU DISTRIBUTEUR DE GRAIN     ");
  logBT("==========================================");
  logBT("--- PARAMÈTRES ACTUELS ---");
  logBT("Poids cible       : " + String(poidsCibleG, 1) + " g");
  logBT("Timeout Max       : " + String(timeoutDistribMs / 1000) + " sec");
  logBT("Seuil EMPTY       : " + String(seuilEmptyG, 1) + " g");
  logBT("Intervalle EMPTY  : " + String(delaiEmptySec) + " sec");
  logBT("Grain disponible  : " + String(grainRestantG / 1000.0, 3) + " kg (" + String(grainRestantG, 1) + " g)");
  logBT("--- COMPTEURS PARTIELS ---");
  logBT("Distributions     : " + String(nbDistributionsPartiel));
  logBT("Total Extrait     : " + String(poidsPartielDistribueG / 1000.0, 3) + " kg");
  logBT("--- COMPTEURS GLOBAUX ---");
  logBT("Distributions     : " + String(nbDistributions));
  logBT("Total Extrait     : " + String(poidsTotalDistribueG / 1000.0, 3) + " kg");
  logBT("------------------------------------------");
  logBT(STRING_AIDE_COMMANDES);
  logBT("------------------------------------------\n");
}

// ==========================================
// GESTION BLUETOOTH & COMMANDES
// ==========================================
void verifierCommandesBluetooth() {
  if (commandeRecue.length() > 0) {
    String cmd = commandeRecue;
    commandeRecue = ""; 

    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "DISTRIBUTE") {
      logBT(">>> DEMANDE DE DISTRIBUTION MANUELLE (DISTRIBUTE) <<<");
      executerCycleDistribution();
    }
    else if (cmd == "EMPTY") {
      logBT(">>> DEMANDE DE VIDAGE DE LA TRÉMIE (EMPTY) <<<");
      viderTremie();
    }
    else if (cmd == "ZERO") {
      logBT(">>> EXECUTION DE LA TARE (ZERO) <<<");
      scale1.tare(10);
      scale2.tare(10);

      offsetHX1 = scale1.get_offset();
      offsetHX2 = scale2.get_offset();

      preferences.begin("calibration", false);
      preferences.putLong("off1", offsetHX1);
      preferences.putLong("off2", offsetHX2);
      preferences.end();

      logBT("SUCCES : Tare enregistree en FLASH !");
      logBT("Offset 1: " + String(offsetHX1) + " | Offset 2: " + String(offsetHX2));

      reinitialiserCompteursPartiels();
      logBT("SUCCES : Compteurs partiels reinitialises !");
    } 
    else if (cmd == "CLEAR") {
      reinitialiserCompteursPartiels();
      logBT(">>> SUCCES : Compteurs partiels reinitialises a zero (CLEAR) ! <<<");
    }
    else if (cmd == "WEIGH") {
      float grainRestantG = lirePoidsTotal(5);
      float poidsBrutG = scale1.get_value(5) + scale2.get_value(5);
      
      logBT("=== PESÉE EN TEMPS RÉEL ===");
      logBT("Poids TOTAL BRUT (avec machinerie) : " + String(poidsBrutG / 1000.0, 3) + " kg (" + String(poidsBrutG, 1) + " g)");
      logBT("Poids NET de grain restant        : " + String(grainRestantG / 1000.0, 3) + " kg (" + String(grainRestantG, 1) + " g)");
    } 
    else if (cmd == "SHOW") {
      logBT("=== CONFIGURATION ET STATISTIQUES ===");
      logBT("Poids cible       : " + String(poidsCibleG, 1) + " g");
      logBT("Timeout Distrib   : " + String(timeoutDistribMs / 1000) + " sec");
      logBT("Seuil EMPTY       : " + String(seuilEmptyG, 1) + " g");
      logBT("Intervalle EMPTY  : " + String(delaiEmptySec) + " sec");
      logBT("--- COMPTEURS PARTIELS ---");
      logBT("Nb Distributions  : " + String(nbDistributionsPartiel));
      logBT("Total Extrait     : " + String(poidsPartielDistribueG / 1000.0, 3) + " kg");
      logBT("--- COMPTEURS GLOBAUX ---");
      logBT("Nb Distributions  : " + String(nbDistributions));
      logBT("Total Extrait     : " + String(poidsTotalDistribueG / 1000.0, 3) + " kg");
    } 
    else if (cmd.startsWith("SET_TARGET")) {
      int indexEgal = cmd.indexOf('=');
      if (indexEgal != -1) {
        float val = cmd.substring(indexEgal + 1).toFloat();
        if (val > 0.0) {
          poidsCibleG = val;
          preferences.begin("calibration", false);
          preferences.putFloat("cible", poidsCibleG);
          preferences.end();
          logBT("SUCCES : Nouveau poids cible = " + String(poidsCibleG, 1) + " g");
        } else logBT("ERREUR : Valeur cible invalide !");
      }
    } 
    else if (cmd.startsWith("SET_TIMEOUT")) {
      int indexEgal = cmd.indexOf('=');
      if (indexEgal != -1) {
        int sec = cmd.substring(indexEgal + 1).toInt();
        if (sec > 0) {
          timeoutDistribMs = (unsigned long)sec * 1000;
          preferences.begin("calibration", false);
          preferences.putULong("timeout", timeoutDistribMs);
          preferences.end();
          logBT("SUCCES : Nouveau Timeout = " + String(sec) + " sec");
        } else logBT("ERREUR : Valeur invalide !");
      }
    }
    else if (cmd.startsWith("SET_EMPTY_THRESHOLD")) {
      int indexEgal = cmd.indexOf('=');
      if (indexEgal != -1) {
        float val = cmd.substring(indexEgal + 1).toFloat();
        if (val > 0.0) {
          seuilEmptyG = val;
          preferences.begin("calibration", false);
          preferences.putFloat("empty_th", seuilEmptyG);
          preferences.end();
          logBT("SUCCES : Seuil EMPTY = " + String(seuilEmptyG, 1) + " g");
        } else logBT("ERREUR : Valeur invalide !");
      }
    }
    else if (cmd.startsWith("SET_EMPTY_INTERVAL")) {
      int indexEgal = cmd.indexOf('=');
      if (indexEgal != -1) {
        int sec = cmd.substring(indexEgal + 1).toInt();
        if (sec > 0) {
          delaiEmptySec = (unsigned long)sec;
          preferences.begin("calibration", false);
          preferences.putULong("empty_int", delaiEmptySec);
          preferences.end();
          logBT("SUCCES : Intervalle EMPTY = " + String(delaiEmptySec) + " sec");
        } else logBT("ERREUR : Valeur invalide !");
      }
    }
    else {
      logBT(STRING_AIDE_COMMANDES);
    }
  }
}

// ==========================================
// FONCTION DE VIDAGE DE LA TRÉMIE (EMPTY)
// ==========================================
void viderTremie() {
  digitalWrite(PIN_RELAIS, LOW);

  logBT("\n==========================================");
  logBT("--- DEBUT DU VIDAGE DE LA TRÉMIE ---");
  logBT("==========================================");

  float poidsInitial = lirePoidsTotal(5);
  logBT("Poids initial estime : " + String(poidsInitial, 1) + " g");
  logBT("Parametres : Seuil = " + String(seuilEmptyG, 1) + "g | Intervalle = " + String(delaiEmptySec) + "s");

  // Phase 1 : Rotation inconditionnelle du moteur pendant 15 secondes
  logBT("Étape 1 : Démarrage moteur pendant 15 secondes...");
  moteurAvant(PWM_DEMI_VITESSE);
  
  unsigned long chrono15s = millis();
  bool interrompu = false;
  
  while (millis() - chrono15s < 15000) {
    if (commandeRecue.length() > 0) {
      logBT("Vidage interrompu par l'utilisateur pendant les 15s initiales.");
      interrompu = true;
      break;
    }
    delay(200);
  }

  // Phase 2 : Surveillance du poids après les 15s initiales
  if (!interrompu) {
    logBT("Étape 2 : Surveillance active de l'évolution du poids...");
    float dernierPoids = lirePoidsTotal(3);
    unsigned long chronoDerniereMesure = millis();

    while (true) {
      if (millis() - chronoDerniereMesure >= (delaiEmptySec * 1000)) {
        chronoDerniereMesure = millis();
        float poidsActuel = lirePoidsTotal(3);
        float deltaPoids = abs(dernierPoids - poidsActuel);

        logBT("Grain restant : " + String(poidsActuel, 1) + " g (Variation sur " + String(delaiEmptySec) + "s : " + String(deltaPoids, 1) + " g)");

        // Seuil de tolérance atteint (ex: variation < 100g)
        if (deltaPoids < seuilEmptyG) {
          logBT("Vidage termine : variation inferieure au seuil de " + String(seuilEmptyG, 1) + " g sur " + String(delaiEmptySec) + "s.");
          break;
        }

        // Trémie vide (<= 0g)
        if (poidsActuel <= 0.0) {
          logBT("Poids net <= 0g atteint.");
          break;
        }

        dernierPoids = poidsActuel;
      }

      if (commandeRecue.length() > 0) {
        logBT("Vidage interrompu par l'utilisateur.");
        break;
      }

      delay(200);
    }
  }

  // STOP SÉCURISÉ
  stopperMoteur();

  float poidsFinal = lirePoidsTotal(5);
  float totalEvacue = poidsInitial - poidsFinal;
  if (totalEvacue < 0) totalEvacue = 0;

  logBT("\n==========================================");
  logBT("--- VIDAGE TERMINÉ (STOP SÉCURISÉ) ---");
  logBT("Total evacue : " + String(totalEvacue, 1) + " g");
  logBT("Grain restant: " + String(poidsFinal, 1) + " g");
  logBT("==========================================\n");
}

// ==========================================
// FONCTION DE SÉQUENCE DE DISTRIBUTION
// ==========================================
void executerCycleDistribution() {
  digitalWrite(PIN_RELAIS, LOW);

  logBT("\n==========================================");
  logBT("--- LANCEMENT DU CYCLE DE DISTRIBUTION ---");
  logBT("==========================================");

  logBT("Etape 1 : Detassage du grain (Marche AR 2s)...");
  moteurArriere(255);
  delay(2000);

  logBT("Etape 2 : Pause et mesure de reference...");
  stopperMoteur();
  delay(2000);

  float poidsInitialReference = lirePoidsTotal(5);
  logBT("-> Grain disponible dans la tremie : " + String(poidsInitialReference / 1000.0, 2) + " kg (" + String(poidsInitialReference, 1) + " g)");

  logBT("Etape 3 : Distribution en cours...");
  moteurAvant(PWM_DEMI_VITESSE);

  unsigned long chronoDebut = millis();
  bool distributionReussie = false;
  float grainDistribue = 0.0;
  int cpt = 0;

  while (millis() - chronoDebut < timeoutDistribMs) {
    cpt++;
    float poidsActuel = lirePoidsTotal(1);
    grainDistribue = poidsInitialReference - poidsActuel;

    float pourcentage = (grainDistribue / poidsCibleG) * 100.0;
    if (pourcentage < 0) pourcentage = 0;

    logBT("[" + String(cpt) + "] Extrait : " + String(grainDistribue, 1) + "g / " + String(poidsCibleG, 1) + "g (" + String(pourcentage, 0) + "%) | Grain Restant : " + String(poidsActuel / 1000.0, 2) + "kg");

    if (grainDistribue >= poidsCibleG) {
      distributionReussie = true;
      break;
    }
    delay(500);
  }

  stopperMoteur();
  unsigned long dureeDistributionMs = millis() - chronoDebut;

  logBT("\n==========================================");
  logBT("--- BILAN DU CYCLE ---");
  logBT("==========================================");

  if (distributionReussie) {
    float debitGparSec = grainDistribue / (dureeDistributionMs / 1000.0);

    nbDistributions++;
    poidsTotalDistribueG += grainDistribue;
    
    nbDistributionsPartiel++;
    poidsPartielDistribueG += grainDistribue;

    preferences.begin("calibration", false);
    preferences.putULong("nb_distrib", nbDistributions);
    preferences.putFloat("tot_poids", poidsTotalDistribueG);
    preferences.putULong("nb_partiel", nbDistributionsPartiel);
    preferences.putFloat("tot_partiel", poidsPartielDistribueG);
    preferences.end();

    logBT("STATUT            : SUCCES ");
    logBT("Quantite extraite : " + String(grainDistribue, 1) + " g (Cible: " + String(poidsCibleG, 1) + " g)");
    logBT("Temps d'extraction: " + String(dureeDistributionMs / 1000.0, 1) + " sec");
    logBT("Debit moyen       : " + String(debitGparSec, 1) + " g/sec");

    float poidsBrutTotal = scale1.get_value(5) + scale2.get_value(5);
    float grainFinal = lirePoidsTotal(5);

    logBT("\n--- ÉTAT DE LA TRÉMIE EN FIN DE CYCLE ---");
    logBT("Poids TOTAL BRUT (avec machinerie) : " + String(poidsBrutTotal / 1000.0, 3) + " kg (" + String(poidsBrutTotal, 1) + " g)");
    logBT("Poids NET de grain restant        : " + String(grainFinal / 1000.0, 3) + " kg (" + String(grainFinal, 1) + " g)");

    logBT("\n--- CUMUL PARTIEL (Depuis dernier Reset/Tare) ---");
    logBT("Distributions partiel   : " + String(nbDistributionsPartiel));
    logBT("Total partiel           : " + String(poidsPartielDistribueG / 1000.0, 3) + " kg (" + String(poidsPartielDistribueG, 1) + " g)");

    logBT("\n--- CUMUL HISTORIQUE (A vie) ---");
    logBT("Distributions totales   : " + String(nbDistributions));
    logBT("Total distribue         : " + String(poidsTotalDistribueG / 1000.0, 3) + " kg (" + String(poidsTotalDistribueG, 1) + " g)");

  } else {
    logBT("STATUT            : ECHEC / TIMEOUT DEPASSE !");
    logBT("Quantite extraite : " + String(grainDistribue, 1) + " g sur " + String(poidsCibleG, 1) + " g cibles");
    logBT("Lancement de la procedure de debourrage (10s Marche AR)...");
    
    delay(2000);
    moteurArriere(255);
    delay(10000);
    stopperMoteur();

    digitalWrite(PIN_RELAIS, HIGH);
    logBT("Lampe de defaut activee.");
  }

  logBT("\n--------------------------------------------------");
  logBT("Cycle termine. En attente de commande Bluetooth.");
  logBT(STRING_AIDE_COMMANDES);
  logBT("--------------------------------------------------");
}

// ==========================================
// DÉROULEMENT PRINCIPAL
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== Distributeur de Grain (BLE iOS / Android) ===");

  BLEDevice::init("DISTRIBUTEUR-GRAIN");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();

  pinMode(PIN_AV_PLUS, OUTPUT);
  pinMode(PIN_AV_MOINS, OUTPUT);
  pinMode(PIN_AR_PLUS, OUTPUT);
  pinMode(PIN_AR_MOINS, OUTPUT);
  pinMode(PIN_RELAIS, OUTPUT);
  digitalWrite(PIN_RELAIS, LOW);
  stopperMoteur();

  analogWriteFrequency(PWM_FREQ);
  analogWriteResolution(PWM_RES);

  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  scale1.set_scale(CALIB_HX1);
  scale2.set_scale(CALIB_HX2);

  preferences.begin("calibration", true);
  offsetHX1              = preferences.getLong("off1", 0);
  offsetHX2              = preferences.getLong("off2", 0);
  poidsCibleG            = preferences.getFloat("cible", DEFAULT_POIDS_CIBLE_G);
  timeoutDistribMs       = preferences.getULong("timeout", DEFAULT_TIMEOUT_MS);
  seuilEmptyG            = preferences.getFloat("empty_th", DEFAULT_EMPTY_THRESHOLD_G);
  delaiEmptySec          = preferences.getULong("empty_int", DEFAULT_EMPTY_INTERVAL_SEC);
  nbDistributions        = preferences.getULong("nb_distrib", 0);
  poidsTotalDistribueG   = preferences.getFloat("tot_poids", 0.0);
  nbDistributionsPartiel = preferences.getULong("nb_partiel", 0);
  poidsPartielDistribueG = preferences.getFloat("tot_partiel", 0.0);
  preferences.end();

  scale1.set_offset(offsetHX1);
  scale2.set_offset(offsetHX2);

  Serial.println("\n--- CHARGEMENT PARAMÈTRES & STATISTIQUES ---");
  Serial.printf("Offsets : Off1=%ld | Off2=%ld\n", offsetHX1, offsetHX2);
  Serial.printf("Parametres : Cible=%.1fg | Timeout=%lums | SeuilEmpty=%.1fg | IntEmpty=%lus\n", 
                poidsCibleG, timeoutDistribMs, seuilEmptyG, delaiEmptySec);

  Serial.println("\n>>> DISTRIBUTION AUTOMATIQUE DU DÉMARRAGE <<<");
  executerCycleDistribution();

  Serial.println("\nPrêt ! En attente de commande BLE...");
}

void loop() {
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  
  // Nouvelle détection de connexion avec temporisation
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;

    delay(5000); 

    logBT("=== CONNECTE AU DISTRIBUTEUR ===");
    envoyerResumeBluetooth();
  }

  verifierCommandesBluetooth();
  delay(100);
}
