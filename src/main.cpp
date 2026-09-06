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

// Factorisation du message d'aide pour le Bluetooth
const String STRING_AIDE_COMMANDES = "Commandes : RUN | PESEE | TARE | RESET | CONFIG | CIBLE=xxx | TIMEOUT=xx | ATTENTE=xx";

BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
String commandeRecue = "";

Preferences preferences;
HX711 scale1;
HX711 scale2;

// Variables globales modifiables en BLE
long offsetHX1 = 0;
long offsetHX2 = 0;
float poidsCibleG = DEFAULT_POIDS_CIBLE_G;
unsigned long timeoutDistribMs = DEFAULT_TIMEOUT_MS;
unsigned long attenteBtSec = DEFAULT_ATTENTE_BT_SEC;

// Compteurs globaux (A vie)
unsigned long nbDistributions = 0;
float poidsTotalDistribueG = 0.0;

// Compteurs partiels (Réinitialisables via RESET ou TARE)
unsigned long nbDistributionsPartiel = 0;
float poidsPartielDistribueG = 0.0;

// Prototype
void executerCycleDistribution();

// ==========================================
// CALLBACKS BLE
// ==========================================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
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

// Réinitialisation dédiée des compteurs partiels
void reinitialiserCompteursPartiels() {
  nbDistributionsPartiel = 0;
  poidsPartielDistribueG = 0.0;

  preferences.begin("calibration", false);
  preferences.putULong("nb_partiel", nbDistributionsPartiel);
  preferences.putFloat("tot_partiel", poidsPartielDistribueG);
  preferences.end();
}

// ==========================================
// FONCTIONS MOTEUR ET PESÉE
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
  return p1 + p2;
}

