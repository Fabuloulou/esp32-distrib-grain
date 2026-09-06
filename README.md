# 🌾 Distributeur de Grain Automatique ESP32

Ce projet est un système embarqué sur **ESP32** conçu pour automatiser la distribution mesurée de grain. Il contrôle un moteur continu (détassage, extraction et débourrage automatique), mesure la quantité distribuée en temps réel via deux jauges de contrainte HX711, et communique via **Bluetooth Low Energy (BLE)**.

---

## 📸 Fonctionnalités Principales

* **Dosage de précision** : Mesure dynamique de la perte de poids via deux capteurs HX711.
* **Séquence intelligente** :
  1. Détassage du grain (marche arrière courte).
  2. Pause et mesure de référence initiales.
  3. Extraction progressive du grain (PWM régulé).
  4. Débourrage automatique (10s de marche arrière) + alarme relais en cas d'échec / timeout.
* **Double système de compteurs (Sauvegardés en Flash NVS)** :
  * **Compteurs Globaux** : Historique cumulé à vie (non réinitialisable par simple commande).
  * **Compteurs Partiels** : Nombre de cycles et masse totale extraite réinitialisables (via `TARE` ou `RESET`).
* **Supervision Bluetooth BLE** : Envoi de commandes en temps réel et réception des journaux via le profil série Nordic UART.
* **Affichage des poids à la fin du cycle** :
  * Poids **brut total** (avec trémie/machinerie).
  * Poids **net de grain restant**.

---

## 🛠️ Schéma Matériel & Recommandations

* **Microcontrôleur** : ESP32 (ex: ESP32-WROOM-32)
* **Amplificateurs de pesée** : 2x HX711
* **Driver Moteur** : Pont en H compatible PWM
* **Actuateur** : Relais pour lampe de défaut / alarme

---

### **Spécifications Hardware & Connexions GPIO**

* **Microcontrôleur :** ESP32 DEVKIT V1 30 broches (Standard DOIT / NodeMCU 30 pins)[cite: 4, 5, 6].
* **Alimentation :** L'ESP32 est alimenté par son port USB pour la programmation (hors tension lors des manipulations)[cite: 5]. Le reste du circuit fonctionne sous 12 V DC (moteur, relais) et 3,3 V DC (capteurs, optocoupleurs)[cite: 6].

#### **1. Pont en H MOSFET (Commande Moteur 12V)**
Pilotage via optocoupleurs PC817[cite: 6]. *Des résistances de pulldown de 10 kΩ maintiennent les LED des optocoupleurs à la masse au repos pour garantir la sécurité[cite: 6].*

| Signal / Rôle | Type MOSFET | Transistor / Direction | GPIO ESP32 |
| :--- | :--- | :--- | :--- |
| **AV+** | P-MOS (IRF5210) | Haut Gauche (Rotation Avant, +) | **GPIO18**[cite: 6] |
| **AV-** | N-MOS (IRFZ44N) | Bas Droit (Rotation Avant, -) | **GPIO17**[cite: 6] |
| **AR+** | P-MOS (IRF5210) | Haut Droit (Rotation Arrière, +) | **GPIO16**[cite: 6] |
| **AR-** | N-MOS (IRFZ44N) | Bas Gauche (Rotation Arrière, -) | **GPIO19**[cite: 6] |

* **PWM :** Appliqué sur les bras bas (N-MOS) à une fréquence maximale de **500 Hz** (résolution 8 bits) afin de respecter le temps de réponse des optocoupleurs PC817[cite: 6].

#### **2. Modules de Pesée (HX711 & Jauges 20 kg x2)**
Alimentés en 3,3 V DC[cite: 6].
* **Module 1 (HX1) :** Data (DT) sur **GPIO25**, Horloge (SCK) sur **GPIO26**[cite: 4, 6, 7].
* **Module 2 (HX2) :** Data (DT) sur **GPIO27**, Horloge (SCK) sur **GPIO33**[cite: 4, 6, 7].
* **Calibration (Étalonné à 26 kg) :** `CALIB_HX1 = 102.5f`, `CALIB_HX2 = 102.07f`[cite: 6, 7].

#### **3. Entrées / Sorties Auxiliaires**
* **Sortie Alarme Relais (Lampe témoin 12V) :** **GPIO23** (Commande via PC817 + transistor BS170, actif à l'état HIGH)[cite: 6].
* **Entrée Sélecteur E1 :** **GPIO34** (Input only, commutée à 3,3 V via optocoupleur)[cite: 6].
* **Entrée Sélecteur E2 :** **GPIO35** (Input only, commutée à 3,3 V via optocoupleur)[cite: 6].
* **Réservation Écran :** **GPIO21** (SDA) et **GPIO22** (SCL) restent dédiés au bus I2C (ex: écran SH1106)[cite: 4].

---

## 💬 Commandes Bluetooth (BLE)

L'ESP32 communique sous le nom **`DISTRIBUTEUR-GRAIN`**. Vous pouvez utiliser une application mobile BLE (ex: *Serial Bluetooth Terminal*, *nRF Connect*) pour envoyer les commandes suivantes :

| Commande | Action |
| :--- | :--- |
| `DISTRIBUTE` | Lance manuellement un cycle complet de distribution. |
| `EMPTY` | Vide entièrement la trémie (arrête si le poids varie de moins de X grammes sur l'intervalle configuré). |
| `WEIGH` | Affiche en temps réel le poids brut (avec machinerie) et le poids net (grain seul). |
| `ZERO` | Recalibre le zéro de la trémie vide, enregistre les offsets en FLASH et réinitialise les compteurs partiels. |
| `CLEAR` | Réinitialise uniquement les compteurs partiels (nombre de tirages + poids partiel) sans modifier la tare. |
| `SHOW` | Affiche les paramètres actuels (cible, timeout, seuils) et tous les compteurs (partiels et globaux). |
| `SET_TARGET=xxx` | Définit le poids cible en grammes (ex: `SET_TARGET=500.0`). |
| `SET_TIMEOUT=xx` | Définit le temps maximal d'extraction en secondes (ex: `SET_TIMEOUT=30`). |
| `SET_EMPTY_THRESHOLD=xxx` | Définit le seuil de variation pour le vidage en grammes (ex: `SET_EMPTY_THRESHOLD=100.0`). |
| `SET_EMPTY_INTERVAL=xx` | Définit le temps entre deux mesures lors du vidage en secondes (ex: `SET_EMPTY_INTERVAL=30`). |
---

## 📂 Structure du Projet

```text
├── src/
│   ├── config.h         # Déclarations des broches, paramètres d'usine & fréquences PWM
│   └── main.cpp         # Code principal ESP32
├── platformio.ini       # Configuration de l'environnement PlatformIO
└── README.md            # Documentation du projet
