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

## 💬 Commandes Bluetooth (BLE)

L'ESP32 communique sous le nom **`DISTRIBUTEUR-GRAIN`**. Vous pouvez utiliser une application mobile BLE (ex: *Serial Bluetooth Terminal*, *nRF Connect*) pour envoyer les commandes suivantes :

| Commande | Action |
| :--- | :--- |
| `RUN` | Lance manuellement un cycle complet de distribution. |
| `PESEE` | Affiche en temps réel le poids brut (avec machinerie) et le poids net (grain seul). |
| `TARE` | Recalibre le zéro de la trémie vide, enregistre les offsets en FLASH et réinitialise les compteurs partiels. |
| `RESET` | Réinitialise uniquement les compteurs partiels (nombre de tirages + poids partiel) sans modifier la tare. |
| `CONFIG` | Affiche les paramètres actuels (cible, timeout, attente) et tous les compteurs (partiels et globaux). |
| `CIBLE=xxx` | Définit le poids cible en grammes (ex: `CIBLE=500.0`). |
| `TIMEOUT=xx` | Définit le temps maximal d'extraction en secondes (ex: `TIMEOUT=30`). |
| `ATTENTE=xx` | Définit la fenêtre d'attente BLE au démarrage en secondes (ex: `ATTENTE=20`). |

---

## 📂 Structure du Projet

```text
├── src/
│   ├── config.h         # Déclarations des broches, paramètres d'usine & fréquences PWM
│   └── main.cpp         # Code principal ESP32
├── platformio.ini       # Configuration de l'environnement PlatformIO
└── README.md            # Documentation du projet