// ==========================================
// GESTION BLUETOOTH & COMMANDES
// ==========================================
void verifierCommandesBluetooth() {
  if (commandeRecue.length() > 0) {
    String commande = commandeRecue;
    commandeRecue = ""; 

    // 1. RUN
    if (commande.equalsIgnoreCase("RUN")) {
      logBT(">>> DEMANDE DE DISTRIBUTION MANUELLE (RUN) <<<");
      executerCycleDistribution();
    }
    // 2. TARE
    else if (commande.equalsIgnoreCase("TARE")) {
      logBT(">>> EXECUTION DE LA TARE (Trémie Vide) <<<");
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

      // Remise à zéro des compteurs partiels lors de la tare
      reinitialiserCompteursPartiels();
      logBT("SUCCES : Compteurs partiels reinitialises !");
    } 
    // 3. RESET (Réinitialisation manuelle du partiel)
    else if (commande.equalsIgnoreCase("RESET")) {
      reinitialiserCompteursPartiels();
      logBT(">>> SUCCES : Compteurs partiels reinitialises a zero ! <<<");
    }
    // 4. PESEE
    else if (commande.equalsIgnoreCase("PESEE")) {
      float grainRestantG = lirePoidsTotal(5);
      float poidsBrutG = scale1.get_value(5) + scale2.get_value(5);
      
      logBT("=== PESÉE EN TEMPS RÉEL ===");
      logBT("Poids TOTAL BRUT (avec machinerie) : " + String(poidsBrutG / 1000.0, 3) + " kg (" + String(poidsBrutG, 1) + " g)");
      logBT("Poids NET de grain restant        : " + String(grainRestantG / 1000.0, 3) + " kg (" + String(grainRestantG, 1) + " g)");
    } 
    // 5. CONFIG
    else if (commande.equalsIgnoreCase("CONFIG")) {
      logBT("=== CONFIGURATION ET STATISTIQUES ===");
      logBT("Poids cible       : " + String(poidsCibleG, 1) + " g");
      logBT("Timeout Distrib   : " + String(timeoutDistribMs / 1000) + " sec");
      logBT("Attente BLE Init  : " + String(attenteBtSec) + " sec");
      logBT("--- COMPTEURS PARTIELS (Dernier Reset) ---");
      logBT("Nb Distributions  : " + String(nbDistributionsPartiel));
      logBT("Total Extrait     : " + String(poidsPartielDistribueG / 1000.0, 3) + " kg (" + String(poidsPartielDistribueG, 1) + " g)");
      logBT("--- COMPTEURS GLOBAUX (A vie) ---");
      logBT("Nb Distributions  : " + String(nbDistributions));
      logBT("Total Extrait     : " + String(poidsTotalDistribueG / 1000.0, 3) + " kg (" + String(poidsTotalDistribueG, 1) + " g)");
    } 
    // 6. CIBLE=xxx
    else if (commande.startsWith("CIBLE=") || commande.startsWith("cible=")) {
      float nouvelleCible = commande.substring(6).toFloat();
      if (nouvelleCible > 0.0) {
        poidsCibleG = nouvelleCible;
        preferences.begin("calibration", false);
        preferences.putFloat("cible", poidsCibleG);
        preferences.end();
        logBT("SUCCES : Nouveau poids cible = " + String(poidsCibleG, 1) + " g");
      } else {
        logBT("ERREUR : Valeur cible invalide !");
      }
    } 
    // 7. TIMEOUT=xx
    else if (commande.startsWith("TIMEOUT=") || commande.startsWith("timeout=")) {
      int secondes = commande.substring(8).toInt();
      if (secondes > 0) {
        timeoutDistribMs = (unsigned long)secondes * 1000;
        preferences.begin("calibration", false);
        preferences.putULong("timeout", timeoutDistribMs);
        preferences.end();
        logBT("SUCCES : Nouveau Timeout = " + String(secondes) + " sec");
      } else {
        logBT("ERREUR : Valeur de timeout invalide !");
      }
    }
    // 8. ATTENTE=xxx
    else if (commande.startsWith("ATTENTE=") || commande.startsWith("attente=")) {
      int secondes = commande.substring(8).toInt();
      if (secondes >= 10) {
        attenteBtSec = (unsigned long)secondes;
        preferences.begin("calibration", false);
        preferences.putULong("attente_bt", attenteBtSec);
        preferences.end();
        logBT("SUCCES : Nouvelle attente BLE = " + String(attenteBtSec) + " sec");
      } else {
        logBT("ERREUR : Temps trop court (min 10s) !");
      }
    }
    else {
      logBT(STRING_AIDE_COMMANDES);
    }
  }
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

  while (millis() - chronoDebut < timeoutDistribMs) {
    float poidsActuel = lirePoidsTotal(1);
    grainDistribue = poidsInitialReference - poidsActuel;

    float pourcentage = (grainDistribue / poidsCibleG) * 100.0;
    if (pourcentage < 0) pourcentage = 0;

    logBT("Extrait : " + String(grainDistribue, 1) + "g / " + String(poidsCibleG, 1) + "g (" + String(pourcentage, 0) + "%) | Grain Restant : " + String(poidsActuel / 1000.0, 2) + "kg");

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

    // Mise à jour des compteurs (Globaux + Partiels)
    nbDistributions++;
    poidsTotalDistribueG += grainDistribue;
    
    nbDistributionsPartiel++;
    poidsPartielDistribueG += grainDistribue;

    // Sauvegarde en FLASH
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

    // Lecture brute (sans offset de tare) et lecture nette (avec offset de tare)
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

  // Initialisation BLE
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

  // Config Pins & PWM
  pinMode(PIN_AV_PLUS, OUTPUT);
  pinMode(PIN_AV_MOINS, OUTPUT);
  pinMode(PIN_AR_PLUS, OUTPUT);
  pinMode(PIN_AR_MOINS, OUTPUT);
  pinMode(PIN_RELAIS, OUTPUT);
  digitalWrite(PIN_RELAIS, LOW);
  stopperMoteur();

  analogWriteFrequency(PWM_FREQ);
  analogWriteResolution(PWM_RES);

  // Balances
  scale1.begin(HX1_DT, HX1_SCK);
  scale2.begin(HX2_DT, HX2_SCK);
  scale1.set_scale(CALIB_HX1);
  scale2.set_scale(CALIB_HX2);

  // Chargement FLASH
  preferences.begin("calibration", true);
  offsetHX1              = preferences.getLong("off1", 0);
  offsetHX2              = preferences.getLong("off2", 0);
  poidsCibleG            = preferences.getFloat("cible", DEFAULT_POIDS_CIBLE_G);
  timeoutDistribMs       = preferences.getULong("timeout", DEFAULT_TIMEOUT_MS);
  attenteBtSec           = preferences.getULong("attente_bt", DEFAULT_ATTENTE_BT_SEC);
  nbDistributions        = preferences.getULong("nb_distrib", 0);
  poidsTotalDistribueG   = preferences.getFloat("tot_poids", 0.0);
  nbDistributionsPartiel = preferences.getULong("nb_partiel", 0);
  poidsPartielDistribueG = preferences.getFloat("tot_partiel", 0.0);
  preferences.end();

  scale1.set_offset(offsetHX1);
  scale2.set_offset(offsetHX2);

  // Affichage des paramètres & statistiques au démarrage USB
  Serial.println("\n--- CHARGEMENT PARAMÈTRES & STATISTIQUES ---");
  Serial.printf("Offsets : Off1=%ld | Off2=%ld\n", offsetHX1, offsetHX2);
  Serial.printf("Parametres : Cible=%.1fg | Timeout=%lums | AttenteBT=%lus\n", poidsCibleG, timeoutDistribMs, attenteBtSec);
  Serial.printf("Partiel : %lu distrib. | Total : %.3f kg\n", nbDistributionsPartiel, poidsPartielDistribueG / 1000.0);
  Serial.printf("Global  : %lu distrib. | Total : %.3f kg\n", nbDistributions, poidsTotalDistribueG / 1000.0);

  // --- FENÊTRE DE CONFIGURATION BLE AVANT CYCLE D'ALLUMAGE ---
  Serial.printf("\n>>> Attente de %lu sec : Connectez votre iPhone 13 en BLE...\n", attenteBtSec);
  
  unsigned long chronoAttente = millis();
  unsigned long dernierAffichage = 0;

  while (millis() - chronoAttente < (attenteBtSec * 1000)) {
    verifierCommandesBluetooth();

    if (millis() - dernierAffichage >= 10000) {
      unsigned long resteSec = attenteBtSec - ((millis() - chronoAttente) / 1000);
      Serial.printf("Temps BLE restant : %lu sec...\n", resteSec);
      dernierAffichage = millis();
    }
    delay(50);
  }

  // --- PREMIER CYCLE AUTOMATIQUE DU DÉMARRAGE ---
  executerCycleDistribution();
}

void loop() {
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  verifierCommandesBluetooth();
  delay(100);
}
