/*
 * Automatisches KH-Messgerät mit Säuretitration
 * ESP32-S3 + DFRobot Gravity pH-Sensor + LittleFS
 *
 * Hardware:
 * - ESP32-S3 UN-O N16R8 (16MB Flash, 8MB PSRAM)
 * - DFRobot Gravity pH-Sensor (Analog)
 * - 3x Stepper-Motor Dosierpumpen (STEP/DIR)
 * - Magnetrührer
 *
 * Features:
 * - Vollautomatische KH-Messung via Säuretitration
 * - Dynamische kontinuierliche Titration (exponentiell interpoliert)
 * - Getauchte Spritze für direkte Säurezufuhr
 * - Web-UI mit WiFi-Konfiguration
 * - LittleFS Datenspeicherung
 * - NTP-Zeitsynchronisation
 */

#include <Arduino.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>

// PSRAM Allocator für ArduinoJson (nutzt 8MB externen Speicher)
struct PsramAllocator {
  void* allocate(size_t size) {
    if (psramFound()) {
      return ps_malloc(size);
    }
    return malloc(size);  // Fallback auf normalen Heap
  }
  void deallocate(void* pointer) {
    free(pointer);
  }
  void* reallocate(void* ptr, size_t new_size) {
    if (psramFound()) {
      return ps_realloc(ptr, new_size);
    }
    return realloc(ptr, new_size);
  }
};

// JsonDocument das PSRAM nutzt (für große Dokumente wie Messungen)
using PsramJsonDocument = BasicJsonDocument<PsramAllocator>;

// KRITISCH: FastAccelStepper MUSS vor WiFi inkludiert werden!
// Verhindert "Cache disabled but cached memory region accessed" Fehler
#define FASACCELSTEPPER_ISR_IN_IRAM  // ISR-Code in IRAM statt Flash
#include "FastAccelStepper.h"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <time.h>
#include <LittleFS.h>
#include <Wire.h>
#include <RTClib.h>
#include <Update.h>      // Web OTA Updates
#include <ArduinoOTA.h>  // Arduino IDE OTA Updates

// ==================== PIN DEFINITIONEN (ESP32-S3) ====================
// WICHTIG: ESP32-S3 hat andere ADC-Pins als ESP32!
// ADC1: GPIO1-10 (bevorzugt für WiFi-Betrieb)
// ADC2: GPIO11-20 (kann Konflikte mit WiFi haben - VERMEIDEN!)

// pH-Sensor (ADC1 - sicher mit WiFi)
#define PH_PIN 4               // GPIO4 = ADC1_CH3 (analog-fähig, sicher mit WiFi)

// Stepper-Motoren (gemeinsame STEP/DIR Pins)
#define STEP_PIN 12            // GPIO12 - Gemeinsamer STEP Pin für alle Stepper
#define DIR_PIN 13             // GPIO13 - Gemeinsamer DIR Pin für alle Stepper

// Enable Pins für die 3 Pumpen (GPIO mit Pull-up/down Widerständen)
#define ENABLE_SAMPLE 17       // GPIO17 - Enable Pin Probenwasser-Pumpe
#define ENABLE_REAGENT 18      // GPIO18 - Enable Pin Säure-Pumpe
#define ENABLE_RINSE 8         // GPIO8  - Enable Pin Spül-Pumpe

// Sensoren & Aktoren
#define STIRRER_PIN 6          // GPIO6 - Magnetrührer PWM (PWM-gesteuert)

// PWM-Konfiguration für Rührer (ESP32-S3 LEDC)
#define STIRRER_PWM_CHANNEL 0      // LEDC Kanal 0 (0-15 verfügbar)
#define STIRRER_PWM_FREQ_DEFAULT 25000  // Default PWM Frequenz (25kHz = unhörbar)
#define STIRRER_PWM_RESOLUTION 8   // 8-bit Auflösung (0-255)
#define STIRRER_MIN_DUTY 60        // Mindest-Duty-Cycle (~24%) — unterhalb dreht der Motor nicht an
#define STIRRER_MAX_DUTY 255       // Maximum Duty-Cycle (100%)

// I2C Pins (ESP32-S3 - Custom)
#define I2C_SDA 19             // GPIO19 - I2C SDA (RTC)
#define I2C_SCL 20             // GPIO20 - I2C SCL (RTC)

// ==================== GLOBALE VARIABLEN ====================
// Forward-Deklaration für HTML (Definition am Ende der Datei)
extern const char MAIN_HTML[] PROGMEM;

// WiFi & Webserver
AsyncWebServer server(80);
String wifiSSID = "";
String wifiPassword = "";
bool wifiConfigured = false;
bool apMode = true;

// pH-Sensor (ohne Library - eigene Berechnung)
float phValue = 7.0;
float phVoltage = 0.0;  // mV Spannung für pH-Messung
float temperature = 25.0;

// Ringpuffer für pH-Spannungsmessung (gleitender Durchschnitt)
#define PH_ARRAY_LENGTH 40
int pHArray[PH_ARRAY_LENGTH];
int pHArrayIndex = 0;

// FastAccelStepper (WICHTIG: Nur EIN Stepper-Objekt für gemeinsamen STEP/DIR Pin!)
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;  // Gemeinsames Stepper-Objekt für alle 3 Pumpen
int activePumpIndex = -1;          // Aktuell aktive Pumpe (0=Sample, 1=Reagent, 2=Rinse, -1=keine)

// Stop-Request Flag (wird von stopMeasurement() gesetzt, von State-Machine geprüft)
volatile bool stopRequested = false;

// Pending Pump Command (HTTP-Handler → loop() Delegation)
// WICHTIG: AsyncWebServer-Handler laufen im async_tcp Task!
// Blockierende Operationen (pumpVolume) dürfen dort NICHT ausgeführt werden,
// sonst blockiert async_tcp seinen eigenen Watchdog → Crash in ESP32 v3.x.
struct PendingPumpCmd {
  volatile bool pending = false;
  int pump = 0;
  float volume = 0;
  bool reverse = false;
};
PendingPumpCmd pendingPumpTest;

// Pending Calibration Command (gleiche Logik)
struct PendingCalCmd {
  volatile bool pending = false;
  int pump = 0;
  long steps = 0;
};
PendingCalCmd pendingCalibration;

// RTC & Zeit
RTC_DS3231 rtc;
bool rtcAvailable = false;

// NTP Zeit (Backup wenn RTC nicht verfügbar)
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;        // GMT+1
const int daylightOffset_sec = 3600;     // Sommerzeit
bool timeInitialized = false;

// Messzustände
enum MeasurementState {
  IDLE,
  RINSING,
  FILLING_SAMPLE,
  MEASURING_INITIAL_PH,
  TITRATING,
  CALCULATING,
  CLEANUP,
  COMPLETED,
  ERROR_STATE
};

MeasurementState currentState = IDLE;
String stateDescription = "Bereit";
unsigned long stateStartTime = 0;  // Timer für State-Machine (global für Reset)
unsigned long lastProgressLog = 0; // Timer für Progress-Logging

// Stabilitäts-Timing für UI-Statistik
unsigned long lastStabilityResetTime = 0;  // Zeitpunkt des letzten resetPHStability()
float lastStabilityDuration = 0;           // Letzte gemessene Stabilitätszeit in Sekunden

// Messparameter
struct MeasurementSettings {
  float sampleVolume = 50.0;           // ml Probevolumen
  float rinseVolume = 10.0;            // ml Spülwasser
  float overpumpPercent = 10.0;        // % Überpumpen beim Entleeren
  float acidConcentration = 0.1;       // mol/l HCl-Konzentration (nominell)
  float acidCorrectionFactor = 1.0;    // Korrekturfaktor für Säure (1.0 = keine Korrektur)
  float targetPH = 4.5;                // Endpunkt pH-Wert

  // Dynamische Titrationsparameter (Min/Max-Paare, exponentiell interpoliert)
  float doseVolumeMin = 0.01;          // ml - nahe Ziel-pH (feine Dosis)
  float doseVolumeMax = 0.5;           // ml - weit vom Ziel (große Dosis)
  float stabilityTimeMin = 5.0;         // Sekunden - weit vom Ziel (kurzer Auswertungszeitraum)
  float stabilityTimeMax = 30.0;       // Sekunden - nahe Ziel (langer Auswertungszeitraum, mehr Datenpunkte)
  float toleranceMin = 0.05;           // pH - nahe Ziel (strenge Stabilitätsprüfung)
  float toleranceMax = 0.3;            // pH - weit vom Ziel (lockere Prüfung)
  float interpolationExponent = 2.5;   // Kurvenform: 1.0=linear, 2-3=exponentiell (empfohlen)
  float acidPrimeVolume = 0.2;         // ml - Priming der Säurepumpe beim Spülen
  float submersionVolume = 5.0;        // ml - Zusatzwasser damit Säurespritze beim Priming untertaucht
  int phAcclimatizationTime = 30000;   // ms Akklimatisierungszeit nach Probenentnahme (30s Standard)
  int maxStabilityTimeout = 120;        // Sekunden - max Wartezeit pro Stabilitätsprüfung

  // Sicherheit: Überlaufschutz
  float maxContainerVolume = 100.0;    // ml Maximales Behältervolumen (Überlaufschutz)

  // Auto-Messung Einstellungen (RTC-basiert)
  int autoMeasureEnabled = 0;          // 0 = aus, 1 = an
  int firstMeasurementHour = 6;        // 0-23 Uhr (Erste Messung)
  int firstMeasurementMinute = 0;      // 0-59 Minuten
  int measurementIntervalHours = 0;    // 0-23 Stunden (0 = nur einmal pro Tag)
  int measurementRepeatDays = 1;       // 1 = täglich, 2 = jeden 2. Tag, etc.
  unsigned long lastMeasurementUnix = 0; // Unix timestamp der letzten Messung
  bool measurementCompleted = true;      // false = letzte Messung nicht abgeschlossen, Messungen blockiert

  // Auto-Send zu Dosierpumpe
  bool autoSendToPumps = false;        // Automatisch an Dosierpumpe senden
  char pumpsIP[16] = "192.168.1.100"; // IP-Adresse der Dosierpumpe

  // Rührer PWM-Geschwindigkeit
  int stirrerSpeed = 80;               // PWM Duty Cycle 0-100% (Rührer-Geschwindigkeit)
  int stirrerPwmFreq = 25000;          // Hz - PWM-Frequenz Rührer (25kHz = unhörbar)

  // Behälter-Füllstandsüberwachung
  float acidContainerMax = 1000.0;     // ml Maximalvolumen Säurebehälter
  float acidContainerLevel = 1000.0;   // ml aktueller Füllstand Säure
  float wasteContainerMax = 2000.0;    // ml Maximalvolumen Abwasserbehälter
  float wasteContainerLevel = 0.0;     // ml aktueller Füllstand Abwasser
  float aquariumTotalUsed = 0.0;       // ml insgesamt aus Aquarium entnommen

  // Timestamps für Behälter-Aktionen
  char acidLastRefill[20] = "";        // Zeitstempel letzte Säure-Nachfüllung
  char wasteLastEmpty[20] = "";        // Zeitstempel letzte Abwasser-Entleerung
  char aquariumLastReset[20] = "";     // Zeitstempel letzter Aquarium-Zähler-Reset
};

MeasurementSettings settings;

// Einzelner Dosierschritt für Dosierprotokoll-Graph
struct TitrationStep {
  float phBefore;        // pH vor Dosierung
  float doseVolume;      // ml dosiert in diesem Schritt
  float stabilityTime;   // Sekunden - tatsächliche Wartezeit bis Stabilität
  float tolerance;       // pH Toleranz
};
#define MAX_TITRATION_STEPS 200

// Titrationsdaten
struct TitrationData {
  unsigned long startTime = 0;
  float initialPH = 0.0;
  float finalPH = 0.0;
  float initialPHDistance = 0.0;     // initialPH - targetPH (für Progress-Berechnung)
  long totalSteps = 0;
  float acidUsed = 0.0;              // ml
  float lastAcidUsed = 0.0;          // ml (für Füllstands-Differenz-Tracking)
  float khValue = 0.0;               // dKH
  String timestamp = "";
  bool valid = false;
  bool sentToPumps = false;          // Flag: Wurde an Dosierpumpe gesendet
  TitrationStep steps[MAX_TITRATION_STEPS];  // Dosierprotokoll
  int stepCount = 0;
  bool stabilityTimeoutOccurred = false;     // Flag: Timeout bei Stabilitätsprüfung aufgetreten
};

TitrationData currentTitration;

// Dynamische Titrationsparameter (Rückgabe von calculateTitrationParams)
struct TitrationParams {
  float doseVolume;       // ml
  float stabilityTime;    // Sekunden - dynamischer Auswertungszeitraum
  float tolerance;        // pH units für Stabilitätsprüfung
};

// pH-Stabilitätsprüfung
// Ringpuffer für pH-Stabilität
#define PH_BUFFER_SIZE_MAX 120 // Max 120 Messungen = 60 Sekunden bei 500ms Intervall
#define PH_SAMPLE_INTERVAL 500 // ms zwischen pH-Messungen
struct PHStability {
  float phBuffer[PH_BUFFER_SIZE_MAX];
  int bufferIndex = 0;
  int bufferCount = 0;
  int requiredSamples = 0;  // Dynamisch berechnet aus stabilityTime
  unsigned long lastUpdateTime = 0;
  bool isStable = false;
  float slope = 0.0;  // Steigung der pH-Kurve
};

PHStability phStability;

// pH-Kalibrierung Variablen
#define PH_CALIBRATION_INTERVAL 30000  // 30 Sekunden Wartezeit für Stabilisierung
#define PH_SAMPLE_COUNT 60             // 60 Samples für Durchschnitt
bool isPhCalibrating = false;
bool isPhCalibrationStable = false;
float phCalibrationValue = 0.0;        // 4.0 oder 7.0
unsigned long phCalibrationStartTime = 0;
float phSamples[PH_SAMPLE_COUNT];
int phSampleIndex = 0;

// pH-Kalibrierungs-Daten (2-Punkt-Kalibrierung, gespeichert in settings.json)
struct PHCalibration {
  float voltage_pH4 = 3000.0;     // mV bei pH 4.0 (Standardwert)
  float voltage_pH7 = 2500.0;     // mV bei pH 7.0 (Standardwert)
  bool isCalibrated = false;      // Beide Punkte kalibriert?
  String timestamp_pH4 = "";      // Zeitstempel der pH 4.0 Kalibrierung
  String timestamp_pH7 = "";      // Zeitstempel der pH 7.0 Kalibrierung
};

PHCalibration phCal;

// pH-Kalibrierungshistorie für Sonden-Gesundheitscheck
#define PH_CAL_HISTORY_MAX 50  // Maximal 50 Einträge pro Typ (pH4/pH7)
#define PH_CAL_TIMESTAMP_LEN 20  // "YYYY-MM-DD HH:MM:SS" + null = 20 Bytes
#define PH_CAL_PAIR_WINDOW_MS (2UL * 60UL * 60UL * 1000UL)  // 2 Stunden für Paare (UL für unsigned long)

struct PHCalibrationEntry {
  char timestamp[PH_CAL_TIMESTAMP_LEN];  // Festes char-Array statt String (spart Heap)
  float mV;
};

struct PHCalibrationHistory {
  char sensorInstallDate[12];  // "YYYY-MM-DD" + null = 11 Bytes, 12 für Alignment
  float referenceSlope = 0;    // Referenz-Steigung (mV/pH) bei erster Kalibrierung = 100%
  PHCalibrationEntry calibrations_pH4[PH_CAL_HISTORY_MAX];
  PHCalibrationEntry calibrations_pH7[PH_CAL_HISTORY_MAX];
  int count_pH4 = 0;
  int count_pH7 = 0;
};

PHCalibrationHistory phCalHistory;
// Speicherverbrauch: 50 * (20 + 4) * 2 + 12 + 8 = ~2420 Bytes statisch (kein Heap-Fragmentierung!)

// Prognose-Struktur für Behälter (muss früh definiert sein wegen Arduino-Compiler)
struct ContainerForecast {
  // Säure
  float avgAcidPerMeasurement;      // ml Durchschnitt
  int acidMeasurementsLeft;         // Anzahl Messungen bis leer
  float acidDaysLeft;               // Tage bis leer (-1 wenn manuell)

  // Abwasser
  float wastePerMeasurement;        // ml pro Messung
  int wasteMeasurementsUntilFull;   // Anzahl Messungen bis voll
  float wasteDaysUntilFull;         // Tage bis voll (-1 wenn manuell)

  // Aquarium
  float aquariumPerMeasurement;     // ml pro Messung
  float aquariumPerDay;             // ml pro Tag
  float aquariumPerWeek;            // ml pro Woche
};

// Messdaten-Historie (LittleFS)
const char* MEASUREMENT_FILE = "/measurements.json";
const int MAX_MEASUREMENTS = 100;

// ==================== LOGGING HELPERS ====================
void logHeader(const char* phase) {
  Serial.println();
  Serial.println("╔════════════════════════════════════════════════════════╗");
  Serial.printf("║  %-52s║\n", phase);
  Serial.println("╚════════════════════════════════════════════════════════╝");
}

void logInfo(const char* msg) {
  Serial.printf("ℹ️  %s\n", msg);
}

// ==================== FORWARD DECLARATIONS ====================
void saveSettings();  // Wird später definiert, aber früh benötigt

void logSuccess(const char* msg) {
  Serial.printf("✅ %s\n", msg);
}

void logWarning(const char* msg) {
  Serial.printf("⚠️  %s\n", msg);
}

void logError(const char* msg) {
  Serial.printf("❌ FEHLER: %s\n", msg);
}

void logState(const char* newState) {
  Serial.println();
  Serial.println("────────────────────────────────────────────────────────");
  Serial.printf("🔄 STATE: %s\n", newState);
  Serial.println("────────────────────────────────────────────────────────");
}

// Pumpen-Kalibrierung
struct PumpCalibration {
  int stepsPerML_Sample = 200;
  int stepsPerML_Reagent = 200;
  int stepsPerML_Rinse = 200;

  // Geschwindigkeit und Beschleunigung für jede Pumpe
  int speed_Sample = 2000;           // Hz Stepper-Geschwindigkeit Probenwasser
  int speed_Reagent = 1000;          // Hz Stepper-Geschwindigkeit Säure
  int speed_Rinse = 2000;            // Hz Stepper-Geschwindigkeit Spülwasser

  int accel_Sample = 1000;           // Beschleunigung Probenwasser
  int accel_Reagent = 500;           // Beschleunigung Säure
  int accel_Rinse = 1000;            // Beschleunigung Spülwasser

  bool isCalibrating = false;
  int calibratingPump = 0;
  long calibrationSteps = 0;
};

PumpCalibration pumpCal;

// ==================== RÜHRER PWM FUNKTIONEN ====================
// Rührer einschalten mit Softstart (3 Sekunden Rampe)
// Zentrale Duty-Cycle-Berechnung mit Motor-Totzonen-Kompensation
// 0% = AUS, 1-100% → STIRRER_MIN_DUTY bis STIRRER_MAX_DUTY
int stirrerDutyCycle(int speedPercent) {
  if (speedPercent <= 0) return 0;
  if (speedPercent >= 100) return STIRRER_MAX_DUTY;
  return map(speedPercent, 1, 100, STIRRER_MIN_DUTY, STIRRER_MAX_DUTY);
}

void stirrerOn() {
  int targetDutyCycle = stirrerDutyCycle(settings.stirrerSpeed);

  Serial.printf("Rührer Softstart → %d%% (Duty: %d/255, Min: %d) über 3 Sekunden...\n",
                settings.stirrerSpeed, targetDutyCycle, STIRRER_MIN_DUTY);

  // Rampe von 0 zu Zielgeschwindigkeit in 3 Sekunden
  int rampSteps = 30;  // 30 Schritte
  int delayPerStep = 3000 / rampSteps;  // 100ms pro Schritt = 3 Sekunden total

  for (int i = 0; i <= rampSteps; i++) {
    int currentDutyCycle = (targetDutyCycle * i) / rampSteps;
    ledcWrite(STIRRER_PIN, currentDutyCycle);
    vTaskDelay(pdMS_TO_TICKS(delayPerStep));  // Nicht-blockierend, gibt CPU frei
  }

  Serial.printf("✓ Rührer AN (Geschwindigkeit: %d%%)\n", settings.stirrerSpeed);
}

// Rührer ausschalten mit Softstopp (3 Sekunden Rampe)
// Bei hardStop=true wird sofort ohne Rampe gestoppt
void stirrerOff(bool hardStop = false) {
  if (hardStop) {
    // HARD STOP: Sofort ausschalten ohne Rampe
    ledcWrite(STIRRER_PIN, 0);
    Serial.println("✓ Rührer SOFORT AUS (Hard Stop)");
    return;
  }

  int currentDutyCycle = stirrerDutyCycle(settings.stirrerSpeed);

  Serial.println("Rührer Softstopp → 0% über 3 Sekunden...");

  // Rampe von aktueller Geschwindigkeit zu 0 in 3 Sekunden
  int rampSteps = 30;  // 30 Schritte
  int delayPerStep = 3000 / rampSteps;  // 100ms pro Schritt = 3 Sekunden total

  for (int i = rampSteps; i >= 0; i--) {
    int dutyCycle = (currentDutyCycle * i) / rampSteps;
    ledcWrite(STIRRER_PIN, dutyCycle);
    vTaskDelay(pdMS_TO_TICKS(delayPerStep));  // Nicht-blockierend, gibt CPU frei
  }

  ledcWrite(STIRRER_PIN, 0);  // Sicherstellen dass es wirklich 0 ist
  Serial.println("✓ Rührer AUS");
}

// Rührer-Geschwindigkeit ändern (während Betrieb)
void stirrerSetSpeed(int speedPercent) {
  if (speedPercent < 0) speedPercent = 0;
  if (speedPercent > 100) speedPercent = 100;
  settings.stirrerSpeed = speedPercent;

  int dutyCycle = stirrerDutyCycle(speedPercent);
  ledcWrite(STIRRER_PIN, dutyCycle);
  Serial.printf("Rührer-Geschwindigkeit: %d%% (PWM Duty Cycle: %d/255, Min: %d)\n", speedPercent, dutyCycle, STIRRER_MIN_DUTY);
  Serial.printf("  → GPIO%d, Frequenz %d Hz\n", STIRRER_PIN, settings.stirrerPwmFreq);
}

// Rührer PWM-Frequenz ändern (zur Laufzeit)
void stirrerUpdateFreq(int freqHz) {
  if (freqHz < 100) freqHz = 100;
  if (freqHz > 40000) freqHz = 40000;
  settings.stirrerPwmFreq = freqHz;

  // WICHTIG: ledcAttach schlägt in ESP32 v3.x fehl wenn Pin bereits attached ist!
  // ledcChangeFrequency() ändert Frequenz+Resolution eines bereits angehängten Pins.
  uint32_t actualFreq = ledcChangeFrequency(STIRRER_PIN, freqHz, STIRRER_PWM_RESOLUTION);

  if (actualFreq == 0) {
    Serial.printf("FEHLER: ledcChangeFrequency fehlgeschlagen für %d Hz!\n", freqHz);
  }

  // Duty Cycle wiederherstellen
  int dutyCycle = stirrerDutyCycle(settings.stirrerSpeed);
  ledcWrite(STIRRER_PIN, dutyCycle);
  Serial.printf("Rührer PWM-Frequenz: %d Hz (tatsächlich: %u Hz, Duty: %d/255)\n", freqHz, actualFreq, dutyCycle);
}

// ==================== PH-KALIBRIERUNG ====================
void startPHCalibration(float phValue) {
  if (phValue != 4.0 && phValue != 7.0) {
    Serial.print("WARNUNG: Nur pH 4.0 und 7.0 für Kalibrierung unterstützt, nicht ");
    Serial.println(phValue);
    return;
  }

  isPhCalibrating = true;
  isPhCalibrationStable = false;
  phCalibrationValue = phValue;
  phCalibrationStartTime = millis();
  phSampleIndex = 0;

  // Initialisiere Samples mit 0
  for (int i = 0; i < PH_SAMPLE_COUNT; i++) {
    phSamples[i] = 0;
  }

  Serial.print("pH-Kalibrierung gestartet für pH ");
  Serial.println(phValue);
}

void updatePHCalibration() {
  if (!isPhCalibrating) return;

  // Zeit seit Beginn der Kalibrierung
  unsigned long elapsedTime = millis() - phCalibrationStartTime;

  // Messung für gleitenden Durchschnitt speichern
  phSamples[phSampleIndex] = phVoltage;  // Aktuelle Spannung speichern
  phSampleIndex = (phSampleIndex + 1) % PH_SAMPLE_COUNT;

  // Prüfen ob pH-Wert stabil ist (nach Wartezeit)
  if (elapsedTime > PH_CALIBRATION_INTERVAL) {
    // Berechne Durchschnitt der Spannung
    float sumVoltage = 0;
    for (int i = 0; i < PH_SAMPLE_COUNT; i++) {
      sumVoltage += phSamples[i];
    }

    float averageVoltage = sumVoltage / PH_SAMPLE_COUNT;

    // Standardabweichung berechnen
    float sumDiff = 0;
    for (int i = 0; i < PH_SAMPLE_COUNT; i++) {
      float diff = phSamples[i] - averageVoltage;
      sumDiff += diff * diff;
    }

    float stdDev = sqrt(sumDiff / PH_SAMPLE_COUNT);

    Serial.printf("pH-Kalibrierung: Durchschnitt=%.2fmV, StdDev=%.2fmV\n", averageVoltage, stdDev);

    // Wenn Standardabweichung klein genug, ist der Wert stabil
    if (stdDev < 10) {  // 10mV als Stabilitätskriterium
      isPhCalibrationStable = true;

      // Speichere Kalibrierungspunkt
      Serial.printf("✓ pH %.1f kalibriert bei %.2f mV\n", phCalibrationValue, averageVoltage);

      // Speichere Kalibrierungsdaten in phCal Struktur
      String timestamp = getFormattedTime();
      if (phCalibrationValue == 4.0) {
        phCal.voltage_pH4 = averageVoltage;
        phCal.timestamp_pH4 = timestamp;
        Serial.printf("  → pH 4.0: %.2f mV @ %s\n", averageVoltage, timestamp.c_str());
        // Zur Kalibrierungshistorie hinzufügen
        addPHCalibrationEntry(4, averageVoltage);
      } else if (phCalibrationValue == 7.0) {
        phCal.voltage_pH7 = averageVoltage;
        phCal.timestamp_pH7 = timestamp;
        Serial.printf("  → pH 7.0: %.2f mV @ %s\n", averageVoltage, timestamp.c_str());
        // Zur Kalibrierungshistorie hinzufügen
        addPHCalibrationEntry(7, averageVoltage);
      }

      // Check ob beide Punkte kalibriert sind
      if (phCal.voltage_pH4 > 0 && phCal.voltage_pH7 > 0) {
        phCal.isCalibrated = true;
        Serial.println("  → Vollständige 2-Punkt-Kalibrierung abgeschlossen!");
        // Effizienz berechnen und ausgeben
        float efficiency = calculatePHSensorEfficiency();
        Serial.printf("  → Sonden-Effizienz: %.1f%%\n", efficiency);
      }

      // HINWEIS: pH-Kalibrierung wird bereits in addPHCalibrationEntry() gespeichert
      // saveSettings() ist hier nicht mehr nötig

      // Kalibrierung beenden
      isPhCalibrating = false;
    } else if (elapsedTime > PH_CALIBRATION_INTERVAL + 30000) {
      // Timeout nach 60 Sekunden total
      Serial.println("⚠ pH-Kalibrierung Timeout - Wert nicht stabil");
      isPhCalibrating = false;
    }
  }
}

// ==================== SETUP ====================
void setup() {
  // SOFORT: Rührer-Pin LOW setzen bevor irgendetwas anderes passiert!
  // Während des ESP32-S3 Boot-Prozesses kann GPIO6 undefiniert sein → Motor dreht unkontrolliert
  pinMode(STIRRER_PIN, OUTPUT);
  digitalWrite(STIRRER_PIN, LOW);

  Serial.begin(115200);
  delay(1000);  // Warte auf Serial-Verbindung

  // WICHTIG: CPU auf maximale Frequenz setzen für Stabilität
  // Verhindert Cache-Probleme bei hoher Last (WiFi + Stepper)
  setCpuFrequencyMhz(240);  // ESP32-S3 Maximum = 240 MHz



  Serial.println("\n\n=== KH-Titrationssystem gestartet ===");
  Serial.println("Board: ESP32-S3 UN-O N16R8");
  Serial.print("CPU Frequenz: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
  // Detaillierte Speicherinfo
  Serial.println("\n--- Speicher-Info ---");
  Serial.printf("Interner Heap: %d KB frei / %d KB total\n",
                ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024);

  // ESP32-S3 PSRAM Info
  if (psramFound()) {
    Serial.printf("PSRAM: %d KB frei / %d KB total (%.1f MB)\n",
                  ESP.getFreePsram() / 1024,
                  ESP.getPsramSize() / 1024,
                  ESP.getPsramSize() / 1024.0 / 1024.0);

    // Prüfe ob PSRAM für malloc verfügbar ist
    void* testAlloc = ps_malloc(1024);
    if (testAlloc) {
      Serial.println("PSRAM: ✓ ps_malloc() funktioniert");
      free(testAlloc);
    } else {
      Serial.println("PSRAM: ⚠ ps_malloc() fehlgeschlagen!");
    }
  } else {
    Serial.println("PSRAM: Nicht gefunden oder nicht aktiviert!");
    Serial.println("  → Kompiliere mit: PSRAM=opi für N16R8");
  }

  Serial.print("Flash Größe: ");
  Serial.print(ESP.getFlashChipSize() / 1024 / 1024);
  Serial.println(" MB");

  // LittleFS initialisieren
  Serial.println("\nInitialisiere LittleFS...");
  if (!LittleFS.begin(true)) {
    Serial.println("FEHLER: LittleFS Mount fehlgeschlagen!");
    Serial.println("System läuft weiter, aber Daten können nicht gespeichert werden.");
    // NICHT return! System soll weiterlaufen
  } else {
    Serial.println("✓ LittleFS erfolgreich gemountet");

    // DEBUG: Liste alle Dateien im Dateisystem auf
    Serial.println("\n[DEBUG] LittleFS Dateien:");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
      Serial.printf("  - %s (%d bytes)\n", file.name(), file.size());
      file = root.openNextFile();
    }
    Serial.println("[DEBUG] Ende der Dateiliste\n");
  }
  
  // Gespeicherte WiFi-Konfiguration laden
  Serial.println("\nLade WiFi-Konfiguration...");
  loadWiFiConfig();
  Serial.println("✓ WiFi-Konfiguration geladen");

  // Pin-Konfiguration
  Serial.println("\nKonfiguriere GPIO Pins...");

  // ESP32-S3: ADC Attenuation für 3.3V Range
  analogSetAttenuation(ADC_11db);  // 0-3.3V Range für alle ADC-Pins

  pinMode(PH_PIN, INPUT);

  // Rührer PWM konfigurieren (ESP32-S3 LEDC)
  ledcAttach(STIRRER_PIN, settings.stirrerPwmFreq, STIRRER_PWM_RESOLUTION);
  ledcWrite(STIRRER_PIN, 0);  // Initial AUS
  Serial.printf("✓ Rührer PWM konfiguriert (Kanal %d, %d Hz)\n", STIRRER_PWM_CHANNEL, settings.stirrerPwmFreq);

  // ENABLE Pins für Stepper-Motoren konfigurieren
  pinMode(ENABLE_SAMPLE, OUTPUT);
  pinMode(ENABLE_REAGENT, OUTPUT);
  pinMode(ENABLE_RINSE, OUTPUT);

  // Initial alle Motoren deaktivieren (HIGH = disabled)
  digitalWrite(ENABLE_SAMPLE, HIGH);
  digitalWrite(ENABLE_REAGENT, HIGH);
  digitalWrite(ENABLE_RINSE, HIGH);
  Serial.println("✓ GPIO Pins konfiguriert");

  // FastAccelStepper Engine initialisieren
  Serial.println("\nInitialisiere FastAccelStepper...");
  engine.init();

  // NUR EIN Stepper-Objekt für gemeinsamen STEP/DIR Pin erstellen
  // (wie im funktionierenden Dosierpumpen-Code)
  stepper = engine.stepperConnectToPin(STEP_PIN);

  if (stepper) {
    // DIR Pin setzen (gemeinsam für alle 3 Pumpen)
    stepper->setDirectionPin(DIR_PIN);
    stepper->setCurrentPosition(0);

    // Standard-Geschwindigkeit setzen
    stepper->setSpeedInHz(pumpCal.speed_Sample);
    stepper->setAcceleration(pumpCal.accel_Sample);

    Serial.println("✓ FastAccelStepper initialisiert (gemeinsamer STEP/DIR)");
    Serial.println("  ENABLE-Pins werden manuell per digitalWrite() gesteuert");
  } else {
    Serial.println("FEHLER: FastAccelStepper Initialisierung fehlgeschlagen!");
  }
  
  // I2C initialisieren (für RTC oder andere I2C-Geräte)
  Serial.println("\nInitialisiere I2C...");
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.printf("✓ I2C initialisiert (SDA=GPIO%d, SCL=GPIO%d)\n", I2C_SDA, I2C_SCL);

  // RTC initialisieren
  Serial.println("\nInitialisiere RTC (DS3231)...");
  if (rtc.begin()) {
    rtcAvailable = true;
    Serial.println("✓ RTC gefunden!");

    // Prüfe ob RTC Zeit verloren hat (z.B. Batterie leer)
    if (rtc.lostPower()) {
      Serial.println("⚠ RTC hat Strom verloren - Zeit wird von NTP synchronisiert");
    } else {
      DateTime now = rtc.now();
      Serial.printf("✓ RTC-Zeit: %04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
      timeInitialized = true;
    }
  } else {
    Serial.println("⚠ RTC nicht gefunden - nutze NTP als Fallback");
    rtcAvailable = false;
  }

  // pH-Sensor initialisieren (Ringpuffer auf 0 setzen)
  Serial.println("\nInitialisiere pH-Sensor...");
  for (int i = 0; i < PH_ARRAY_LENGTH; i++) {
    pHArray[i] = 0;
  }
  Serial.println("✓ pH-Sensor initialisiert (Ringpuffer bereit)");

  // Einstellungen laden
  Serial.println("\nLade Einstellungen...");
  loadSettings();
  Serial.println("✓ Einstellungen geladen");

  // WICHTIG: Gespeicherte Rührer-PWM-Frequenz auf Hardware anwenden!
  // ledcAttach() oben wurde mit dem Struct-Default aufgerufen (vor loadSettings).
  // Jetzt die gespeicherte Frequenz setzen, damit der Rührer korrekt läuft.
  uint32_t actualFreq = ledcChangeFrequency(STIRRER_PIN, settings.stirrerPwmFreq, STIRRER_PWM_RESOLUTION);
  ledcWrite(STIRRER_PIN, 0);  // Sicherstellen: Rührer bleibt AUS
  Serial.printf("✓ Rührer PWM aktualisiert: %d Hz (tatsächlich: %u Hz), Speed: %d%%\n",
                settings.stirrerPwmFreq, actualFreq, settings.stirrerSpeed);

  // pH-Kalibrierungshistorie laden
  Serial.println("\nLade pH-Kalibrierungshistorie...");
  loadPHCalibrationHistory();

  // WiFi starten
  Serial.println("\nStarte WiFi...");
  if (wifiConfigured) {
    Serial.println("WiFi konfiguriert - starte Client-Modus");
    startWiFiClient();
  } else {
    Serial.println("WiFi nicht konfiguriert - starte Access Point");
    startAccessPoint();
  }

  // Webserver-Routen einrichten
  Serial.println("\nRichte Webserver-Routen ein...");
  setupWebServer();

  // Webserver starten
  Serial.println("Starte Webserver...");
  server.begin();
  Serial.println("✓ Webserver gestartet");

  // ArduinoOTA für Updates über Arduino IDE einrichten (nur im Client-Modus)
  if (!apMode) {
    ArduinoOTA.setHostname("KH-Tester");  // Name der im IDE angezeigt wird
    ArduinoOTA.setPassword("khtester");   // Passwort für OTA (optional)

    ArduinoOTA.onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "Firmware" : "Filesystem";
      Serial.println("\n[OTA] Update Start: " + type);
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\n[OTA] Update abgeschlossen!");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("[OTA] Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("✓ ArduinoOTA gestartet (Hostname: KH-Tester)");
  }

  Serial.println("\n" + String('=', 50));
  if (apMode) {
    Serial.println("=== ACCESS POINT MODUS ===");
    Serial.println("SSID: ESP32-KH-Tester");
    Serial.println("IP: 192.168.4.1");
  } else {
    Serial.println("=== WIFI CLIENT MODUS ===");
    Serial.print("SSID: ");
    Serial.println(wifiSSID);
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());
    Serial.println("OTA-Name: KH-Tester");
  }
  Serial.println(String('=', 50));
  Serial.println("\nSetup abgeschlossen - System bereit!\n");

  // Erste pH-Messung durchführen (damit phValue initialisiert ist)
  updatePHValue();
  // pH-Wert wird live im Einstellungen-Tab angezeigt
}

// ==================== AUTOMATISCHE MESSUNG (RTC-BASIERT) ====================
void checkAutoMeasurement() {
  // Nur wenn Auto-Messung aktiviert ist
  if (settings.autoMeasureEnabled == 0) {
    return;
  }

  // Blockiert wenn letzte Messung nicht abgeschlossen (Stromausfall/Abbruch)
  if (!settings.measurementCompleted) {
    return;
  }

  // Nur wenn System IDLE ist (keine laufende Messung)
  if (currentState != IDLE && currentState != COMPLETED && currentState != ERROR_STATE) {
    return;
  }

  // Aktuelle Zeit von RTC holen
  DateTime now = rtc.now();
  unsigned long currentUnix = now.unixtime();

  // Berechne Tag seit Epoch (für Wiederholungs-Prüfung)
  unsigned long daysSinceEpoch = currentUnix / 86400;
  unsigned long currentDayInCycle = daysSinceEpoch % settings.measurementRepeatDays;

  // Ist heute ein Mess-Tag? (Tag 0 im Zyklus)
  if (currentDayInCycle != 0) {
    return; // Heute keine Messungen laut Plan
  }

  // Erstelle DateTime für erste Messung heute
  DateTime firstMeasurementToday = DateTime(
    now.year(), now.month(), now.day(),
    settings.firstMeasurementHour, settings.firstMeasurementMinute, 0
  );

  // Berechne alle Mess-Zeiten für heute
  DateTime nextMeasurement = firstMeasurementToday;

  // Finde nächste Mess-Zeit die >= jetzt ist
  while (nextMeasurement.unixtime() < currentUnix) {
    if (settings.measurementIntervalHours == 0) {
      // Nur einmal pro Tag, nächste ist morgen
      return;
    }
    // Nächste Mess-Zeit = aktuelle + Intervall
    nextMeasurement = DateTime(nextMeasurement.unixtime() + (settings.measurementIntervalHours * 3600L));

    // Wenn nächste Messung nicht mehr heute ist, abbrechen
    if (nextMeasurement.day() != now.day()) {
      return; // Keine weiteren Messungen heute
    }
  }

  // Prüfe: Ist es Zeit zu messen UND haben wir noch nicht gemessen?
  unsigned long nextMeasurementUnix = nextMeasurement.unixtime();

  if (currentUnix >= nextMeasurementUnix && settings.lastMeasurementUnix < nextMeasurementUnix) {
    // Zeit für automatische Messung!
    Serial.printf("\n=== AUTO-MESSUNG GESTARTET (%02d:%02d Uhr) ===\n",
                  nextMeasurement.hour(), nextMeasurement.minute());

    // Zeitstempel aktualisieren
    settings.lastMeasurementUnix = currentUnix;
    saveSettings();

    // Messung starten
    startMeasurement();
  }
}

// ==================== MAIN LOOP ====================
void loop() {
  // AsyncWebServer braucht kein handleClient() - läuft automatisch

  // ArduinoOTA für IDE-Updates (nur wenn WiFi verbunden)
  ArduinoOTA.handle();

  // WiFi-Verbindung überwachen und bei Bedarf neu verbinden
  checkWiFiConnection();

  // Heartbeat - zeige alle 10 Sekunden, dass System läuft
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 10000) {
    Serial.print(".");
    lastHeartbeat = millis();
  }

  // Messzustandsmaschine
  handleMeasurementStateMachine();

  // Automatische Messung prüfen
  checkAutoMeasurement();

  // pH-Wert periodisch aktualisieren (alle 500ms)
  static unsigned long lastPHUpdate = 0;
  if (millis() - lastPHUpdate > 500) {
    updatePHValue();
    lastPHUpdate = millis();
  }

  // pH-Kalibrierung aktualisieren
  if (isPhCalibrating) {
    updatePHCalibration();
  }

  // Pending Pump-Befehle ausführen (von HTTP-Handlern delegiert)
  // Läuft hier im loopTask statt im async_tcp Task → kein Watchdog-Problem
  if (pendingPumpTest.pending) {
    Serial.printf("Führe Pumpentest aus: Pumpe %d, %.2f ml, %s\n",
                  pendingPumpTest.pump, pendingPumpTest.volume,
                  pendingPumpTest.reverse ? "rückwärts" : "vorwärts");
    pumpVolume(pendingPumpTest.pump, pendingPumpTest.volume, pendingPumpTest.reverse);
    pendingPumpTest.pending = false;
    Serial.println("✓ Pumpentest abgeschlossen");
  }

  if (pendingCalibration.pending) {
    Serial.printf("Führe Kalibrierung aus: Pumpe %d, %ld Schritte\n",
                  pendingCalibration.pump, pendingCalibration.steps);
    if (stepper) {
      activatePump(pendingCalibration.pump);

      int speed, accel;
      switch(pendingCalibration.pump) {
        case 1: speed = pumpCal.speed_Sample; accel = pumpCal.accel_Sample; break;
        case 2: speed = pumpCal.speed_Reagent; accel = pumpCal.accel_Reagent; break;
        case 3: speed = pumpCal.speed_Rinse; accel = pumpCal.accel_Rinse; break;
        default: speed = 1000; accel = 500; break;
      }
      stepper->setSpeedInHz(speed);
      stepper->setAcceleration(accel);
      stepper->setCurrentPosition(0);
      stepper->move(pendingCalibration.steps);

      // Warten bis abgeschlossen (im loopTask — async_tcp läuft weiter)
      while (stepper->isRunning()) {
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      deactivateAllPumps();
      Serial.printf("✓ %ld Schritte ausgeführt - Warte auf Volumenmessung\n",
                    pendingCalibration.steps);
    }
    pendingCalibration.pending = false;
  }

  // Kein blocking delay() im loop - stattdessen yield() für Watchdog
  yield();
}

// ==================== pH-MESSUNG ====================

// pH-Wert aus Spannung berechnen (2-Punkt-Kalibrierung)
float getPHFromVoltage(float voltage) {
  if (phCal.isCalibrated) {
    // Lineare Interpolation zwischen den beiden Kalibrierungspunkten
    // Formel: pH = pH7 - slope * (voltage_pH7 - voltage)
    // wobei slope = (pH7 - pH4) / (voltage_pH7 - voltage_pH4)
    float slope = (7.0 - 4.0) / (phCal.voltage_pH7 - phCal.voltage_pH4);
    float pH = 7.0 - (slope * (phCal.voltage_pH7 - voltage));
    return pH;
  } else {
    // Fallback wenn nicht kalibriert (Nernst-Näherung)
    // Typisch: ~59mV pro pH-Einheit bei 25°C, Neutralpunkt bei ~2500mV
    return 7.0 - (voltage - 2500.0) / 177.0;
  }
}

// Durchschnitt eines Arrays berechnen (mit Ausreißer-Filterung)
double averageArray(int* arr, int number) {
  if (number <= 0) return 0;

  if (number < 5) {
    // Bei wenigen Werten: einfacher Durchschnitt
    long sum = 0;
    for (int i = 0; i < number; i++) {
      sum += arr[i];
    }
    return (double)sum / number;
  }

  // Bei >= 5 Werten: Min und Max entfernen für bessere Filterung
  int max_val = arr[0];
  int min_val = arr[0];
  long sum = 0;

  for (int i = 0; i < number; i++) {
    if (arr[i] > max_val) max_val = arr[i];
    if (arr[i] < min_val) min_val = arr[i];
    sum += arr[i];
  }

  // Entferne Min und Max vom Durchschnitt
  sum = sum - max_val - min_val;
  return (double)sum / (number - 2);
}

// pH-Spannung aktualisieren (mit Ringpuffer)
void updatePHValue() {
  static unsigned long lastSamplingTime = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastSamplingTime > 20) {  // Alle 20ms ein Sample
    // ADC-Wert in Ringpuffer speichern
    pHArray[pHArrayIndex++] = analogRead(PH_PIN);
    if (pHArrayIndex == PH_ARRAY_LENGTH) pHArrayIndex = 0;

    // Gefilterten Durchschnitt berechnen
    int avgReading = (int)averageArray(pHArray, PH_ARRAY_LENGTH);

    // ADC-Wert in mV umrechnen (ESP32: 12-bit ADC, 3.3V Referenz)
    phVoltage = avgReading * 3300.0 / 4095.0;

    // pH-Wert aus Spannung berechnen
    phValue = getPHFromVoltage(phVoltage);

    lastSamplingTime = currentMillis;
  }
}

// pH-Wert zum gleitenden Durchschnitt hinzufügen
void addPHToBuffer(float ph) {
  phStability.phBuffer[phStability.bufferIndex] = ph;
  phStability.bufferIndex = (phStability.bufferIndex + 1) % phStability.requiredSamples;
  if (phStability.bufferCount < phStability.requiredSamples) {
    phStability.bufferCount++;
  }
  phStability.lastUpdateTime = millis();
}

// Berechne Steigung der pH-Kurve (lineare Regression)
float calculatePHSlope() {
  if (phStability.bufferCount < 5) return 999.0; // Nicht genug Daten

  float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
  int n = phStability.bufferCount;

  for (int i = 0; i < n; i++) {
    float x = i;
    float y = phStability.phBuffer[i];
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }

  // Steigung: m = (n*sumXY - sumX*sumY) / (n*sumX2 - sumX*sumX)
  float slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
  return slope;
}

bool checkPHStability(float dynamicTolerance = -1.0, float dynamicStabilityTime = -1.0) {
  unsigned long now = millis();

  // Auswertungszeitraum (in Sekunden): dynamisch übergeben oder Default (stabilityTimeMin)
  float activeStabilityTime = (dynamicStabilityTime > 0) ? dynamicStabilityTime : settings.stabilityTimeMin;
  phStability.requiredSamples = (int)(activeStabilityTime * 1000.0 / PH_SAMPLE_INTERVAL);

  // Sicherheitsgrenzen: min 4 Samples (2s), max PH_BUFFER_SIZE_MAX
  if (phStability.requiredSamples < 4) phStability.requiredSamples = 4;
  if (phStability.requiredSamples > PH_BUFFER_SIZE_MAX) phStability.requiredSamples = PH_BUFFER_SIZE_MAX;

  // Erste Initialisierung
  if (phStability.bufferCount == 0) {
    addPHToBuffer(phValue);
    return false;
  }

  // Warte PH_SAMPLE_INTERVAL ms zwischen Updates
  if (now - phStability.lastUpdateTime < PH_SAMPLE_INTERVAL) {
    return false;
  }

  // Füge aktuellen pH-Wert hinzu
  addPHToBuffer(phValue);

  // Brauchen mindestens die konfigurierte Zeit an Daten
  if (phStability.bufferCount < phStability.requiredSamples) {
    // Nur beim ersten Sample eine Meldung ausgeben
    if (phStability.bufferCount == 1) {
      float timeNeeded = (phStability.requiredSamples * PH_SAMPLE_INTERVAL) / 1000.0;
      Serial.printf("pH-Puffer wird gefüllt (%.1fs benötigt)...\n", timeNeeded);
    }
    return false;
  }

  // Puffer ist jetzt voll - einmalige Bestätigung beim ersten Mal
  static bool bufferFullMessageShown = false;
  if (!bufferFullMessageShown) {
    Serial.println("✓ pH-Puffer gefüllt - Stabilitätsprüfung aktiv");
    bufferFullMessageShown = true;
  }

  // Berechne pH-Schwankung über den Zeitraum (Min/Max Differenz)
  float minPH = phStability.phBuffer[0];
  float maxPH = phStability.phBuffer[0];
  for (int i = 0; i < phStability.bufferCount; i++) {
    if (phStability.phBuffer[i] < minPH) minPH = phStability.phBuffer[i];
    if (phStability.phBuffer[i] > maxPH) maxPH = phStability.phBuffer[i];
  }
  float phVariation = maxPH - minPH;  // Absolute pH-Schwankung

  // Toleranz bestimmen: dynamisch übergeben oder Default (toleranceMin = feinste Stufe)
  float effectiveThreshold = (dynamicTolerance > 0) ? dynamicTolerance : settings.toleranceMin;

  float stabilityTime = (phStability.bufferCount * PH_SAMPLE_INTERVAL) / 1000.0;
  Serial.printf("pH-Stabilität: %.3f (Schwankung: %.3f pH über %.1fs, Schwelle: %.3f)\n",
                phValue, phVariation, stabilityTime, effectiveThreshold);

  // Prüfe ob Schwankung unter der aktiven Schwelle liegt
  if (phVariation < effectiveThreshold) {
    phStability.isStable = true;
    // Stabilitätsdauer für UI-Statistik berechnen
    lastStabilityDuration = (millis() - lastStabilityResetTime) / 1000.0;
    Serial.printf("✓ pH ist stabil (%.3f pH < %.3f pH Schwelle) nach %.1fs\n",
                  phVariation, effectiveThreshold, lastStabilityDuration);
    return true;
  }

  // Timeout-Prüfung: Wenn max. Wartezeit überschritten, trotzdem weiter
  unsigned long waitingTime = (now - lastStabilityResetTime);
  if (settings.maxStabilityTimeout > 0 && waitingTime > (unsigned long)settings.maxStabilityTimeout * 1000) {
    Serial.printf("⚠️ Stabilitäts-Timeout nach %lus (Max: %ds) - fahre fort\n",
                  waitingTime / 1000, settings.maxStabilityTimeout);
    currentTitration.stabilityTimeoutOccurred = true;
    phStability.isStable = false;  // nicht wirklich stabil
    lastStabilityDuration = waitingTime / 1000.0;
    return true;  // aber trotzdem weiter
  }

  return false;
}

void resetPHStability() {
  phStability.bufferIndex = 0;
  phStability.bufferCount = 0;
  phStability.lastUpdateTime = 0;
  phStability.isStable = false;
  phStability.slope = 0.0;
  // requiredSamples wird dynamisch in checkPHStability() berechnet
  for (int i = 0; i < PH_BUFFER_SIZE_MAX; i++) {
    phStability.phBuffer[i] = 0.0;
  }
  // Timing für UI-Statistik starten
  lastStabilityResetTime = millis();
}

// Setzt nur den Stabilität-Flag zurück, behält den Puffer
void invalidatePHStability() {
  phStability.isStable = false;
  Serial.println("[pH] Stabilität invalidiert, Puffer bleibt erhalten");
}

// ==================== BEHÄLTER-VOLUMEN TRACKING ====================
// Überlaufschutz: Trackt das aktuelle Volumen im Probebehälter

// Forward-Deklaration (wird später aufgerufen, bevor definiert)
void pumpVolume(int pumpNumber, float volumeML, bool reverse = false);

// Prüft ob Überlauf während Titration (sampleVolume + acidUsed > maxContainerVolume)
bool checkTitrationOverflow() {
  float totalVolume = settings.sampleVolume + currentTitration.acidUsed;
  if (totalVolume > settings.maxContainerVolume) {
    Serial.printf("⚠ ÜBERLAUFSCHUTZ: %.2f ml (Probe) + %.2f ml (Säure) = %.2f ml > %.2f ml max!\n",
                  settings.sampleVolume, currentTitration.acidUsed, totalVolume, settings.maxContainerVolume);
    return true;  // Überlauf!
  }
  return false;  // OK
}

// ==================== BEHÄLTER-FÜLLSTANDSÜBERWACHUNG ====================

// Säure wurde verbraucht (nach jeder Dosis)
void trackAcidUsed(float volumeML) {
  settings.acidContainerLevel -= volumeML;
  if (settings.acidContainerLevel < 0) settings.acidContainerLevel = 0;
  Serial.printf("[SÄURE] -%.3f ml → Füllstand: %.1f / %.1f ml\n",
                volumeML, settings.acidContainerLevel, settings.acidContainerMax);
}

// Abwasser wurde hinzugefügt (nach Abpumpen aus Messbehälter)
void trackWasteAdded(float volumeML) {
  settings.wasteContainerLevel += volumeML;
  if (settings.wasteContainerLevel > settings.wasteContainerMax) {
    settings.wasteContainerLevel = settings.wasteContainerMax;
  }
  Serial.printf("[ABWASSER] +%.1f ml → Füllstand: %.1f / %.1f ml\n",
                volumeML, settings.wasteContainerLevel, settings.wasteContainerMax);
}

// Wasser aus Aquarium entnommen (Probe + Spülwasser)
void trackAquariumUsed(float volumeML) {
  settings.aquariumTotalUsed += volumeML;
  Serial.printf("[AQUARIUM] +%.1f ml → Gesamt entnommen: %.1f ml\n",
                volumeML, settings.aquariumTotalUsed);
}

// Säurebehälter wurde aufgefüllt
void refillAcidContainer(float newLevel) {
  if (newLevel > settings.acidContainerMax) {
    newLevel = settings.acidContainerMax;
  }
  if (newLevel < 0) newLevel = 0;
  Serial.printf("[SÄURE] Aufgefüllt: %.1f ml → %.1f ml\n",
                settings.acidContainerLevel, newLevel);
  settings.acidContainerLevel = newLevel;

  // Timestamp setzen
  String ts = getFormattedTime();
  strncpy(settings.acidLastRefill, ts.c_str(), sizeof(settings.acidLastRefill) - 1);
  settings.acidLastRefill[sizeof(settings.acidLastRefill) - 1] = '\0';
}

// Abwasserbehälter wurde entleert
void emptyWasteContainer() {
  Serial.printf("[ABWASSER] Entleert: %.1f ml → 0 ml\n", settings.wasteContainerLevel);
  settings.wasteContainerLevel = 0;

  // Timestamp setzen
  String ts = getFormattedTime();
  strncpy(settings.wasteLastEmpty, ts.c_str(), sizeof(settings.wasteLastEmpty) - 1);
  settings.wasteLastEmpty[sizeof(settings.wasteLastEmpty) - 1] = '\0';
}

// Aquarium-Zähler zurücksetzen
void resetAquariumCounter() {
  Serial.printf("[AQUARIUM] Reset: %.1f ml → 0 ml\n", settings.aquariumTotalUsed);
  settings.aquariumTotalUsed = 0;

  // Timestamp setzen
  String ts = getFormattedTime();
  strncpy(settings.aquariumLastReset, ts.c_str(), sizeof(settings.aquariumLastReset) - 1);
  settings.aquariumLastReset[sizeof(settings.aquariumLastReset) - 1] = '\0';
}

// Füllstände als Prozent (für UI)
float getAcidLevelPercent() {
  if (settings.acidContainerMax <= 0) return 0;
  return (settings.acidContainerLevel / settings.acidContainerMax) * 100.0;
}

float getWasteLevelPercent() {
  if (settings.wasteContainerMax <= 0) return 0;
  return (settings.wasteContainerLevel / settings.wasteContainerMax) * 100.0;
}

// ==================== PROGNOSE-BERECHNUNGEN ====================

// Streaming-Leser: Holt die letzten N acidUsed-Werte aus measurements.json
// Verwendet Streaming um Speicher zu sparen
int getLastAcidUsedValues(float* values, int maxCount) {
  File file = LittleFS.open(MEASUREMENT_FILE, "r");
  if (!file) {
    return 0;
  }

  // Temporärer Ringpuffer für die letzten Werte
  float tempValues[10];  // Max 10 Werte zwischenspeichern
  int tempCount = 0;
  int totalFound = 0;

  // Stream-basiertes Parsing
  DynamicJsonDocument filter(64);
  filter["measurements"][0]["acidUsed"] = true;

  DynamicJsonDocument doc(4096);  // Kleinerer Buffer für Streaming
  DeserializationError error = deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();

  if (error) {
    return 0;
  }

  JsonArray measurements = doc["measurements"];
  totalFound = measurements.size();

  // Die letzten N Werte extrahieren
  int startIdx = (totalFound > maxCount) ? totalFound - maxCount : 0;
  int count = 0;

  for (int i = startIdx; i < totalFound; i++) {
    float acidUsed = measurements[i]["acidUsed"] | 0.0f;
    if (acidUsed > 0) {
      values[count++] = acidUsed;
    }
  }

  return count;
}

// Berechne Durchschnitt
float calculateAverage(float* values, int count) {
  if (count <= 0) return 0;
  float sum = 0;
  for (int i = 0; i < count; i++) {
    sum += values[i];
  }
  return sum / count;
}

// Berechne Messungen pro Tag basierend auf Messplan
float getMeasurementsPerDay() {
  if (settings.autoMeasureEnabled == 0) {
    return 0;  // Manueller Betrieb
  }

  float measurementsPerScheduledDay;
  if (settings.measurementIntervalHours == 0) {
    // Nur einmal pro Tag
    measurementsPerScheduledDay = 1.0;
  } else {
    // Mehrmals pro Tag (24h / Intervall)
    measurementsPerScheduledDay = 24.0 / settings.measurementIntervalHours;
  }

  // Berücksichtige Wiederholungstage (z.B. jeden 2. Tag = 0.5 × Messungen)
  float dailyAverage = measurementsPerScheduledDay / settings.measurementRepeatDays;

  return dailyAverage;
}

// Berechne alle Prognosen
ContainerForecast calculateForecast() {
  ContainerForecast forecast = {0};

  // 1. Säure-Durchschnitt aus letzten 3 Messungen
  float acidValues[3];
  int acidCount = getLastAcidUsedValues(acidValues, 3);
  forecast.avgAcidPerMeasurement = (acidCount > 0) ? calculateAverage(acidValues, acidCount) : 1.2;  // Fallback 1.2ml

  // 2. Abwasser pro Messung: rinseVolume + sampleVolume + acidUsed
  // (Testtropfen-Säure ist minimal und im avgAcid enthalten)
  forecast.wastePerMeasurement = settings.rinseVolume + settings.sampleVolume + forecast.avgAcidPerMeasurement;

  // 3. Aquarium pro Messung: 2× rinseVolume + sampleVolume
  // (Einmal beim Spülen, einmal am Ende für Sonde feucht halten)
  forecast.aquariumPerMeasurement = (2.0 * settings.rinseVolume) + settings.sampleVolume;

  // 4. Messungen bis Säure leer
  if (forecast.avgAcidPerMeasurement > 0) {
    forecast.acidMeasurementsLeft = (int)(settings.acidContainerLevel / forecast.avgAcidPerMeasurement);
  }

  // 5. Messungen bis Abwasser voll
  float wasteRemaining = settings.wasteContainerMax - settings.wasteContainerLevel;
  if (forecast.wastePerMeasurement > 0) {
    forecast.wasteMeasurementsUntilFull = (int)(wasteRemaining / forecast.wastePerMeasurement);
  }

  // 6. Tagesbasierte Prognosen (nur wenn Auto-Messung aktiv)
  float measPerDay = getMeasurementsPerDay();
  if (measPerDay > 0) {
    forecast.acidDaysLeft = forecast.acidMeasurementsLeft / measPerDay;
    forecast.wasteDaysUntilFull = forecast.wasteMeasurementsUntilFull / measPerDay;
    forecast.aquariumPerDay = forecast.aquariumPerMeasurement * measPerDay;
    forecast.aquariumPerWeek = forecast.aquariumPerDay * 7.0;
  } else {
    // Manueller Betrieb - keine Tagesprognose möglich
    forecast.acidDaysLeft = -1;
    forecast.wasteDaysUntilFull = -1;
    forecast.aquariumPerDay = 0;
    forecast.aquariumPerWeek = 0;
  }

  return forecast;
}

// Speichert nur die Behälter-Füllstände (leichtgewichtig, nach jeder Messung aufrufen)
void saveContainerLevels() {
  Serial.println("[CONTAINER] Speichere Füllstände...");
  saveSettings();  // Nutzt die bestehende Funktion, da Füllstände Teil der Settings sind
}

// ==================== STEPPER-KONTROLLE (FastAccelStepper) ====================
// WICHTIG: Wir verwenden EIN gemeinsames Stepper-Objekt für alle 3 Pumpen
// (gemeinsamer STEP/DIR Pin). Die Pumpen werden durch ENABLE-Pins unterschieden.
// Dieser Ansatz ist vom funktionierenden Dosierpumpen-Code übernommen.

// Helfer-Funktion zum Aktivieren einer bestimmten Pumpe
void activatePump(int pumpIndex) {
  // Alle Pumpen deaktivieren
  digitalWrite(ENABLE_SAMPLE, HIGH);
  digitalWrite(ENABLE_REAGENT, HIGH);
  digitalWrite(ENABLE_RINSE, HIGH);
  vTaskDelay(pdMS_TO_TICKS(50));  // Hardware-Stabilisierung

  // Gewünschte Pumpe aktivieren
  switch(pumpIndex) {
    case 1:
      digitalWrite(ENABLE_SAMPLE, LOW);
      activePumpIndex = 0;
      break;
    case 2:
      digitalWrite(ENABLE_REAGENT, LOW);
      activePumpIndex = 1;
      break;
    case 3:
      digitalWrite(ENABLE_RINSE, LOW);
      activePumpIndex = 2;
      break;
    default:
      activePumpIndex = -1;
      break;
  }
}

void deactivateAllPumps() {
  digitalWrite(ENABLE_SAMPLE, HIGH);
  digitalWrite(ENABLE_REAGENT, HIGH);
  digitalWrite(ENABLE_RINSE, HIGH);
  activePumpIndex = -1;
  // Kurze Pause für Hardware-Stabilisierung (nicht-blockierend)
  vTaskDelay(pdMS_TO_TICKS(10));
}

// Hilfsfunktion: Gibt stepsPerML für eine Pumpe zurück
float getStepsPerML(int pumpNumber) {
  switch(pumpNumber) {
    case 1: return pumpCal.stepsPerML_Sample;
    case 2: return pumpCal.stepsPerML_Reagent;
    case 3: return pumpCal.stepsPerML_Rinse;
    default: return 200.0;  // Fallback
  }
}

void pumpVolume(int pumpNumber, float volumeML, bool reverse) {
  if (!stepper) {
    Serial.println("❌ FEHLER: Stepper nicht initialisiert!");
    return;
  }

  // Berechne Schritte basierend auf Kalibrierung
  long steps;
  int speed, accel;
  switch(pumpNumber) {
    case 1:
      steps = volumeML * pumpCal.stepsPerML_Sample;
      speed = pumpCal.speed_Sample;
      accel = pumpCal.accel_Sample;
      break;
    case 2:
      steps = volumeML * pumpCal.stepsPerML_Reagent;
      speed = pumpCal.speed_Reagent;
      accel = pumpCal.accel_Reagent;
      break;
    case 3:
      steps = volumeML * pumpCal.stepsPerML_Rinse;
      speed = pumpCal.speed_Rinse;
      accel = pumpCal.accel_Rinse;
      break;
    default:
      steps = volumeML * 200;
      speed = 1000;
      accel = 500;
      break;
  }

  // Pumpe aktivieren
  activatePump(pumpNumber);
  stepper->setSpeedInHz(speed);
  stepper->setAcceleration(accel);

  // Position-Handling: Nur Säurepumpe (2) behält Position für Tracking
  if (pumpNumber != 2) {
    stepper->setCurrentPosition(0);
  }

  // Bewegung starten
  if (reverse) {
    stepper->move(-steps);
  } else {
    stepper->move(steps);
  }

  // Warten bis Bewegung abgeschlossen (oder Stop angefordert)
  // WICHTIG: Lange vTaskDelay(100) statt kurze 5ms Schleifen, damit
  // async_tcp auf CPU 1 genug Zeit für Netzwerk-Operationen bekommt.
  // FastAccelStepper erzeugt Steps per Hardware-Timer — die Präzision
  // ist unabhängig davon wie oft wir isRunning() prüfen.
  unsigned long loopStartTime = millis();
  unsigned long lastStatusTime = loopStartTime;
  while (stepper->isRunning()) {
    // HARD STOP: Sofort abbrechen wenn Stop angefordert
    if (stopRequested) {
      Serial.println("  ⚠ STOP angefordert - Pumpe wird abgebrochen!");
      stepper->forceStopAndNewPosition(0);
      deactivateAllPumps();
      return;  // Sofort raus
    }

    // 100ms Pause — gibt async_tcp/WiFi ausreichend CPU-Zeit auf CPU 1
    vTaskDelay(pdMS_TO_TICKS(100));

    // Status-Ausgabe alle 2 Sekunden
    if (millis() - lastStatusTime >= 2000) {
      lastStatusTime = millis();
      Serial.printf("  → [%lu ms] Pumpe läuft...\n", millis() - loopStartTime);
    }
  }

  // Pumpe deaktivieren
  deactivateAllPumps();
  vTaskDelay(pdMS_TO_TICKS(100));  // Stabilisierungszeit
}

// ==================== DYNAMISCHE TITRATIONSPARAMETER ====================
// Berechnet interpolierte Parameter basierend auf Abstand zum Ziel-pH
TitrationParams calculateTitrationParams(float currentPH, float targetPH, float initialPHDistance) {
  TitrationParams params;

  float distance = currentPH - targetPH;
  if (distance < 0) distance = 0;
  if (initialPHDistance <= 0) initialPHDistance = 1.0; // Sicherheit

  // progress: 0.0 = Start (weit weg), 1.0 = am Ziel
  float progress = 1.0 - (distance / initialPHDistance);
  if (progress < 0.0) progress = 0.0;
  if (progress > 1.0) progress = 1.0;

  // Exponentielle Interpolation: feine Kontrolle nahe dem Endpunkt
  float factor = pow(progress, settings.interpolationExponent);

  // doseVolume: max → min (kleiner nahe Ziel)
  params.doseVolume = settings.doseVolumeMax - factor * (settings.doseVolumeMax - settings.doseVolumeMin);

  // stabilityTime: min → max (länger nahe Ziel = mehr Datenpunkte)
  params.stabilityTime = settings.stabilityTimeMin + factor * (settings.stabilityTimeMax - settings.stabilityTimeMin);

  // tolerance: max → min (strenger nahe Ziel)
  params.tolerance = settings.toleranceMax - factor * (settings.toleranceMax - settings.toleranceMin);

  return params;
}

// ==================== SÄURE DOSIEREN (Volumetrisch) ====================
// Dosiert ein präzises Volumen Säure (Spritze ist unter Wasser getaucht)
// Rückgabe: true wenn erfolgreich, false wenn gestoppt
bool doseAcidVolume(float volumeML) {
  if (!stepper || volumeML <= 0) return false;

  // Säure direkt pumpen (Spritze ist unter Wasser)
  pumpVolume(2, volumeML);

  // Check for stop request
  if (stopRequested) return false;

  // Tracking aktualisieren
  long doseSteps = round(volumeML * pumpCal.stepsPerML_Reagent);
  currentTitration.totalSteps += doseSteps;
  currentTitration.acidUsed = (float)currentTitration.totalSteps / pumpCal.stepsPerML_Reagent;

  // Behälter-Füllstandsüberwachung: Säure verbraucht
  float acidDifference = currentTitration.acidUsed - currentTitration.lastAcidUsed;
  currentTitration.lastAcidUsed = currentTitration.acidUsed;
  if (acidDifference > 0) {
    trackAcidUsed(acidDifference);
  }

  return true;
}

// ==================== MESSZUSTANDSMASCHINE ====================
void handleMeasurementStateMachine() {
  // Prüfe ob Stop angefordert wurde (von stopMeasurement() gesetzt)
  if (stopRequested && currentState != IDLE) {
    Serial.println("[STATE-MACHINE] Stop-Request erkannt - überspringe Ausführung");
    return;  // Keine weiteren State-Aktionen ausführen
  }

  switch(currentState) {
    case IDLE:
      // Warten auf Startbefehl
      break;
      
    case RINSING: {
      if (stateStartTime == 0) {
        stateStartTime = millis();
        logState("RINSING");
        stateDescription = "Spüle Messkammer...";
        logHeader("PHASE 1: SPÜLEN & SÄURE-TEST");
      }

      // === SCHRITT 1: Zusatzwasser + Rührer AN + Säure-Pumpe primen ===
      // rinseVolume Wasser bereits im Behälter vom CLEANUP
      logInfo("Schritt 1/5: Rührer starten + Säure-Pumpe primen");

      // Zusatzwasser: Spritze muss unter Wasser sein für Priming
      if (settings.submersionVolume > 0) {
        Serial.printf("  → Zusatzwasser: %.1f ml (Spritze untertauchen)\n", settings.submersionVolume);
        pumpVolume(1, settings.submersionVolume);
        trackAquariumUsed(settings.submersionVolume);
      }
      stirrerOn();
      logSuccess("Rührer aktiv");
      vTaskDelay(pdMS_TO_TICKS(1000));

      // Säure-Tracking zurücksetzen für Priming
      currentTitration.acidUsed = 0;
      currentTitration.lastAcidUsed = 0;
      currentTitration.totalSteps = 0;

      if (settings.acidPrimeVolume > 0) {
        Serial.printf("  → Prime Säure-Pumpe mit %.3f ml\n", settings.acidPrimeVolume);
        pumpVolume(2, settings.acidPrimeVolume);
        logSuccess("Säure-Pumpe geprimed");
      }

      // === SCHRITT 2: 10 Sekunden weiter rühren ===
      logInfo("Schritt 2/5: Rühren (10s)");
      vTaskDelay(pdMS_TO_TICKS(10000));

      // Rührer AUS vor Abpumpen (nie trocken laufen lassen!)
      stirrerOff();
      logSuccess("Rührer gestoppt");
      vTaskDelay(pdMS_TO_TICKS(500));

      // === SCHRITT 3: Erstes Abpumpen (Spülwasser + Priming-Säure) ===
      logInfo("Schritt 3/5: Erstes Abpumpen (Spülwasser + Priming-Säure)");
      float primeAcidUsed = settings.acidPrimeVolume;
      float firstPumpVol = settings.rinseVolume + settings.submersionVolume + primeAcidUsed;
      float firstPumpVolOver = firstPumpVol * (1.0 + settings.overpumpPercent / 100.0);
      Serial.printf("  → Pumpe %.2f ml ab (%.1f ml + %.1f ml Zusatz + %.3f ml Säure + %.0f%% Überpumpen)\n",
                    firstPumpVolOver, settings.rinseVolume, settings.submersionVolume, primeAcidUsed, settings.overpumpPercent);
      pumpVolume(3, firstPumpVolOver, true);
      trackWasteAdded(firstPumpVol);
      logSuccess("Erstes Abpumpen fertig");
      vTaskDelay(pdMS_TO_TICKS(1000));

      // === SCHRITT 4: Frisches Spülwasser + 20s Rühren ===
      logInfo("Schritt 4/5: Spülwasser einfüllen + Rühren (20s)");
      Serial.printf("  → Pumpe %.1f ml Probewasser (Pumpe 1)\n", settings.rinseVolume);
      pumpVolume(1, settings.rinseVolume);
      trackAquariumUsed(settings.rinseVolume);
      logSuccess("Spülwasser eingefüllt");

      // Rührer AN für Spülphase
      stirrerOn();
      logSuccess("Rührer aktiv - Spüle 20 Sekunden");
      vTaskDelay(pdMS_TO_TICKS(20000));

      // Rührer AUS vor Abpumpen
      stirrerOff();
      logSuccess("Rührer gestoppt");
      vTaskDelay(pdMS_TO_TICKS(500));

      // === SCHRITT 5: Zweites Abpumpen (Spülwasser) ===
      logInfo("Schritt 5/5: Zweites Abpumpen (Spülwasser)");
      float secondPumpVolOver = settings.rinseVolume * (1.0 + settings.overpumpPercent / 100.0);
      Serial.printf("  → Pumpe %.2f ml ab (inkl. %.0f%% Überpumpen)\n",
                    secondPumpVolOver, settings.overpumpPercent);
      pumpVolume(3, secondPumpVolOver, true);
      trackWasteAdded(settings.rinseVolume);
      logSuccess("Zweites Abpumpen fertig - Behälter gespült");
      vTaskDelay(pdMS_TO_TICKS(1000));

      stateStartTime = 0;
      currentState = FILLING_SAMPLE;
      break;
    }
      
    case FILLING_SAMPLE:
      if (stateStartTime == 0) {
        stateStartTime = millis();
        logState("FILLING_SAMPLE");
        stateDescription = "Fülle Probe...";
        logHeader("PHASE 2: PROBENWASSER EINFÜLLEN");
        Serial.printf("  → Sollvolumen: %.1f ml\n", settings.sampleVolume);
      }

      // Prüfe ob Probevolumen überhaupt in Behälter passt
      if (settings.sampleVolume > settings.maxContainerVolume) {
        logError("Probenvolumen größer als Behälter!");
        Serial.printf("  → Probe: %.1f ml, Max: %.1f ml\n",
                      settings.sampleVolume, settings.maxContainerVolume);
        stateStartTime = 0;
        currentState = ERROR_STATE;
        stateDescription = "FEHLER: Probenvolumen zu groß";
        break;
      }

      logInfo("Pumpe Probenwasser (Pumpe 1)");
      pumpVolume(1, settings.sampleVolume);
      trackAquariumUsed(settings.sampleVolume);  // Behälter-Tracking: Aus Aquarium entnommen
      logSuccess("Probenwasser eingefüllt");

      // Säure-Tracking für Titration zurücksetzen (war vorher für Priming verwendet)
      currentTitration.acidUsed = 0;
      currentTitration.lastAcidUsed = 0;
      currentTitration.totalSteps = 0;

      // Rührer starten
      logInfo("Starte Magnetrührer");
      stirrerOn();
      logSuccess("Magnetrührer aktiv");

      stateStartTime = 0;
      currentState = MEASURING_INITIAL_PH;
      resetPHStability();
      break;
      
    case MEASURING_INITIAL_PH: {
      if (stateStartTime == 0) {
        stateStartTime = millis();
        logState("MEASURING_INITIAL_PH");
        int acclimatizationSeconds = settings.phAcclimatizationTime / 1000;
        stateDescription = String("pH-Akklimatisierung (") + String(acclimatizationSeconds) + String("s)...");
        logHeader("PHASE 3: PH-AKKLIMATISIERUNG & MESSUNG");
        Serial.printf("  → Warte %ds für pH-Stabilisierung und Rührer-Mischung\n", acclimatizationSeconds);
        Serial.printf("  → Aktueller pH: %.3f\n", phValue);

        // Titrationsdaten zurücksetzen für saubere Chart-Darstellung
        currentTitration.totalSteps = 0;
        currentTitration.acidUsed = 0.0;
        currentTitration.lastAcidUsed = 0.0;  // Wichtig für Füllstands-Differenz-Tracking

        // Stepper-Position auf 0 setzen für neue Messung
        if (stepper) {
          stepper->setCurrentPosition(0);
        }

        Serial.println("[CHART] Titrationsdaten zurückgesetzt für Chart");
      }

      // Konfigurierbare Wartezeit für Akklimatisierung
      unsigned long elapsedMS = millis() - stateStartTime;
      unsigned long elapsed = elapsedMS / 1000;
      unsigned long totalSeconds = settings.phAcclimatizationTime / 1000;

      if (millis() - lastProgressLog > 5000) {  // Alle 5 Sekunden Status
        Serial.printf("  ⏳ Akklimatisierung... (%lus/%lus) | pH: %.3f\n", elapsed, totalSeconds, phValue);
        lastProgressLog = millis();
      }

      if (elapsedMS >= settings.phAcclimatizationTime) {
        // Akklimatisierungszeit abgelaufen - jetzt auch auf pH-Stabilität prüfen
        if (!checkPHStability()) {
          // pH schwankt noch → weiter warten
          if (millis() - lastProgressLog > 5000) {
            Serial.printf("  ⏳ Warte auf pH-Stabilität... | pH: %.3f\n", phValue);
            lastProgressLog = millis();
          }
          // Sicherheits-Timeout: Max. 60s extra auf Stabilität warten
          if (elapsedMS > settings.phAcclimatizationTime + 60000) {
            Serial.println("  ⚠ pH nicht stabil nach 60s extra - verwende aktuellen Wert");
            // Fallthrough → pH trotzdem übernehmen
          } else {
            break;  // Weiter warten
          }
        }

        // pH ist stabil (oder Timeout) → Initial-pH übernehmen
        currentTitration.initialPH = phValue;
        currentTitration.initialPHDistance = phValue - settings.targetPH;
        logSuccess(String(String(totalSeconds) + "s Akklimatisierung abgeschlossen").c_str());
        Serial.printf("  → Initial-pH gemessen: %.3f (stabil), Distanz: %.3f\n",
                      currentTitration.initialPH, currentTitration.initialPHDistance);

        // Stepper-Position zurücksetzen vor Titration
        if (stepper) {
          stepper->setCurrentPosition(0);
          Serial.println("[STEPPER] Position auf 0 zurückgesetzt für Titration");
        }

        stateStartTime = 0;
        currentState = TITRATING;
        currentTitration.startTime = millis();
        // WICHTIG: totalSteps NICHT zurücksetzen!
        // Wurde bereits in MEASURING_INITIAL_PH auf 0 gesetzt
        // pH-Stabilitätsprüfung für Titration vorbereiten
        resetPHStability();
      }
      break;
    }
      
    case TITRATING:
      if (stateStartTime == 0) {
        stateStartTime = millis();
        logState("TITRATING");
        stateDescription = "Titriere...";
        logHeader("PHASE 4: DYNAMISCHE TITRATION");
        Serial.printf("  → Ziel-pH: %.2f\n", settings.targetPH);
        Serial.printf("  → Initial-pH: %.3f (Distanz: %.3f)\n",
                      currentTitration.initialPH, currentTitration.initialPHDistance);
        Serial.printf("  → Dosis: %.3f - %.3f ml\n", settings.doseVolumeMin, settings.doseVolumeMax);
        Serial.printf("  → Auswertungszeitraum: %.0f - %.0f s\n", settings.stabilityTimeMin, settings.stabilityTimeMax);
        Serial.printf("  → Toleranz: %.3f - %.3f pH\n", settings.toleranceMin, settings.toleranceMax);
        Serial.printf("  → Exponent: %.1f\n", settings.interpolationExponent);
        Serial.printf("  → Max. Wartezeit/Schritt: %d Sekunden\n", settings.maxStabilityTimeout);

        // Rührer läuft bereits seit FILLING_SAMPLE

        // WICHTIG: Stepper-Position zurücksetzen vor Titration
        if (stepper) {
          stepper->setCurrentPosition(0);
          Serial.println("  → Stepper-Position auf 0 zurückgesetzt für Titration");
        }

        // Dosierprotokoll und Timeout-Flag zurücksetzen
        currentTitration.stepCount = 0;
        currentTitration.stabilityTimeoutOccurred = false;
      }

      {
        // Dynamische Parameter berechnen
        TitrationParams params = calculateTitrationParams(phValue, settings.targetPH, currentTitration.initialPHDistance);

        // Prüfe ob Ziel-pH erreicht - aber NUR wenn pH auch stabil ist!
        if (phValue <= settings.targetPH) {
          if (checkPHStability(params.tolerance, params.stabilityTime)) {
            // pH ist stabil UND unter Ziel → Titration wirklich fertig
            currentTitration.finalPH = phValue;
            Serial.println();
            logSuccess("Ziel-pH erreicht (stabil bestätigt)!");
            Serial.printf("  → Final-pH: %.3f (Ziel: %.2f)\n", phValue, settings.targetPH);
            Serial.printf("  → Säure verwendet: %.3f ml\n", currentTitration.acidUsed);

            stateStartTime = 0;
            currentState = CALCULATING;
          } else {
            // pH unter Ziel aber noch nicht stabil → warten
            break;
          }
        }
        // Überlaufschutz
        else if (checkTitrationOverflow()) {
          Serial.println();
          logError("ÜBERLAUFSCHUTZ: Behälter voll!");
          float totalVol = settings.sampleVolume + currentTitration.acidUsed;
          Serial.printf("  → Probe: %.1f ml + Säure: %.2f ml = %.2f ml (Max: %.1f ml)\n",
                        settings.sampleVolume, currentTitration.acidUsed, totalVol, settings.maxContainerVolume);
          stateStartTime = 0;
          currentState = ERROR_STATE;
          stateDescription = "FEHLER: Behälter überlaufen verhindert";
        }
        // Nächster Dosier-Schritt (dynamisch)
        else {
          // Warte auf pH-Stabilität nach letzter Dosis
          if (currentTitration.acidUsed > 0 && !checkPHStability(params.tolerance, params.stabilityTime)) {
            break; // Noch nicht stabil
          }

          // Aktuelle Parameter neu berechnen (pH könnte sich geändert haben)
          params = calculateTitrationParams(phValue, settings.targetPH, currentTitration.initialPHDistance);

          float doseVolume = params.doseVolume;

          // Überlaufschutz: Dosis begrenzen wenn nötig
          float nextTotal = settings.sampleVolume + currentTitration.acidUsed + doseVolume;
          if (nextTotal > settings.maxContainerVolume) {
            doseVolume = settings.maxContainerVolume - settings.sampleVolume - currentTitration.acidUsed;
            if (doseVolume < 0.001) break; // Kein Platz mehr
            Serial.printf("  → Dosis begrenzt auf %.4f ml (Überlaufschutz)\n", doseVolume);
          }

          unsigned long elapsed = (millis() - currentTitration.startTime) / 1000;
          Serial.printf("\n📦 Dosis | pH: %.3f → %.2f | %.4f ml | Auswertung: %.0fs | Toleranz: %.3f | Zeit: %lus\n",
                        phValue, settings.targetPH, doseVolume, params.stabilityTime, params.tolerance, elapsed);

          // Säure dosieren (Spritze ist unter Wasser getaucht)
          if (!doseAcidVolume(doseVolume)) {
            break; // Stop angefordert
          }

          Serial.printf("  → Gesamt: %.3f ml Säure | %ld Steps\n",
                        currentTitration.acidUsed, currentTitration.totalSteps);

          // Dosierprotokoll: Schritt erfassen
          if (currentTitration.stepCount < MAX_TITRATION_STEPS) {
            TitrationStep& step = currentTitration.steps[currentTitration.stepCount];
            step.phBefore = phValue;
            step.doseVolume = doseVolume;
            step.stabilityTime = (currentTitration.stepCount == 0) ? 0 : lastStabilityDuration;
            step.tolerance = params.tolerance;
            currentTitration.stepCount++;
          }

          // pH-Stabilitätspuffer zurücksetzen (Wartezeit ist in checkPHStability() enthalten)
          resetPHStability();
        }
      }
      break;
      
    case CALCULATING: {
      logState("CALCULATING");
      stateDescription = "Berechne KH-Wert...";
      logHeader("PHASE 5: KH-BERECHNUNG");

      // Säureverbrauch berechnen
      currentTitration.acidUsed = (float)currentTitration.totalSteps / pumpCal.stepsPerML_Reagent;
      logInfo("Berechne Säureverbrauch");
      Serial.printf("  → Schritte gesamt: %ld\n", currentTitration.totalSteps);
      Serial.printf("  → Schritte/ml: %d\n", pumpCal.stepsPerML_Reagent);
      Serial.printf("  → Säure: %.3f ml\n", currentTitration.acidUsed);

      // KH-Wert berechnen (USGS-Formel)
      // KH [dKH] = (V_acid [ml] × c_acid [mol/L] × 1000) / V_sample [ml] × 2.8
      // Wir speichern den ROHWERT (ohne Korrekturfaktor) + den Faktor separat
      logInfo("Berechne KH-Wert (USGS-Formel)");

      // Rohwert ohne Korrekturfaktor
      float khRaw = (currentTitration.acidUsed *
                     settings.acidConcentration *
                     1000.0 /
                     settings.sampleVolume) * 2.8;

      // Korrigierter Wert für Anzeige
      float khCorrected = khRaw * settings.acidCorrectionFactor;

      // Speichere Rohwert (khValue) - Korrekturfaktor wird separat gespeichert
      currentTitration.khValue = khRaw;

      Serial.printf("  → Rohwert (ohne Faktor): %.2f dKH\n", khRaw);
      Serial.printf("  → Korrekturfaktor: %.3f\n", settings.acidCorrectionFactor);
      Serial.printf("  → Korrigierter Wert: %.2f dKH (%.2f × %.3f)\n",
                    khCorrected, khRaw, settings.acidCorrectionFactor);

      // Zeitstempel
      currentTitration.timestamp = getFormattedTime();
      currentTitration.valid = true;

      // Ergebnisse ausgeben
      Serial.println();
      Serial.println("╔════════════════════════════════════════════════════════╗");
      Serial.println("║              📊 MESSERGEBNIS                           ║");
      Serial.println("╠════════════════════════════════════════════════════════╣");
      Serial.printf("║  Zeitstempel:   %-38s║\n", currentTitration.timestamp.c_str());
      Serial.printf("║  Initial-pH:    %-38.3f║\n", currentTitration.initialPH);
      Serial.printf("║  Final-pH:      %-38.3f║\n", currentTitration.finalPH);
      Serial.printf("║  Säure:         %-35.3f ml║\n", currentTitration.acidUsed);
      Serial.printf("║  KH (Roh):      %-33.2f dKH║\n", khRaw);
      Serial.printf("║  Faktor:        %-38.3f║\n", settings.acidCorrectionFactor);
      Serial.printf("║  🎯 KH-Wert:    %-33.2f dKH║\n", khCorrected);
      Serial.println("╚════════════════════════════════════════════════════════╝");
      Serial.println();

      // Messung speichern
      saveMeasurement();

      // Ungesendete Messungen an Dosierpumpe senden (wenn aktiviert)
      if (settings.autoSendToPumps) {
        logInfo("Sende Messung an Dosierpumpe");
        sendUnsentMeasurements();
      }

      // Cleanup starten (Rührer läuft ja bereits seit MEASURING_INITIAL_PH)
      stateStartTime = 0;
      currentState = CLEANUP;
      break;
    }

    case CLEANUP: {
      if (stateStartTime == 0) {
        stateStartTime = millis();
        logState("CLEANUP");
        stateDescription = "Reinige Messkammer...";
        logHeader("PHASE 6: CLEANUP");
      }

      // 1. Rührer ausschalten
      logInfo("Schritt 1/3: Stoppe Rührer");
      stirrerOff();
      logSuccess("Rührer gestoppt");
      vTaskDelay(pdMS_TO_TICKS(1000));

      // 2. Benutzte Probe abpumpen (Probewasser + Säure + Überpumpen)
      logInfo("Schritt 2/3: Pumpe benutzte Probe ab");
      float totalVol = settings.sampleVolume + currentTitration.acidUsed;
      float pumpOutVol = totalVol * (1.0 + settings.overpumpPercent / 100.0);
      Serial.printf("  → Probe: %.1f ml + Säure: %.3f ml = %.3f ml gesamt\n",
                    settings.sampleVolume, currentTitration.acidUsed, totalVol);
      Serial.printf("  → Abpumpen: %.2f ml (inkl. %.0f%% Überpumpen)\n",
                    pumpOutVol, settings.overpumpPercent);
      pumpVolume(3, pumpOutVol, true); // Spül-Pumpe rückwärts
      trackWasteAdded(totalVol);  // Behälter-Tracking: Abwasser (OHNE Überpumpen)
      logSuccess("Probe abgepumpt");
      vTaskDelay(pdMS_TO_TICKS(2000));

      // 3. Frisches Spülwasser einfüllen (damit Sonde nicht trocken liegt)
      logInfo("Schritt 3/3: Fülle Spülwasser ein (Sonde feucht halten)");
      Serial.printf("  → Fülle %.1f ml Spülwasser ein\n", settings.rinseVolume);
      pumpVolume(1, settings.rinseVolume);
      trackAquariumUsed(settings.rinseVolume);  // Behälter-Tracking: Aus Aquarium entnommen
      logSuccess("Spülwasser eingefüllt");
      vTaskDelay(pdMS_TO_TICKS(1000));

      logSuccess("Cleanup abgeschlossen - Bereit für nächste Messung");
      Serial.println();

      stateStartTime = 0;
      currentState = COMPLETED;
      stateDescription = "Messung abgeschlossen";
      break;
    }

    case COMPLETED:
      if (stateStartTime == 0) {
        stateStartTime = millis();
        logState("COMPLETED");
        logSuccess("MESSUNG ERFOLGREICH ABGESCHLOSSEN");
        Serial.println("  → Warte auf neue Messanforderung oder Reset");

        // Messung als erfolgreich markieren (Freigabe für nächste Messung)
        settings.measurementCompleted = true;

        // Behälter-Füllstände nach erfolgreicher Messung speichern
        saveContainerLevels();  // ruft saveSettings() auf → measurementCompleted wird gespeichert
      }
      // Warten auf Bestätigung oder neue Messung
      break;
      
    case ERROR_STATE:
      if (stateStartTime == 0) {
        stateStartTime = millis();
        Serial.println();
        Serial.println("╔════════════════════════════════════════════════════════╗");
        Serial.println("║              ❌ FEHLER AUFGETRETEN                     ║");
        Serial.println("╠════════════════════════════════════════════════════════╣");
        Serial.printf("║  Fehlermeldung: %-38s║\n", stateDescription.c_str());
        Serial.println("╚════════════════════════════════════════════════════════╝");
        Serial.println();

        logWarning("Führe Notfall-Shutdown durch");

        // Stoppe Rührer
        logInfo("1. Stoppe Rührer");
        stirrerOff();
        logSuccess("Rührer gestoppt");

        // Stoppe Stepper und deaktiviere alle Pumpen
        logInfo("2. Stoppe alle Pumpen");
        if (stepper) stepper->forceStopAndNewPosition(0);
        deactivateAllPumps();
        logSuccess("Alle Pumpen gestoppt");

        Serial.println();
        logError("Messung abgebrochen!");
        logWarning("Behälter manuell leeren falls nötig!");
        logWarning("Bitte Fehlerursache beheben und erneut versuchen");
        Serial.println("  → Mögliche Ursachen:");
        Serial.println("    • Säure-Pumpe verstopft oder leer");
        Serial.println("    • pH-Sensor nicht kalibriert oder defekt");
        Serial.println("    • Behälter zu klein für Probenvolumen");
        Serial.println();
        Serial.println("  → System bleibt in ERROR_STATE bis manuelle Reset-Anforderung");
        Serial.println();

        // WICHTIG: stateStartTime NICHT zurücksetzen, damit diese Meldung nur 1x kommt!
        // stateStartTime bleibt != 0, daher wird dieser Block nicht erneut ausgeführt
      }
      // ERROR_STATE bleibt aktiv bis manueller Reset über UI
      break;
  }
}

void startMeasurement() {
  // Sicherheitsprüfung: Letzte Messung muss abgeschlossen sein
  if (!settings.measurementCompleted) {
    logWarning("Messung blockiert - letzte Messung nicht abgeschlossen. Bitte Behälter prüfen und freigeben.");
    return;
  }

  if (currentState == IDLE || currentState == COMPLETED || currentState == ERROR_STATE) {
    // Flag setzen: Messung läuft (wird bei COMPLETED wieder auf true gesetzt)
    settings.measurementCompleted = false;
    saveSettings();

    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║          🚀 NEUE KH-MESSUNG GESTARTET                  ║");
    Serial.println("╠════════════════════════════════════════════════════════╣");
    Serial.printf("║  Zeitstempel:        %-33s║\n", getFormattedTime().c_str());
    Serial.printf("║  Probenvolumen:      %-30.1f ml║\n", settings.sampleVolume);
    Serial.printf("║  Säure-Konz.:        %-29.2f M║\n", settings.acidConcentration);
    Serial.printf("║  Korrektur-Faktor:   %-33.3f║\n", settings.acidCorrectionFactor);
    Serial.printf("║  Effektive Konz.:    %-28.4f M║\n", settings.acidConcentration * settings.acidCorrectionFactor);
    Serial.printf("║  Ziel-pH:            %-33.2f║\n", settings.targetPH);
    Serial.printf("║  Spülvolumen:        %-30.1f ml║\n", settings.rinseVolume);
    Serial.printf("║  Überpumpen:         %-30.0f %%║\n", settings.overpumpPercent);
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.println();

    currentState = RINSING;
    currentTitration = TitrationData(); // Reset
    stateStartTime = 0;  // WICHTIG: State-Timer zurücksetzen
  } else {
    logWarning("Messung bereits aktiv - Ignoriere Startbefehl");
  }
}

void stopMeasurement() {
  Serial.println();
  logHeader("MESSUNG STOPPEN (HARD STOP)");
  logWarning("Benutzer-Abbruch - Sofortiger Abbruch aller Aktionen");

  // 1. ZUERST stopRequested setzen - damit alle blockierenden Schleifen abbrechen!
  stopRequested = true;

  // 2. Rührer sofort ausschalten (ohne Rampe!)
  stirrerOff(true);  // hardStop = true

  // 3. Alle Pumpen sofort stoppen (hart)
  if (stepper) {
    stepper->forceStopAndNewPosition(0);
  }
  deactivateAllPumps();

  logSuccess("✓ Alle Pumpen und Rührer gestoppt");

  // 4. Kurz warten damit alle blockierenden Schleifen das stopRequested sehen
  vTaskDelay(pdMS_TO_TICKS(50));

  // 5. Status auf IDLE setzen (bereit für neue Messung)
  currentState = IDLE;
  stateDescription = "ABGEBROCHEN - Behälter manuell leeren!";
  stateStartTime = 0;

  // 6. stopRequested zurücksetzen (für nächste Messung)
  stopRequested = false;

  Serial.println("════════════════════════════════════════");
  Serial.println("✓ MESSUNG HART ABGEBROCHEN");
  Serial.println("⚠ Behälter muss manuell geleert werden!");
  Serial.println("════════════════════════════════════════\n");
}

// ==================== WIFI-KONFIGURATION ====================
void loadWiFiConfig() {
  if (!LittleFS.exists("/wifi.json")) {
    Serial.println("Keine WiFi-Konfiguration gefunden");
    return;
  }

  File file = LittleFS.open("/wifi.json", "r");
  if (!file) return;

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("WiFi-Config JSON Fehler");
    return;
  }

  wifiSSID = doc["ssid"].as<String>();
  wifiPassword = doc["password"].as<String>();
  wifiConfigured = doc["configured"] | false;

  Serial.printf("WiFi-Config geladen: %s\n", wifiSSID.c_str());
}

void saveWiFiConfig() {
  DynamicJsonDocument doc(1024);
  doc["ssid"] = wifiSSID;
  doc["password"] = wifiPassword;
  doc["configured"] = wifiConfigured;

  File file = LittleFS.open("/wifi.json", "w");
  if (!file) {
    Serial.println("FEHLER: Kann WiFi-Config nicht speichern");
    return;
  }

  serializeJson(doc, file);
  file.flush();  // WICHTIG: Buffer leeren vor close()
  file.close();
  Serial.println("✓ WiFi-Config gespeichert");
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP);

  // WICHTIG: WiFi Sleep deaktivieren für Cache-Stabilität
  WiFi.setSleep(false);

  WiFi.softAP("ESP32-KH-Tester", "");
  apMode = true;
  Serial.println("Access Point gestartet");
  Serial.println("SSID: ESP32-KH-Tester");
  Serial.println("IP: 192.168.4.1");
}

void startWiFiClient() {
  WiFi.mode(WIFI_STA);

  // WICHTIG: WiFi Sleep deaktivieren für Cache-Stabilität
  // Verhindert "Cache disabled but cached memory region accessed" während Stepper läuft
  WiFi.setSleep(false);

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  Serial.printf("Verbinde mit %s", wifiSSID.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    Serial.println("\n✓ WiFi verbunden");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // NTP-Zeit initialisieren
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Warte kurz auf NTP-Synchronisation
    Serial.println("Warte auf NTP-Zeit...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    // NTP → RTC Synchronisation
    if (rtcAvailable) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        // NTP-Zeit erfolgreich abgerufen, setze RTC
        rtc.adjust(DateTime(timeinfo.tm_year + 1900,
                           timeinfo.tm_mon + 1,
                           timeinfo.tm_mday,
                           timeinfo.tm_hour,
                           timeinfo.tm_min,
                           timeinfo.tm_sec));
        Serial.println("✓ RTC mit NTP synchronisiert");
        Serial.printf("Zeit: %04d-%02d-%02d %02d:%02d:%02d\n",
                     timeinfo.tm_year + 1900,
                     timeinfo.tm_mon + 1,
                     timeinfo.tm_mday,
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
      }
    }
    timeInitialized = true;
  } else {
    Serial.println("\n✗ WiFi-Verbindung fehlgeschlagen");
    startAccessPoint();
  }
}

// ==================== WIFI-VERBINDUNGSÜBERWACHUNG ====================
// Prüft periodisch ob WiFi noch verbunden ist und verbindet bei Bedarf neu
void checkWiFiConnection() {
  // Nur im Client-Modus (nicht im AP-Modus)
  if (apMode || !wifiConfigured) return;

  static unsigned long lastWiFiCheck = 0;
  static unsigned long disconnectedSince = 0;
  static int reconnectAttempts = 0;

  // Nur alle 5 Sekunden prüfen
  if (millis() - lastWiFiCheck < 5000) return;
  lastWiFiCheck = millis();

  if (WiFi.status() == WL_CONNECTED) {
    // Alles OK - Reset der Disconnect-Tracker
    if (disconnectedSince > 0) {
      Serial.printf("[WIFI] Wieder verbunden! (IP: %s, war %lu s getrennt)\n",
                    WiFi.localIP().toString().c_str(),
                    (millis() - disconnectedSince) / 1000);
      disconnectedSince = 0;
      reconnectAttempts = 0;
    }
    return;
  }

  // WiFi ist nicht verbunden
  if (disconnectedSince == 0) {
    disconnectedSince = millis();
    Serial.println("[WIFI] Verbindung verloren!");
  }

  reconnectAttempts++;

  // Reconnect-Strategie: Erst sanft, dann härter
  if (reconnectAttempts <= 3) {
    // Sanfter Reconnect: WiFi.reconnect()
    Serial.printf("[WIFI] Reconnect-Versuch %d (sanft)...\n", reconnectAttempts);
    WiFi.reconnect();
  } else if (reconnectAttempts <= 6) {
    // Harter Reconnect: disconnect + begin
    Serial.printf("[WIFI] Reconnect-Versuch %d (Neustart)...\n", reconnectAttempts);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  } else {
    // Kompletter WiFi-Neustart alle 30 Sekunden
    if (reconnectAttempts % 6 == 0) {
      Serial.printf("[WIFI] Reconnect-Versuch %d (kompletter Neustart)...\n", reconnectAttempts);
      WiFi.mode(WIFI_OFF);
      vTaskDelay(pdMS_TO_TICKS(500));
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(false);
      WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    }
  }
}

// ==================== ZEIT-FUNKTIONEN ====================
String getFormattedTime() {
  char buffer[30];

  // PRIORITÄT 1: RTC (wenn verfügbar)
  if (rtcAvailable) {
    DateTime now = rtc.now();

    // Plausibilitätsprüfung: Jahr muss >= 2024 sein (RTC nicht zurückgesetzt)
    if (now.year() >= 2024) {
      snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
               now.year(), now.month(), now.day(),
               now.hour(), now.minute(), now.second());
      return String(buffer);
    } else {
      Serial.println("⚠ RTC-Zeit ungültig (Jahr < 2024), nutze NTP-Fallback");
    }
  }

  // PRIORITÄT 2: NTP (Fallback wenn RTC nicht verfügbar oder ungültig)
  if (!timeInitialized) return "Keine Zeit";

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Zeitfehler";
  }

  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

// ==================== pH-KALIBRIERUNGSHISTORIE ====================
// Hilfsfunktion: Aktualisiert phCal mit den neuesten Werten aus der Historie
void updatePHCalFromHistory() {
  // pH4: Neuesten Wert nehmen (letzter im Array)
  if (phCalHistory.count_pH4 > 0) {
    int lastIdx = phCalHistory.count_pH4 - 1;
    phCal.voltage_pH4 = phCalHistory.calibrations_pH4[lastIdx].mV;
    phCal.timestamp_pH4 = phCalHistory.calibrations_pH4[lastIdx].timestamp;
  } else {
    phCal.voltage_pH4 = 3000.0;  // Standardwert
    phCal.timestamp_pH4 = "";
  }

  // pH7: Neuesten Wert nehmen (letzter im Array)
  if (phCalHistory.count_pH7 > 0) {
    int lastIdx = phCalHistory.count_pH7 - 1;
    phCal.voltage_pH7 = phCalHistory.calibrations_pH7[lastIdx].mV;
    phCal.timestamp_pH7 = phCalHistory.calibrations_pH7[lastIdx].timestamp;
  } else {
    phCal.voltage_pH7 = 2500.0;  // Standardwert
    phCal.timestamp_pH7 = "";
  }

  // isCalibrated nur wenn beide Werte vorhanden
  phCal.isCalibrated = (phCalHistory.count_pH4 > 0 && phCalHistory.count_pH7 > 0);
}

void loadPHCalibrationHistory() {
  // Standardwerte setzen
  phCalHistory.count_pH4 = 0;
  phCalHistory.count_pH7 = 0;

  // Standarddatum setzen (sicher mit strncpy)
  String defaultDate = getFormattedTime().substring(0, 10);
  strncpy(phCalHistory.sensorInstallDate, defaultDate.c_str(), sizeof(phCalHistory.sensorInstallDate) - 1);
  phCalHistory.sensorInstallDate[sizeof(phCalHistory.sensorInstallDate) - 1] = '\0';

  if (!LittleFS.exists("/ph_calibrations.json")) {
    Serial.println("⚠ ph_calibrations.json nicht gefunden - starte mit leerer Historie");
    updatePHCalFromHistory();
    return;
  }

  File file = LittleFS.open("/ph_calibrations.json", "r");
  if (!file) {
    Serial.println("FEHLER: Kann ph_calibrations.json nicht öffnen");
    updatePHCalFromHistory();
    return;
  }

  PsramJsonDocument doc(8192);  // Nutzt PSRAM - 8KB für 50+50 Einträge
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("FEHLER beim Parsen von ph_calibrations.json: ");
    Serial.println(error.c_str());
    updatePHCalFromHistory();
    return;
  }

  // sensorInstallDate laden (sicher mit strncpy)
  const char* installDate = doc["sensorInstallDate"] | defaultDate.c_str();
  strncpy(phCalHistory.sensorInstallDate, installDate, sizeof(phCalHistory.sensorInstallDate) - 1);
  phCalHistory.sensorInstallDate[sizeof(phCalHistory.sensorInstallDate) - 1] = '\0';

  // Referenz-Steigung laden
  phCalHistory.referenceSlope = doc["referenceSlope"] | 0.0f;

  // pH4 Kalibrierungen laden
  JsonArray arr4 = doc["calibrations_pH4"].as<JsonArray>();
  for (JsonObject entry : arr4) {
    if (phCalHistory.count_pH4 >= PH_CAL_HISTORY_MAX) break;
    const char* ts = entry["timestamp"] | "";
    strncpy(phCalHistory.calibrations_pH4[phCalHistory.count_pH4].timestamp, ts, PH_CAL_TIMESTAMP_LEN - 1);
    phCalHistory.calibrations_pH4[phCalHistory.count_pH4].timestamp[PH_CAL_TIMESTAMP_LEN - 1] = '\0';
    phCalHistory.calibrations_pH4[phCalHistory.count_pH4].mV = entry["mV"] | 0.0f;
    phCalHistory.count_pH4++;
  }

  // pH7 Kalibrierungen laden
  JsonArray arr7 = doc["calibrations_pH7"].as<JsonArray>();
  for (JsonObject entry : arr7) {
    if (phCalHistory.count_pH7 >= PH_CAL_HISTORY_MAX) break;
    const char* ts = entry["timestamp"] | "";
    strncpy(phCalHistory.calibrations_pH7[phCalHistory.count_pH7].timestamp, ts, PH_CAL_TIMESTAMP_LEN - 1);
    phCalHistory.calibrations_pH7[phCalHistory.count_pH7].timestamp[PH_CAL_TIMESTAMP_LEN - 1] = '\0';
    phCalHistory.calibrations_pH7[phCalHistory.count_pH7].mV = entry["mV"] | 0.0f;
    phCalHistory.count_pH7++;
  }

  // phCal mit neuesten Werten aktualisieren
  updatePHCalFromHistory();

  Serial.printf("✓ pH-Kalibrierungshistorie geladen: %d pH4, %d pH7 Einträge\n",
                phCalHistory.count_pH4, phCalHistory.count_pH7);
  if (phCal.isCalibrated) {
    Serial.printf("  pH-Kalibrierung aktiv: pH 4.0=%.1fmV, pH 7.0=%.1fmV\n",
                  phCal.voltage_pH4, phCal.voltage_pH7);
  } else {
    Serial.println("  pH-Sensor: Nicht vollständig kalibriert");
  }
}

void savePHCalibrationHistory() {
  PsramJsonDocument doc(8192);  // Nutzt PSRAM

  doc["sensorInstallDate"] = phCalHistory.sensorInstallDate;
  doc["referenceSlope"] = phCalHistory.referenceSlope;

  // pH4 Kalibrierungen speichern
  JsonArray arr4 = doc.createNestedArray("calibrations_pH4");
  for (int i = 0; i < phCalHistory.count_pH4; i++) {
    JsonObject entry = arr4.createNestedObject();
    entry["timestamp"] = phCalHistory.calibrations_pH4[i].timestamp;
    entry["mV"] = phCalHistory.calibrations_pH4[i].mV;
  }

  // pH7 Kalibrierungen speichern
  JsonArray arr7 = doc.createNestedArray("calibrations_pH7");
  for (int i = 0; i < phCalHistory.count_pH7; i++) {
    JsonObject entry = arr7.createNestedObject();
    entry["timestamp"] = phCalHistory.calibrations_pH7[i].timestamp;
    entry["mV"] = phCalHistory.calibrations_pH7[i].mV;
  }

  File file = LittleFS.open("/ph_calibrations.json", "w");
  if (!file) {
    Serial.println("FEHLER: Kann ph_calibrations.json nicht schreiben");
    return;
  }

  serializeJson(doc, file);
  file.close();
  Serial.println("✓ pH-Kalibrierungshistorie gespeichert");
}

void addPHCalibrationEntry(int phType, float mV) {
  String timestampStr = getFormattedTime();
  const char* timestamp = timestampStr.c_str();

  if (phType == 4) {
    // Wenn Array voll, ältesten Eintrag entfernen (am Index 0)
    if (phCalHistory.count_pH4 >= PH_CAL_HISTORY_MAX) {
      // Alle um eins nach vorne schieben (memmove ist sicherer als Schleife)
      memmove(&phCalHistory.calibrations_pH4[0], &phCalHistory.calibrations_pH4[1],
              (PH_CAL_HISTORY_MAX - 1) * sizeof(PHCalibrationEntry));
      phCalHistory.count_pH4 = PH_CAL_HISTORY_MAX - 1;
    }
    // Neuen Eintrag am Ende hinzufügen (sicher mit strncpy)
    strncpy(phCalHistory.calibrations_pH4[phCalHistory.count_pH4].timestamp, timestamp, PH_CAL_TIMESTAMP_LEN - 1);
    phCalHistory.calibrations_pH4[phCalHistory.count_pH4].timestamp[PH_CAL_TIMESTAMP_LEN - 1] = '\0';
    phCalHistory.calibrations_pH4[phCalHistory.count_pH4].mV = mV;
    phCalHistory.count_pH4++;
    Serial.printf("✓ pH4 Kalibrierung zur Historie hinzugefügt: %.2f mV @ %s\n", mV, timestamp);
  }
  else if (phType == 7) {
    // Wenn Array voll, ältesten Eintrag entfernen (am Index 0)
    if (phCalHistory.count_pH7 >= PH_CAL_HISTORY_MAX) {
      memmove(&phCalHistory.calibrations_pH7[0], &phCalHistory.calibrations_pH7[1],
              (PH_CAL_HISTORY_MAX - 1) * sizeof(PHCalibrationEntry));
      phCalHistory.count_pH7 = PH_CAL_HISTORY_MAX - 1;
    }
    // Neuen Eintrag am Ende hinzufügen (sicher mit strncpy)
    strncpy(phCalHistory.calibrations_pH7[phCalHistory.count_pH7].timestamp, timestamp, PH_CAL_TIMESTAMP_LEN - 1);
    phCalHistory.calibrations_pH7[phCalHistory.count_pH7].timestamp[PH_CAL_TIMESTAMP_LEN - 1] = '\0';
    phCalHistory.calibrations_pH7[phCalHistory.count_pH7].mV = mV;
    phCalHistory.count_pH7++;
    Serial.printf("✓ pH7 Kalibrierung zur Historie hinzugefügt: %.2f mV @ %s\n", mV, timestamp);
  }

  // Referenz-Steigung setzen wenn erste vollständige Kalibrierung
  updateReferenceSlope();

  // Sofort speichern
  savePHCalibrationHistory();
}

void deletePHCalibrationEntry(int phType, int index) {
  if (phType == 4 && index >= 0 && index < phCalHistory.count_pH4) {
    // Alle nach dem Index um eins nach vorne schieben (memmove für überlappende Bereiche)
    if (index < phCalHistory.count_pH4 - 1) {
      memmove(&phCalHistory.calibrations_pH4[index], &phCalHistory.calibrations_pH4[index + 1],
              (phCalHistory.count_pH4 - index - 1) * sizeof(PHCalibrationEntry));
    }
    phCalHistory.count_pH4--;
    updatePHCalFromHistory();  // phCal aktualisieren falls neuester Wert gelöscht wurde
    Serial.printf("✓ pH4 Kalibrierung #%d gelöscht\n", index);
    savePHCalibrationHistory();
  }
  else if (phType == 7 && index >= 0 && index < phCalHistory.count_pH7) {
    if (index < phCalHistory.count_pH7 - 1) {
      memmove(&phCalHistory.calibrations_pH7[index], &phCalHistory.calibrations_pH7[index + 1],
              (phCalHistory.count_pH7 - index - 1) * sizeof(PHCalibrationEntry));
    }
    phCalHistory.count_pH7--;
    updatePHCalFromHistory();  // phCal aktualisieren falls neuester Wert gelöscht wurde
    Serial.printf("✓ pH7 Kalibrierung #%d gelöscht\n", index);
    savePHCalibrationHistory();
  }
}

void resetPHCalibrationHistory() {
  phCalHistory.count_pH4 = 0;
  phCalHistory.count_pH7 = 0;
  phCalHistory.referenceSlope = 0;  // Referenz zurücksetzen - nächste Kalibrierung = 100%

  // Datum sicher setzen mit strncpy
  String dateStr = getFormattedTime().substring(0, 10);
  strncpy(phCalHistory.sensorInstallDate, dateStr.c_str(), sizeof(phCalHistory.sensorInstallDate) - 1);
  phCalHistory.sensorInstallDate[sizeof(phCalHistory.sensorInstallDate) - 1] = '\0';

  updatePHCalFromHistory();  // phCal zurücksetzen
  savePHCalibrationHistory();
  Serial.println("✓ pH-Kalibrierungshistorie zurückgesetzt (neue Sonde - nächste Kalibrierung = 100%)");
}

// Berechne aktuelle Steigung in mV/pH
float calculateCurrentSlope() {
  if (phCalHistory.count_pH4 == 0 || phCalHistory.count_pH7 == 0) {
    return 0;
  }

  // Neueste Werte nehmen
  float mV_pH4 = phCalHistory.calibrations_pH4[phCalHistory.count_pH4 - 1].mV;
  float mV_pH7 = phCalHistory.calibrations_pH7[phCalHistory.count_pH7 - 1].mV;

  // Steigung = Differenz / 3 pH-Einheiten
  return abs(mV_pH7 - mV_pH4) / 3.0;
}

// Berechne Sonden-Effizienz (verglichen mit Referenz-Steigung bei erster Kalibrierung)
float calculatePHSensorEfficiency() {
  if (phCalHistory.count_pH4 == 0 || phCalHistory.count_pH7 == 0) {
    return -1;  // Nicht genug Daten
  }

  float currentSlope = calculateCurrentSlope();

  // Wenn noch keine Referenz gespeichert, ist das die erste Kalibrierung = 100%
  if (phCalHistory.referenceSlope <= 0) {
    return 100.0;
  }

  // Effizienz = (aktuelle Steigung / Referenz-Steigung) * 100
  float efficiency = (currentSlope / phCalHistory.referenceSlope) * 100.0;

  // Debug-Ausgabe
  Serial.printf("[pH-Effizienz] Aktuelle Steigung: %.2f mV/pH, Referenz: %.2f mV/pH, Effizienz: %.1f%%\n",
                currentSlope, phCalHistory.referenceSlope, efficiency);

  return efficiency;
}

// Setze Referenz-Steigung (bei erster Kalibrierung nach Reset)
void updateReferenceSlope() {
  if (phCalHistory.count_pH4 > 0 && phCalHistory.count_pH7 > 0 && phCalHistory.referenceSlope <= 0) {
    phCalHistory.referenceSlope = calculateCurrentSlope();
    Serial.printf("[pH] Referenz-Steigung gesetzt: %.2f mV/pH (= 100%%)\n", phCalHistory.referenceSlope);
    savePHCalibrationHistory();
  }
}

// ==================== EINSTELLUNGEN ====================
void loadSettings() {
  if (!LittleFS.exists("/settings.json")) {
    Serial.println("⚠ settings.json nicht gefunden - verwende Standard-Einstellungen");
    return;
  }

  Serial.println("→ settings.json gefunden, lade Daten...");
  File file = LittleFS.open("/settings.json", "r");
  if (!file) {
    Serial.println("FEHLER: Kann settings.json nicht öffnen");
    return;
  }

  // DEBUG: Zeige Dateiinhalt
  String fileContent = file.readString();
  file.close();
  Serial.println("[DEBUG] Dateiinhalt:");
  Serial.println(fileContent);
  Serial.println("[DEBUG] Ende Dateiinhalt");

  DynamicJsonDocument doc(3072);  // Erhöht für Behälter-Füllstände
  DeserializationError error = deserializeJson(doc, fileContent);

  if (error) {
    Serial.printf("FEHLER: Settings JSON Parse-Fehler: %s\n", error.c_str());
    return;
  }
  Serial.printf("→ JSON erfolgreich geparst (%d bytes)\n", fileContent.length());
  
  settings.sampleVolume = doc["sampleVolume"] | 50.0;
  settings.rinseVolume = doc["rinseVolume"] | 10.0;
  settings.overpumpPercent = doc["overpumpPercent"] | 10.0;
  settings.acidConcentration = doc["acidConcentration"] | 0.1;
  settings.acidCorrectionFactor = doc["acidCorrectionFactor"] | 1.0;
  settings.targetPH = doc["targetPH"] | 4.5;

  // Dynamische Titrationsparameter laden
  settings.doseVolumeMin = doc["doseVolumeMin"] | 0.01;
  settings.doseVolumeMax = doc["doseVolumeMax"] | 0.5;
  settings.stabilityTimeMin = doc["stabilityTimeMin"] | 5.0;
  settings.stabilityTimeMax = doc["stabilityTimeMax"] | 30.0;
  settings.toleranceMin = doc["toleranceMin"] | 0.05;
  settings.toleranceMax = doc["toleranceMax"] | 0.3;
  settings.interpolationExponent = doc["interpolationExponent"] | 2.5;
  settings.acidPrimeVolume = doc["acidPrimeVolume"] | 0.2;
  settings.submersionVolume = doc["submersionVolume"] | 5.0;

  // Akklimatisierungszeit laden mit Validierung (10s bis 120s)
  int acclimatizationTime = doc["phAcclimatizationTime"] | 30000;
  if (acclimatizationTime < 10000) acclimatizationTime = 10000;    // Min 10 Sekunden
  if (acclimatizationTime > 120000) acclimatizationTime = 120000;  // Max 120 Sekunden (2 Minuten)
  settings.phAcclimatizationTime = acclimatizationTime;
  settings.maxStabilityTimeout = doc["maxStabilityTimeout"] | 120;
  settings.maxContainerVolume = doc["maxContainerVolume"] | 100.0;

  // Rührer-Geschwindigkeit laden mit Validierung
  if (doc.containsKey("stirrerSpeed")) {
    settings.stirrerSpeed = doc["stirrerSpeed"];
    // Wenn 0 geladen wurde, ist das wahrscheinlich ein alter Fehler
    if (settings.stirrerSpeed == 0) {
      settings.stirrerSpeed = 80;
      Serial.println("[SETTINGS] stirrerSpeed war 0 in Datei, korrigiere auf 80%");
    }
  } else {
    // Kein Wert in Datei -> Default
    settings.stirrerSpeed = 80;
    Serial.println("[SETTINGS] stirrerSpeed nicht in Datei, setze Default 80%");
  }
  settings.stirrerPwmFreq = doc["stirrerPwmFreq"] | STIRRER_PWM_FREQ_DEFAULT;

  // Auto-Messung Einstellungen laden (RTC-basiert)
  settings.autoMeasureEnabled = doc["autoMeasureEnabled"] | 0;
  settings.firstMeasurementHour = doc["firstMeasurementHour"] | 6;
  settings.firstMeasurementMinute = doc["firstMeasurementMinute"] | 0;
  settings.measurementIntervalHours = doc["measurementIntervalHours"] | 0;
  settings.measurementRepeatDays = doc["measurementRepeatDays"] | 1;
  settings.lastMeasurementUnix = doc["lastMeasurementUnix"] | 0;
  settings.measurementCompleted = doc["measurementCompleted"] | true;

  // Auto-Send Einstellungen laden
  settings.autoSendToPumps = doc["autoSendToPumps"] | false;
  const char* pumpsIP = doc["pumpsIP"] | "192.168.1.100";
  strncpy(settings.pumpsIP, pumpsIP, sizeof(settings.pumpsIP) - 1);
  settings.pumpsIP[sizeof(settings.pumpsIP) - 1] = '\0';

  // Pumpen-Kalibrierung laden (falls vorhanden)
  pumpCal.stepsPerML_Sample = doc["stepsPerML_Sample"] | 200;
  pumpCal.stepsPerML_Reagent = doc["stepsPerML_Reagent"] | 200;
  pumpCal.stepsPerML_Rinse = doc["stepsPerML_Rinse"] | 200;

  // Pumpen-Geschwindigkeiten und Beschleunigungen laden
  pumpCal.speed_Sample = doc["speed_Sample"] | 2000;
  pumpCal.speed_Reagent = doc["speed_Reagent"] | 1000;
  pumpCal.speed_Rinse = doc["speed_Rinse"] | 2000;
  pumpCal.accel_Sample = doc["accel_Sample"] | 1000;
  pumpCal.accel_Reagent = doc["accel_Reagent"] | 500;
  pumpCal.accel_Rinse = doc["accel_Rinse"] | 1000;

  // HINWEIS: pH-Kalibrierung wird jetzt aus ph_calibrations.json geladen (nicht mehr hier)

  // Behälter-Füllstandsüberwachung laden
  settings.acidContainerMax = doc["acidContainerMax"] | 1000.0;
  settings.acidContainerLevel = doc["acidContainerLevel"] | 1000.0;
  settings.wasteContainerMax = doc["wasteContainerMax"] | 2000.0;
  settings.wasteContainerLevel = doc["wasteContainerLevel"] | 0.0;
  settings.aquariumTotalUsed = doc["aquariumTotalUsed"] | 0.0;

  // Behälter-Timestamps laden
  const char* acidTs = doc["acidLastRefill"] | "";
  const char* wasteTs = doc["wasteLastEmpty"] | "";
  const char* aquariumTs = doc["aquariumLastReset"] | "";
  strncpy(settings.acidLastRefill, acidTs, sizeof(settings.acidLastRefill) - 1);
  strncpy(settings.wasteLastEmpty, wasteTs, sizeof(settings.wasteLastEmpty) - 1);
  strncpy(settings.aquariumLastReset, aquariumTs, sizeof(settings.aquariumLastReset) - 1);

  Serial.println("✓ Einstellungen geladen");
  // HINWEIS: pH-Kalibrierung wird separat aus ph_calibrations.json geladen
  Serial.printf("  Pumpe 1 (Probe): %d Schritte/ml, %d Hz, %d Beschl.\n",
                pumpCal.stepsPerML_Sample, pumpCal.speed_Sample, pumpCal.accel_Sample);
  Serial.printf("  Pumpe 2 (Säure): %d Schritte/ml, %d Hz, %d Beschl.\n",
                pumpCal.stepsPerML_Reagent, pumpCal.speed_Reagent, pumpCal.accel_Reagent);
  Serial.printf("  Pumpe 3 (Spülung): %d Schritte/ml, %d Hz, %d Beschl.\n",
                pumpCal.stepsPerML_Rinse, pumpCal.speed_Rinse, pumpCal.accel_Rinse);
}

void saveSettings() {
  Serial.println("[SAVE] Speichere Einstellungen...");

  DynamicJsonDocument doc(3072);  // Erhöht für Behälter-Füllstände
  doc["sampleVolume"] = settings.sampleVolume;
  doc["rinseVolume"] = settings.rinseVolume;
  doc["overpumpPercent"] = settings.overpumpPercent;
  doc["acidConcentration"] = settings.acidConcentration;
  doc["acidCorrectionFactor"] = settings.acidCorrectionFactor;
  doc["targetPH"] = settings.targetPH;

  // Dynamische Titrationsparameter speichern
  doc["doseVolumeMin"] = settings.doseVolumeMin;
  doc["doseVolumeMax"] = settings.doseVolumeMax;
  doc["stabilityTimeMin"] = settings.stabilityTimeMin;
  doc["stabilityTimeMax"] = settings.stabilityTimeMax;
  doc["toleranceMin"] = settings.toleranceMin;
  doc["toleranceMax"] = settings.toleranceMax;
  doc["interpolationExponent"] = settings.interpolationExponent;
  doc["acidPrimeVolume"] = settings.acidPrimeVolume;
  doc["submersionVolume"] = settings.submersionVolume;
  doc["phAcclimatizationTime"] = settings.phAcclimatizationTime;
  doc["maxStabilityTimeout"] = settings.maxStabilityTimeout;
  doc["maxContainerVolume"] = settings.maxContainerVolume;

  // Rührer-Geschwindigkeit speichern
  doc["stirrerSpeed"] = settings.stirrerSpeed;
  doc["stirrerPwmFreq"] = settings.stirrerPwmFreq;

  // Auto-Messung Einstellungen speichern (RTC-basiert)
  doc["autoMeasureEnabled"] = settings.autoMeasureEnabled;
  doc["firstMeasurementHour"] = settings.firstMeasurementHour;
  doc["firstMeasurementMinute"] = settings.firstMeasurementMinute;
  doc["measurementIntervalHours"] = settings.measurementIntervalHours;
  doc["measurementRepeatDays"] = settings.measurementRepeatDays;
  doc["lastMeasurementUnix"] = settings.lastMeasurementUnix;
  doc["measurementCompleted"] = settings.measurementCompleted;

  // Auto-Send Einstellungen speichern
  doc["autoSendToPumps"] = settings.autoSendToPumps;
  doc["pumpsIP"] = settings.pumpsIP;

  // Pumpen-Kalibrierung speichern
  doc["stepsPerML_Sample"] = pumpCal.stepsPerML_Sample;
  doc["stepsPerML_Reagent"] = pumpCal.stepsPerML_Reagent;
  doc["stepsPerML_Rinse"] = pumpCal.stepsPerML_Rinse;

  // Pumpen-Geschwindigkeiten und Beschleunigungen speichern
  doc["speed_Sample"] = pumpCal.speed_Sample;
  doc["speed_Reagent"] = pumpCal.speed_Reagent;
  doc["speed_Rinse"] = pumpCal.speed_Rinse;
  doc["accel_Sample"] = pumpCal.accel_Sample;
  doc["accel_Reagent"] = pumpCal.accel_Reagent;
  doc["accel_Rinse"] = pumpCal.accel_Rinse;

  // HINWEIS: pH-Kalibrierung wird jetzt in ph_calibrations.json gespeichert (nicht mehr hier)

  // Behälter-Füllstandsüberwachung speichern
  doc["acidContainerMax"] = settings.acidContainerMax;
  doc["acidContainerLevel"] = settings.acidContainerLevel;
  doc["wasteContainerMax"] = settings.wasteContainerMax;
  doc["wasteContainerLevel"] = settings.wasteContainerLevel;
  doc["aquariumTotalUsed"] = settings.aquariumTotalUsed;

  // Behälter-Timestamps speichern
  doc["acidLastRefill"] = settings.acidLastRefill;
  doc["wasteLastEmpty"] = settings.wasteLastEmpty;
  doc["aquariumLastReset"] = settings.aquariumLastReset;

  // DEBUG: Zeige was gespeichert wird
  String jsonString;
  serializeJson(doc, jsonString);
  Serial.println("[DEBUG] Speichere folgendes JSON:");
  Serial.println(jsonString);
  Serial.println("[DEBUG] Ende JSON");

  File file = LittleFS.open("/settings.json", "w");
  if (!file) {
    Serial.println("FEHLER: Kann Settings nicht speichern");
    return;
  }

  size_t bytesWritten = serializeJson(doc, file);
  file.flush();  // WICHTIG: Buffer leeren vor close()
  file.close();

  Serial.printf("✓ Einstellungen gespeichert (%d bytes)\n", bytesWritten);

  // DEBUG: Datei sofort wieder lesen zur Verifikation
  File verifyFile = LittleFS.open("/settings.json", "r");
  if (verifyFile) {
    String savedContent = verifyFile.readString();
    Serial.printf("[DEBUG] Verifikation: Datei existiert, Größe: %d bytes\n", verifyFile.size());
    Serial.println("[DEBUG] Gespeicherter Inhalt:");
    Serial.println(savedContent);
    verifyFile.close();
  } else {
    Serial.println("[DEBUG] FEHLER: Datei konnte nicht zur Verifikation gelesen werden!");
  }
}

// ==================== HTTP SENDEN AN DOSIERPUMPE ====================
bool sendMeasurementToPumps(JsonObject& measurement) {
  // Prüfe ob WiFi verbunden ist
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi nicht verbunden - kann nicht senden");
    return false;
  }

  // Validiere IP-Adresse (mindestens "x.x.x.x" = 7 Zeichen)
  if (strlen(settings.pumpsIP) < 7) {
    Serial.println("[HTTP] Ungültige Dosierpumpen-IP konfiguriert");
    return false;
  }

  // JSON erstellen - Dosierpumpe erwartet nur "kh" als Parameter
  // khValue ist Rohwert, wir senden den korrigierten Wert
  DynamicJsonDocument sendDoc(1024);
  float rawKH = measurement["khValue"] | 0.0f;
  float factor = measurement["correctionFactor"] | 1.0f;  // Fallback für alte Messungen
  float correctedKH = rawKH * factor;
  sendDoc["kh"] = correctedKH;  // WICHTIG: Korrigierten Wert senden!

  // Optional: Weitere Daten für Logging/Debugging
  sendDoc["timestamp"] = measurement["timestamp"];
  sendDoc["initialPH"] = measurement["initialPH"];
  sendDoc["finalPH"] = measurement["finalPH"];
  sendDoc["acidUsed"] = measurement["acidUsed"];
  sendDoc["sampleVolume"] = measurement["sampleVolume"];
  sendDoc["acidConcentration"] = measurement["acidConcentration"];
  sendDoc["khValueRaw"] = rawKH;  // Rohwert für Transparenz
  sendDoc["correctionFactor"] = factor;

  String json;
  serializeJson(sendDoc, json);

  String url = String("http://") + settings.pumpsIP + "/api/auto_kh_measurement";

  // Retry-Logik: Bis zu 3 Versuche
  const int MAX_RETRIES = 3;
  const int RETRY_DELAY_MS = 1000;

  for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    Serial.printf("[HTTP] Versuch %d/%d: Sende an %s\n", attempt, MAX_RETRIES, url.c_str());
    Serial.printf("[HTTP] JSON: %s\n", json.c_str());

    WiFiClient client;
    HTTPClient http;

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);  // 5 Sekunden Timeout

    int httpCode = http.POST(json);

    if (httpCode == 200) {
      // Response-Body prüfen
      String response = http.getString();
      http.end();

      // Response parsen und success-Flag prüfen
      DynamicJsonDocument responseDoc(256);
      DeserializationError error = deserializeJson(responseDoc, response);

      if (!error && responseDoc["success"] == true) {
        Serial.printf("[HTTP] ✓ Erfolgreich gesendet (Versuch %d)\n", attempt);
        return true;
      } else {
        Serial.printf("[HTTP] ⚠ Server antwortete 200, aber success != true: %s\n", response.c_str());
        // Trotzdem als Erfolg werten wenn 200 (Dosierpumpe hat es empfangen)
        return true;
      }
    } else if (httpCode > 0) {
      // HTTP-Fehler (4xx, 5xx)
      Serial.printf("[HTTP] Server-Fehler: %d\n", httpCode);
      http.end();
      // Bei Server-Fehlern nicht wiederholen (Problem liegt am Server)
      return false;
    } else {
      // Netzwerkfehler - Retry sinnvoll
      Serial.printf("[HTTP] Netzwerkfehler: %s\n", http.errorToString(httpCode).c_str());
      http.end();

      if (attempt < MAX_RETRIES) {
        Serial.printf("[HTTP] Warte %dms vor nächstem Versuch...\n", RETRY_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
      }
    }
  }

  Serial.println("[HTTP] ✗ Alle Versuche fehlgeschlagen");
  return false;
}

void sendUnsentMeasurements() {
  if (!settings.autoSendToPumps) {
    return;  // Auto-Send nicht aktiviert
  }

  Serial.println("[SYNC] Prüfe ungesendete Messungen...");

  // Lade alle Messungen
  if (!LittleFS.exists(MEASUREMENT_FILE)) {
    return;
  }

  File file = LittleFS.open(MEASUREMENT_FILE, "r");
  if (!file) return;

  PsramJsonDocument doc(20480);  // Nutzt PSRAM für große Dokumente
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("[SYNC] JSON Fehler beim Laden: %s\n", error.c_str());
    return;
  }

  if (!doc.containsKey("measurements")) {
    return;
  }

  JsonArray measurements = doc["measurements"];

  // Schritt 1: Zähle ungesendete Messungen und finde den Index der neuesten
  // Messungen sind chronologisch — letzte im Array ist die neueste.
  int latestUnsentIndex = -1;
  int unsentCount = 0;
  int idx = 0;
  for (JsonObject m : measurements) {
    bool sent = m["sentToPumps"] | false;
    if (!sent) {
      latestUnsentIndex = idx;
      unsentCount++;
    }
    idx++;
  }

  if (latestUnsentIndex < 0) {
    Serial.println("[SYNC] Keine ungesendeten Messungen gefunden");
    return;
  }

  Serial.printf("[SYNC] %d ungesendete Messung(en) gefunden, sende nur die neueste (Index %d)\n",
                unsentCount, latestUnsentIndex);

  // Schritt 2: In einem Durchlauf die neueste senden und ältere als gesendet markieren
  bool sendSuccess = false;
  bool anyUpdated = false;
  idx = 0;
  for (JsonObject m : measurements) {
    bool sent = m["sentToPumps"] | false;
    if (!sent) {
      if (idx == latestUnsentIndex) {
        // Dies ist die neueste Messung — senden
        Serial.printf("[SYNC] Sende Messung: %.2f dKH vom %s\n",
                      m["khValue"].as<float>(),
                      m["timestamp"].as<const char*>());
        sendSuccess = sendMeasurementToPumps(m);
        if (sendSuccess) {
          m["sentToPumps"] = true;
          anyUpdated = true;
          Serial.println("[SYNC] ✓ Neueste Messung erfolgreich gesendet");
        } else {
          // Bei Fehler: Messung bleibt ungesendet für nächsten Versuch
          Serial.println("[SYNC] ✗ Senden fehlgeschlagen — bleibt ungesendet für Retry");
        }
      } else {
        // Ältere ungesendete Messung — als gesendet markieren ohne zu senden
        m["sentToPumps"] = true;
        anyUpdated = true;
      }
    }
    idx++;
  }

  // Wenn Änderungen vorgenommen wurden, Datei atomar speichern
  if (anyUpdated) {
    const char* TEMP_FILE = "/measurements.tmp";
    size_t expectedSize = measureJson(doc);

    // In temp-Datei schreiben
    File tempFile = LittleFS.open(TEMP_FILE, "w");
    if (tempFile) {
      size_t bytesWritten = serializeJson(doc, tempFile);
      tempFile.flush();
      tempFile.close();

      // Validieren und umbenennen
      if (bytesWritten == expectedSize) {
        if (LittleFS.exists(MEASUREMENT_FILE)) {
          LittleFS.remove(MEASUREMENT_FILE);
        }
        if (LittleFS.rename(TEMP_FILE, MEASUREMENT_FILE)) {
          Serial.printf("[SYNC] ✓ Neueste Messung %s gesendet, %d Messung(en) als gesendet markiert\n",
                        sendSuccess ? "erfolgreich" : "fehlgeschlagen", unsentCount);
        } else {
          Serial.println("[SYNC] ✗ Fehler beim Umbenennen der temp-Datei");
        }
      } else {
        Serial.printf("[SYNC] ✗ Schreibfehler (geschrieben: %d, erwartet: %d)\n", bytesWritten, expectedSize);
        LittleFS.remove(TEMP_FILE);
      }
    } else {
      Serial.println("[SYNC] ✗ Kann temp-Datei nicht öffnen");
    }
  }
}

// ==================== MESSUNGEN SPEICHERN/LADEN ====================
void saveMeasurement() {
  // Lade existierende Messungen
  // Größe: ~150 bytes pro Messung × 100 max = 15000 bytes + Overhead
  PsramJsonDocument doc(20480);  // Nutzt PSRAM - 20KB für bis zu 100 Messungen

  if (LittleFS.exists(MEASUREMENT_FILE)) {
    File file = LittleFS.open(MEASUREMENT_FILE, "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (error) {
        Serial.printf("[SAVE] ✗ JSON Parse Fehler: %s (Datei möglicherweise korrupt)\n", error.c_str());
        // Weiter mit leerem Array - korrupte Datei wird überschrieben
      }
    }
  }

  // Array erstellen falls nicht vorhanden
  if (!doc.containsKey("measurements")) {
    doc.createNestedArray("measurements");
  }

  JsonArray measurements = doc["measurements"];

  // Neue Messung hinzufügen
  // khValue = Rohwert (ohne Korrekturfaktor)
  // correctionFactor = zum Messzeitpunkt verwendeter Faktor
  // Korrigierter Wert = khValue × correctionFactor
  JsonObject newMeasurement = measurements.createNestedObject();
  newMeasurement["timestamp"] = currentTitration.timestamp;
  newMeasurement["initialPH"] = currentTitration.initialPH;
  newMeasurement["finalPH"] = currentTitration.finalPH;
  newMeasurement["acidUsed"] = currentTitration.acidUsed;
  newMeasurement["khValue"] = currentTitration.khValue;  // Rohwert ohne Faktor
  newMeasurement["correctionFactor"] = settings.acidCorrectionFactor;  // Faktor zum Messzeitpunkt
  newMeasurement["sampleVolume"] = settings.sampleVolume;
  newMeasurement["acidConcentration"] = settings.acidConcentration;
  newMeasurement["sentToPumps"] = currentTitration.sentToPumps;
  newMeasurement["stabilityTimeout"] = currentTitration.stabilityTimeoutOccurred;

  float correctedKH = currentTitration.khValue * settings.acidCorrectionFactor;
  Serial.printf("[SAVE] Speichere Messung: %.2f dKH (Roh: %.2f × Faktor %.3f) vom %s\n",
                correctedKH, currentTitration.khValue, settings.acidCorrectionFactor,
                currentTitration.timestamp.c_str());

  // Alte Messungen löschen (max MAX_MEASUREMENTS)
  while (measurements.size() > MAX_MEASUREMENTS) {
    measurements.remove(0);
  }

  int measurementCount = measurements.size();
  Serial.printf("[SAVE] Gesamt %d Messungen im Array\n", measurementCount);

  // Erwartete Größe berechnen für Validierung
  size_t expectedSize = measureJson(doc);

  // ATOMARES SPEICHERN: Erst in temp-Datei schreiben, dann umbenennen
  const char* TEMP_FILE = "/measurements.tmp";

  // Schritt 1: In temporäre Datei schreiben
  File tempFile = LittleFS.open(TEMP_FILE, "w");
  if (!tempFile) {
    Serial.println("[SAVE] ✗ FEHLER: Kann temp-Datei nicht öffnen");
    return;
  }

  size_t bytesWritten = serializeJson(doc, tempFile);
  tempFile.flush();
  tempFile.close();

  // Schritt 2: Prüfen ob Schreiben erfolgreich war
  if (bytesWritten == 0 || bytesWritten != expectedSize) {
    Serial.printf("[SAVE] ✗ FEHLER: Schreiben fehlgeschlagen (geschrieben: %d, erwartet: %d)\n",
                  bytesWritten, expectedSize);
    LittleFS.remove(TEMP_FILE);  // Temp-Datei aufräumen
    return;
  }

  // Schritt 3: Temp-Datei verifizieren (lesen und parsen)
  File verifyFile = LittleFS.open(TEMP_FILE, "r");
  if (!verifyFile) {
    Serial.println("[SAVE] ✗ FEHLER: Kann temp-Datei nicht zur Verifikation öffnen");
    LittleFS.remove(TEMP_FILE);
    return;
  }

  // Größe prüfen
  size_t fileSize = verifyFile.size();
  verifyFile.close();

  if (fileSize != bytesWritten) {
    Serial.printf("[SAVE] ✗ FEHLER: Dateigröße stimmt nicht (Datei: %d, geschrieben: %d)\n",
                  fileSize, bytesWritten);
    LittleFS.remove(TEMP_FILE);
    return;
  }

  // Schritt 4: Alte Datei löschen und temp-Datei umbenennen
  if (LittleFS.exists(MEASUREMENT_FILE)) {
    LittleFS.remove(MEASUREMENT_FILE);
  }

  if (LittleFS.rename(TEMP_FILE, MEASUREMENT_FILE)) {
    Serial.printf("✓ Messung gespeichert (%d bytes, %d Messungen total)\n",
                  bytesWritten, measurementCount);
  } else {
    Serial.println("[SAVE] ✗ FEHLER: Umbenennen fehlgeschlagen");
    // Temp-Datei bleibt erhalten als Backup
  }
}

String getMeasurementsJSON() {
  if (!LittleFS.exists(MEASUREMENT_FILE)) {
    Serial.println("[LOAD] Keine Messungsdatei vorhanden");
    return "{\"measurements\":[]}";
  }

  File file = LittleFS.open(MEASUREMENT_FILE, "r");
  if (!file) {
    Serial.println("[LOAD] Fehler beim Öffnen der Datei");
    return "{\"measurements\":[]}";
  }

  String content = file.readString();
  file.close();

  // Parse um Anzahl zu loggen
  PsramJsonDocument doc(20480);  // Nutzt PSRAM - gleiche Größe wie beim Speichern
  DeserializationError error = deserializeJson(doc, content);
  if (!error && doc.containsKey("measurements")) {
    JsonArray measurements = doc["measurements"];
    Serial.printf("[LOAD] Lade %d Messungen aus Datei (%d bytes)\n",
                  measurements.size(), content.length());
  } else if (error) {
    Serial.printf("[LOAD] ✗ JSON Parse Fehler: %s\n", error.c_str());
  }

  return content;
}

// ==================== WEBSERVER ====================
void setupWebServer() {
  // Hauptseite - verwende send_P für PROGMEM String (speichereffizient!)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", MAIN_HTML);
  });
  
  // API: Status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    PsramJsonDocument doc(16384);  // 16KB PSRAM - Platz für Dosierprotokoll (~200 Steps)
    doc["state"] = (int)currentState;
    doc["stateDescription"] = stateDescription;
    doc["measurementCompleted"] = settings.measurementCompleted;
    doc["phValue"] = phValue;
    doc["temperature"] = temperature;
    doc["phStable"] = phStability.isStable;
    doc["wifiConfigured"] = wifiConfigured;
    doc["apMode"] = apMode;
    doc["ip"] = apMode ? "192.168.4.1" : WiFi.localIP().toString();
    doc["time"] = getFormattedTime();

    // Letzter gemessener KH-Wert (immer verfügbar wenn gültig)
    // lastKH = korrigierter Wert für Anzeige, lastKHRaw = Rohwert für Kalibrierung
    if (currentTitration.valid) {
      doc["lastKHRaw"] = currentTitration.khValue;  // Rohwert ohne Faktor
      doc["lastKH"] = currentTitration.khValue * settings.acidCorrectionFactor;  // Korrigierter Wert
      doc["correctionFactor"] = settings.acidCorrectionFactor;
    }

    // Chart-Daten für Live-Visualisierung
    if (currentState == MEASURING_INITIAL_PH || currentState == TITRATING) {
      doc["acidUsed"] = currentTitration.acidUsed;
    }

    if (currentState == TITRATING) {
      doc["initialPH"] = currentTitration.initialPH;
      doc["lastStabilityDuration"] = lastStabilityDuration;
      doc["stabilityReached"] = phStability.isStable;

      // Dynamische Titrationsparameter für Live-Anzeige
      TitrationParams params = calculateTitrationParams(phValue, settings.targetPH, currentTitration.initialPHDistance);
      doc["currentDoseVolume"] = params.doseVolume;
      doc["currentStabilityTime"] = params.stabilityTime;
      doc["currentTolerance"] = params.tolerance;
    }

    // Dosierprotokoll: nur Anzahl + letzter Step (Frontend sammelt selbst)
    if (currentTitration.stepCount > 0 && (currentState >= TITRATING && currentState <= COMPLETED)) {
      doc["stepCount"] = currentTitration.stepCount;
      int last = currentTitration.stepCount - 1;
      JsonObject lastStep = doc.createNestedObject("lastStep");
      lastStep["ph"] = currentTitration.steps[last].phBefore;
      lastStep["dose"] = currentTitration.steps[last].doseVolume;
      lastStep["stabT"] = currentTitration.steps[last].stabilityTime;
      lastStep["tol"] = currentTitration.steps[last].tolerance;
    }

    if (currentState == COMPLETED && currentTitration.valid) {
      doc["result"]["khValueRaw"] = currentTitration.khValue;  // Rohwert
      doc["result"]["khValue"] = currentTitration.khValue * settings.acidCorrectionFactor;  // Korrigiert
      doc["result"]["correctionFactor"] = settings.acidCorrectionFactor;
      doc["result"]["acidUsed"] = currentTitration.acidUsed;
      doc["result"]["initialPH"] = currentTitration.initialPH;
      doc["result"]["finalPH"] = currentTitration.finalPH;
      doc["result"]["stabilityTimeout"] = currentTitration.stabilityTimeoutOccurred;
    }

    // pH-Kalibrierungs-Status
    doc["phCalibrating"] = isPhCalibrating;
    if (isPhCalibrating) {
      unsigned long elapsedTime = millis() - phCalibrationStartTime;
      unsigned long totalTime = PH_CALIBRATION_INTERVAL;  // 30 Sekunden
      doc["phCalProgress"] = min(100, (int)((elapsedTime * 100) / totalTime));
      doc["phCalRemaining"] = max(0, (int)((totalTime - elapsedTime) / 1000));  // Sekunden
      doc["phCalValue"] = phCalibrationValue;  // 4.0 oder 7.0
      doc["phCalStable"] = isPhCalibrationStable;
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // API: Messung starten
  server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest *request){
    startMeasurement();
    request->send(200, "text/plain", "OK");
  });
  
  // API: Messung stoppen / Reset
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(256);
      deserializeJson(doc, (const char*)data, len);

      stopMeasurement();

      request->send(200, "text/plain", "OK");
    }
  );
  
  // API: Messung freigeben (nach Stromausfall/Abbruch)
  server.on("/api/clearance", HTTP_POST, [](AsyncWebServerRequest *request){
    settings.measurementCompleted = true;
    saveSettings();
    Serial.println("✓ Messungen manuell freigegeben");
    request->send(200, "application/json", "{\"success\":true}");
  });

  // API: Einstellungen abrufen
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(2048);  // Erhöht für alle Settings
    doc["sampleVolume"] = settings.sampleVolume;
    doc["rinseVolume"] = settings.rinseVolume;
    doc["overpumpPercent"] = settings.overpumpPercent;
    doc["acidConcentration"] = settings.acidConcentration;
    doc["acidCorrectionFactor"] = settings.acidCorrectionFactor;
    doc["targetPH"] = settings.targetPH;
    doc["doseVolumeMin"] = settings.doseVolumeMin;
    doc["doseVolumeMax"] = settings.doseVolumeMax;
    doc["stabilityTimeMin"] = settings.stabilityTimeMin;
    doc["stabilityTimeMax"] = settings.stabilityTimeMax;
    doc["toleranceMin"] = settings.toleranceMin;
    doc["toleranceMax"] = settings.toleranceMax;
    doc["interpolationExponent"] = settings.interpolationExponent;
    doc["acidPrimeVolume"] = settings.acidPrimeVolume;
    doc["submersionVolume"] = settings.submersionVolume;
    doc["phAcclimatizationTime"] = settings.phAcclimatizationTime;
    doc["maxStabilityTimeout"] = settings.maxStabilityTimeout;
    doc["maxContainerVolume"] = settings.maxContainerVolume;

    // Rührer-Geschwindigkeit + Frequenz
    doc["stirrerSpeed"] = settings.stirrerSpeed;
    doc["stirrerPwmFreq"] = settings.stirrerPwmFreq;

    // Auto-Messung Einstellungen (RTC-basiert)
    doc["autoMeasureEnabled"] = settings.autoMeasureEnabled;
    doc["firstMeasurementHour"] = settings.firstMeasurementHour;
    doc["firstMeasurementMinute"] = settings.firstMeasurementMinute;
    doc["measurementIntervalHours"] = settings.measurementIntervalHours;
    doc["measurementRepeatDays"] = settings.measurementRepeatDays;

    // Auto-Send Einstellungen
    doc["autoSendToPumps"] = settings.autoSendToPumps;
    doc["pumpsIP"] = settings.pumpsIP;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // API: Einstellungen speichern
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(2048);  // Erhöht für alle Settings
      deserializeJson(doc, (const char*)data, len);

      settings.sampleVolume = doc["sampleVolume"];
      settings.rinseVolume = doc["rinseVolume"];
      settings.overpumpPercent = doc["overpumpPercent"];
      settings.acidConcentration = doc["acidConcentration"];

      // Validierung: Säure-Korrekturfaktor zwischen 0.5 und 1.5
      float correctionFactor = doc["acidCorrectionFactor"] | 1.0;
      if (correctionFactor < 0.5) correctionFactor = 0.5;
      if (correctionFactor > 1.5) correctionFactor = 1.5;
      settings.acidCorrectionFactor = correctionFactor;

      settings.targetPH = doc["targetPH"];

      // Dynamische Titrationsparameter mit Validierung
      settings.doseVolumeMin = constrain(doc["doseVolumeMin"] | 0.01f, 0.001f, 1.0f);
      settings.doseVolumeMax = constrain(doc["doseVolumeMax"] | 0.5f, 0.01f, 2.0f);
      settings.stabilityTimeMin = constrain(doc["stabilityTimeMin"] | 5.0f, 1.0f, 60.0f);
      settings.stabilityTimeMax = constrain(doc["stabilityTimeMax"] | 30.0f, 2.0f, 120.0f);
      settings.toleranceMin = constrain(doc["toleranceMin"] | 0.05f, 0.01f, 1.0f);
      settings.toleranceMax = constrain(doc["toleranceMax"] | 0.3f, 0.05f, 2.0f);
      settings.interpolationExponent = constrain(doc["interpolationExponent"] | 2.5f, 1.0f, 5.0f);
      settings.acidPrimeVolume = constrain(doc["acidPrimeVolume"] | 0.2f, 0.0f, 1.0f);
      settings.submersionVolume = constrain(doc["submersionVolume"] | 5.0f, 0.0f, 20.0f);

      // Validierung: Akklimatisierungszeit zwischen 10s und 120s
      int acclimatizationTime = doc["phAcclimatizationTime"];
      if (acclimatizationTime < 10000) acclimatizationTime = 10000;    // Min 10 Sekunden
      if (acclimatizationTime > 120000) acclimatizationTime = 120000;  // Max 120 Sekunden
      settings.phAcclimatizationTime = acclimatizationTime;

      settings.maxStabilityTimeout = constrain((int)(doc["maxStabilityTimeout"] | 120), 10, 600);

      // Sicherheit: Maximales Behältervolumen
      if (doc.containsKey("maxContainerVolume")) {
        settings.maxContainerVolume = doc["maxContainerVolume"];
      }

      // Rührer-Geschwindigkeit (0-100%) + Frequenz (100-40000 Hz)
      if (doc.containsKey("stirrerSpeed")) {
        int speed = doc["stirrerSpeed"];
        if (speed < 0) speed = 0;
        if (speed > 100) speed = 100;
        settings.stirrerSpeed = speed;
      }
      if (doc.containsKey("stirrerPwmFreq")) {
        settings.stirrerPwmFreq = constrain((int)doc["stirrerPwmFreq"], 100, 40000);
      }

      // Auto-Messung Einstellungen (RTC-basiert)
      if (doc.containsKey("autoMeasureEnabled")) settings.autoMeasureEnabled = doc["autoMeasureEnabled"];
      if (doc.containsKey("firstMeasurementHour")) {
        int hour = doc["firstMeasurementHour"];
        settings.firstMeasurementHour = constrain(hour, 0, 23);
      }
      if (doc.containsKey("firstMeasurementMinute")) {
        int minute = doc["firstMeasurementMinute"];
        settings.firstMeasurementMinute = constrain(minute, 0, 59);
      }
      if (doc.containsKey("measurementIntervalHours")) {
        int interval = doc["measurementIntervalHours"];
        // Validierung: 0 (nur einmal) oder >= 1 Stunde
        if (interval != 0 && interval < 1) interval = 1;
        if (interval > 23) interval = 23;
        settings.measurementIntervalHours = interval;
      }
      if (doc.containsKey("measurementRepeatDays")) {
        int days = doc["measurementRepeatDays"];
        settings.measurementRepeatDays = constrain(days, 1, 30);
      }

      // Auto-Send Einstellungen
      if (doc.containsKey("autoSendToPumps")) {
        settings.autoSendToPumps = doc["autoSendToPumps"];
      }
      if (doc.containsKey("pumpsIP")) {
        const char* ip = doc["pumpsIP"];
        strncpy(settings.pumpsIP, ip, sizeof(settings.pumpsIP) - 1);
        settings.pumpsIP[sizeof(settings.pumpsIP) - 1] = '\0';
      }

      saveSettings();
      request->send(200, "text/plain", "OK");
    }
  );
  
  // API: WiFi konfigurieren
  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, (const char*)data, len);
      
      wifiSSID = doc["ssid"].as<String>();
      wifiPassword = doc["password"].as<String>();
      wifiConfigured = true;
      
      saveWiFiConfig();
      
      request->send(200, "text/plain", "OK - Neustart erforderlich");

      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
    }
  );
  
  // API: Pumpen-Kalibrierung abrufen
  server.on("/api/pumps", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    doc["stepsPerML_Sample"] = pumpCal.stepsPerML_Sample;
    doc["stepsPerML_Reagent"] = pumpCal.stepsPerML_Reagent;
    doc["stepsPerML_Rinse"] = pumpCal.stepsPerML_Rinse;
    doc["speed_Sample"] = pumpCal.speed_Sample;
    doc["speed_Reagent"] = pumpCal.speed_Reagent;
    doc["speed_Rinse"] = pumpCal.speed_Rinse;
    doc["accel_Sample"] = pumpCal.accel_Sample;
    doc["accel_Reagent"] = pumpCal.accel_Reagent;
    doc["accel_Rinse"] = pumpCal.accel_Rinse;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: Pumpen-Kalibrierung speichern
  server.on("/api/pumps", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, (const char*)data, len);

      pumpCal.stepsPerML_Sample = doc["stepsPerML_Sample"];
      pumpCal.stepsPerML_Reagent = doc["stepsPerML_Reagent"];
      pumpCal.stepsPerML_Rinse = doc["stepsPerML_Rinse"];
      pumpCal.speed_Sample = doc["speed_Sample"];
      pumpCal.speed_Reagent = doc["speed_Reagent"];
      pumpCal.speed_Rinse = doc["speed_Rinse"];
      pumpCal.accel_Sample = doc["accel_Sample"];
      pumpCal.accel_Reagent = doc["accel_Reagent"];
      pumpCal.accel_Rinse = doc["accel_Rinse"];

      saveSettings();
      request->send(200, "text/plain", "OK");
    }
  );

  // API: Messungen abrufen
  server.on("/api/measurements", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getMeasurementsJSON());
  });

  // API: Einzelne Messung löschen
  server.on("/api/measurements/delete", HTTP_POST, [](AsyncWebServerRequest *request){
    if (!request->hasParam("index")) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing index\"}");
      return;
    }

    int index = request->getParam("index")->value().toInt();

    // Datei lesen
    File file = LittleFS.open(MEASUREMENT_FILE, "r");
    if (!file) {
      request->send(404, "application/json", "{\"success\":false,\"error\":\"No measurements file\"}");
      return;
    }

    PsramJsonDocument doc(32768);  // Nutzt PSRAM für 32KB
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
      request->send(500, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
      return;
    }

    JsonArray measurements = doc["measurements"];
    if (index < 0 || index >= (int)measurements.size()) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid index\"}");
      return;
    }

    // Element entfernen
    measurements.remove(index);

    // Datei neu schreiben
    file = LittleFS.open(MEASUREMENT_FILE, "w");
    if (!file) {
      request->send(500, "application/json", "{\"success\":false,\"error\":\"Cannot write file\"}");
      return;
    }

    serializeJson(doc, file);
    file.close();

    Serial.printf("[API] Messung %d gelöscht, %d verbleibend\n", index, measurements.size());
    request->send(200, "application/json", "{\"success\":true}");
  });

  // ==================== BEHÄLTER-FÜLLSTANDSÜBERWACHUNG API ====================

  // API: Behälter-Füllstände abrufen (inkl. Prognosen)
  server.on("/api/containers", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1536);

    // Flache Struktur für einfacheren Frontend-Zugriff
    doc["acidContainerMax"] = settings.acidContainerMax;
    doc["acidContainerLevel"] = settings.acidContainerLevel;
    doc["wasteContainerMax"] = settings.wasteContainerMax;
    doc["wasteContainerLevel"] = settings.wasteContainerLevel;
    doc["aquariumTotalUsed"] = settings.aquariumTotalUsed;

    // Timestamps
    doc["acidLastRefill"] = settings.acidLastRefill;
    doc["wasteLastEmpty"] = settings.wasteLastEmpty;
    doc["aquariumLastReset"] = settings.aquariumLastReset;

    // Prognosen berechnen
    ContainerForecast forecast = calculateForecast();

    // Säure-Prognose
    doc["acidAvgPerMeasurement"] = forecast.avgAcidPerMeasurement;
    doc["acidMeasurementsLeft"] = forecast.acidMeasurementsLeft;
    doc["acidDaysLeft"] = forecast.acidDaysLeft;

    // Abwasser-Prognose
    doc["wastePerMeasurement"] = forecast.wastePerMeasurement;
    doc["wasteMeasurementsUntilFull"] = forecast.wasteMeasurementsUntilFull;
    doc["wasteDaysUntilFull"] = forecast.wasteDaysUntilFull;

    // Aquarium-Prognose
    doc["aquariumPerMeasurement"] = forecast.aquariumPerMeasurement;
    doc["aquariumPerDay"] = forecast.aquariumPerDay;
    doc["aquariumPerWeek"] = forecast.aquariumPerWeek;

    // Auto-Messung aktiv?
    doc["autoMeasureEnabled"] = (settings.autoMeasureEnabled == 1);

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: Säurebehälter aufgefüllt
  server.on("/api/containers/acid/refill", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);

      if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      float newLevel = doc["level"] | settings.acidContainerMax;

      // Validierung: nicht höher als Max
      if (newLevel > settings.acidContainerMax) {
        newLevel = settings.acidContainerMax;
      }
      if (newLevel < 0) newLevel = 0;

      refillAcidContainer(newLevel);
      saveSettings();

      DynamicJsonDocument response(128);
      response["success"] = true;
      response["level"] = settings.acidContainerLevel;
      String json;
      serializeJson(response, json);
      request->send(200, "application/json", json);
    }
  );

  // API: Abwasserbehälter entleert
  server.on("/api/containers/waste/empty", HTTP_POST, [](AsyncWebServerRequest *request){
    emptyWasteContainer();
    saveSettings();

    DynamicJsonDocument response(128);
    response["success"] = true;
    response["level"] = settings.wasteContainerLevel;
    String json;
    serializeJson(response, json);
    request->send(200, "application/json", json);
  });

  // API: Aquarium-Zähler zurücksetzen
  server.on("/api/containers/aquarium/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    resetAquariumCounter();
    saveSettings();

    DynamicJsonDocument response(128);
    response["success"] = true;
    response["totalUsed"] = settings.aquariumTotalUsed;
    String json;
    serializeJson(response, json);
    request->send(200, "application/json", json);
  });

  // API: Behälter-Einstellungen ändern (Maximalvolumen)
  server.on("/api/containers/settings", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);

      if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      // Maximalvolumen aktualisieren (wenn vorhanden)
      if (doc.containsKey("acidContainerMax")) {
        float newMax = doc["acidContainerMax"];
        if (newMax > 0) {
          settings.acidContainerMax = newMax;
          // Level anpassen wenn > neues Max
          if (settings.acidContainerLevel > newMax) {
            settings.acidContainerLevel = newMax;
          }
        }
      }

      if (doc.containsKey("wasteContainerMax")) {
        float newMax = doc["wasteContainerMax"];
        if (newMax > 0) {
          settings.wasteContainerMax = newMax;
          // Level anpassen wenn > neues Max
          if (settings.wasteContainerLevel > newMax) {
            settings.wasteContainerLevel = newMax;
          }
        }
      }

      saveSettings();

      DynamicJsonDocument response(256);
      response["success"] = true;
      response["acidContainerMax"] = settings.acidContainerMax;
      response["wasteContainerMax"] = settings.wasteContainerMax;
      String json;
      serializeJson(response, json);
      request->send(200, "application/json", json);
    }
  );

  // API: pH-Kalibrierungshistorie abrufen
  server.on("/api/ph/calibration-history", HTTP_GET, [](AsyncWebServerRequest *request){
    PsramJsonDocument doc(8192);  // Nutzt PSRAM

    doc["sensorInstallDate"] = phCalHistory.sensorInstallDate;
    doc["referenceSlope"] = phCalHistory.referenceSlope;
    doc["currentSlope"] = calculateCurrentSlope();
    doc["efficiency"] = calculatePHSensorEfficiency();

    // pH4 Kalibrierungen
    JsonArray arr4 = doc.createNestedArray("calibrations_pH4");
    for (int i = 0; i < phCalHistory.count_pH4; i++) {
      JsonObject entry = arr4.createNestedObject();
      entry["timestamp"] = phCalHistory.calibrations_pH4[i].timestamp;
      entry["mV"] = phCalHistory.calibrations_pH4[i].mV;
    }

    // pH7 Kalibrierungen
    JsonArray arr7 = doc.createNestedArray("calibrations_pH7");
    for (int i = 0; i < phCalHistory.count_pH7; i++) {
      JsonObject entry = arr7.createNestedObject();
      entry["timestamp"] = phCalHistory.calibrations_pH7[i].timestamp;
      entry["mV"] = phCalHistory.calibrations_pH7[i].mV;
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: pH-Kalibrierungseintrag löschen
  server.on("/api/ph/calibration-history/delete", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(256);
      deserializeJson(doc, (const char*)data, len);

      int phType = doc["phType"] | 0;
      int entryIndex = doc["index"] | -1;

      if ((phType == 4 || phType == 7) && entryIndex >= 0) {
        deletePHCalibrationEntry(phType, entryIndex);
        request->send(200, "text/plain", "OK");
      } else {
        request->send(400, "text/plain", "Invalid parameters");
      }
    }
  );

  // API: pH-Kalibrierungshistorie zurücksetzen (neue Sonde)
  server.on("/api/ph/calibration-history/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    resetPHCalibrationHistory();
    request->send(200, "text/plain", "OK");
  });

  // API: Einstellungen zurücksetzen
  server.on("/api/reset/settings", HTTP_POST, [](AsyncWebServerRequest *request){
    if (LittleFS.remove("/settings.json")) {
      Serial.println("[RESET] ✓ Einstellungen gelöscht");
      request->send(200, "text/plain", "Settings reset");
    } else {
      Serial.println("[RESET] ✗ Fehler beim Löschen der Einstellungen");
      request->send(500, "text/plain", "Error deleting settings");
    }
  });

  // API: Messungen löschen
  server.on("/api/reset/measurements", HTTP_POST, [](AsyncWebServerRequest *request){
    if (LittleFS.remove("/measurements.json")) {
      Serial.println("[RESET] ✓ Messungen gelöscht");
      request->send(200, "text/plain", "Measurements deleted");
    } else {
      Serial.println("[RESET] ✗ Fehler beim Löschen der Messungen");
      request->send(500, "text/plain", "Error deleting measurements");
    }
  });

  // API: WiFi-Scan starten (asynchron)
  server.on("/api/wifi/scan/start", HTTP_GET, [](AsyncWebServerRequest *request){
    WiFi.scanNetworks(true);  // true = async scan
    request->send(200, "application/json", "{\"status\":\"scanning\"}");
  });

  // API: WiFi-Scan Ergebnisse abrufen
  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request){
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "{\"status\":\"scanning\"}");
      return;
    }

    DynamicJsonDocument doc(2048);
    doc["status"] = "complete";
    JsonArray networks = doc.createNestedArray("networks");

    if (n > 0) {
      for (int i = 0; i < n; i++) {
        JsonObject net = networks.createNestedObject();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "secured";
      }
      WiFi.scanDelete();  // Ergebnisse freigeben
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // API: pH kalibrieren
  server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, (const char*)data, len);

      if (doc.containsKey("ph4")) {
        startPHCalibration(4.0);
        request->send(200, "text/plain", "pH 4.0 Kalibrierung gestartet - bitte 30 Sekunden warten");
      } else if (doc.containsKey("ph7")) {
        startPHCalibration(7.0);
        request->send(200, "text/plain", "pH 7.0 Kalibrierung gestartet - bitte 30 Sekunden warten");
      } else {
        request->send(400, "text/plain", "Ungültiger pH-Wert");
      }
    }
  );

  // API: pH Kalibrierungsdaten abrufen
  server.on("/api/calibrate/info", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(512);
    doc["ph4Calibrated"] = (phCal.voltage_pH4 > 0 && phCal.timestamp_pH4.length() > 0);
    doc["ph7Calibrated"] = (phCal.voltage_pH7 > 0 && phCal.timestamp_pH7.length() > 0);
    doc["ph4Voltage"] = phCal.voltage_pH4;
    doc["ph7Voltage"] = phCal.voltage_pH7;
    doc["ph4Timestamp"] = phCal.timestamp_pH4;
    doc["ph7Timestamp"] = phCal.timestamp_pH7;
    doc["isCalibrated"] = phCal.isCalibrated;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // API: Pumpen-Kalibrierung Status abrufen
  server.on("/api/pump/calibration", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(1024);
    doc["sample"] = pumpCal.stepsPerML_Sample;
    doc["reagent"] = pumpCal.stepsPerML_Reagent;
    doc["rinse"] = pumpCal.stepsPerML_Rinse;
    doc["isCalibrating"] = pumpCal.isCalibrating;
    doc["calibratingPump"] = pumpCal.calibratingPump;
    doc["calibrationSteps"] = pumpCal.calibrationSteps;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  // API: Pumpe manuell testen
  // WICHTIG: Nicht blockierend! pumpVolume() darf NICHT im async_tcp-Kontext laufen,
  // sonst blockiert es den Netzwerk-Task → Watchdog-Crash in ESP32 v3.x.
  // Stattdessen: Befehl in pendingPumpTest speichern, loop() führt ihn aus.
  server.on("/api/pump/test", HTTP_POST, [](AsyncWebServerRequest *request){
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if (currentState != IDLE && currentState != COMPLETED) {
      request->send(400, "text/plain", "Messung läuft - kann nicht testen");
      return;
    }
    if (pendingPumpTest.pending) {
      request->send(400, "text/plain", "Pumpentest läuft bereits");
      return;
    }

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (const char*)data, len);

    pendingPumpTest.pump = doc["pump"];
    pendingPumpTest.volume = doc["volume"];
    pendingPumpTest.reverse = doc["reverse"] | false;
    pendingPumpTest.pending = true;  // Signal an loop()

    Serial.printf("Manueller Pumpentest angefordert: Pumpe %d, %.2f ml, %s\n",
                  pendingPumpTest.pump, pendingPumpTest.volume,
                  pendingPumpTest.reverse ? "rückwärts" : "vorwärts");

    request->send(200, "text/plain", "OK");
  });

  // API: Rührer testen
  server.on("/api/stirrer/test", HTTP_POST, [](AsyncWebServerRequest *request){
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (const char*)data, len);

    if (doc.containsKey("freq")) {
      int freq = doc["freq"];
      stirrerUpdateFreq(freq);
    }
    int speed = doc["speed"] | 0;
    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;

    // Direkt PWM setzen ohne settings.stirrerSpeed zu überschreiben
    int dutyCycle = stirrerDutyCycle(speed);
    ledcWrite(STIRRER_PIN, dutyCycle);
    Serial.printf("Rührer-Test: %d%% → Duty %d/255 @ %d Hz\n", speed, dutyCycle, settings.stirrerPwmFreq);

    request->send(200, "text/plain", "OK");
  });

  // API: Rührer stoppen
  server.on("/api/stirrer/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("Rührer-Test beendet");
    ledcWrite(STIRRER_PIN, 0);  // Direkt stoppen ohne settings.stirrerSpeed zu ändern
    request->send(200, "text/plain", "OK");
  });

  // API: Pumpen-Kalibrierung starten
  // WICHTIG: Nicht blockierend! Gleiche Logik wie Pumpentest — an loop() delegieren.
  server.on("/api/pump/calibrate/start", HTTP_POST, [](AsyncWebServerRequest *request){
    if (currentState != IDLE) {
      request->send(400, "text/plain", "Messung läuft");
      return;
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if (pendingCalibration.pending) {
      request->send(400, "text/plain", "Kalibrierung läuft bereits");
      return;
    }

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (const char*)data, len);

    pumpCal.calibratingPump = doc["pump"];
    long steps = doc["steps"];

    Serial.printf("Kalibrierung angefordert: Pumpe %d, %ld Schritte\n",
                  pumpCal.calibratingPump, steps);

    pumpCal.isCalibrating = true;
    pumpCal.calibrationSteps = steps;

    pendingCalibration.pump = pumpCal.calibratingPump;
    pendingCalibration.steps = steps;
    pendingCalibration.pending = true;  // Signal an loop()

    request->send(200, "text/plain", "OK");
  });
  
  // API: Pumpen-Kalibrierung abschließen
  server.on("/api/pump/calibrate/finish", HTTP_POST, [](AsyncWebServerRequest *request){
    // Leerer erster Callback - Prüfung erfolgt im Body-Handler
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    // Prüfe isCalibrating HIER im Body-Handler
    if (!pumpCal.isCalibrating) {
      request->send(400, "text/plain", "Keine Kalibrierung aktiv");
      return;
    }

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, (const char*)data, len);

    long steps = doc["steps"];
    float actualVolume = doc["actualVolume"];

    // Neue Schritte/ml berechnen
    int newStepsPerML = round((float)steps / actualVolume);

    // Alte Kalibrierung für Vergleich
    int oldStepsPerML;
    switch(pumpCal.calibratingPump) {
      case 1: oldStepsPerML = pumpCal.stepsPerML_Sample; break;
      case 2: oldStepsPerML = pumpCal.stepsPerML_Reagent; break;
      case 3: oldStepsPerML = pumpCal.stepsPerML_Rinse; break;
      default: oldStepsPerML = 200; break;
    }

    Serial.printf("Kalibrierung Pumpe %d abgeschlossen:\n", pumpCal.calibratingPump);
    Serial.printf("  Schritte: %ld\n", steps);
    Serial.printf("  Gemessen: %.2f ml\n", actualVolume);
    Serial.printf("  Alt: %d Schritte/ml\n", oldStepsPerML);
    Serial.printf("  NEU: %d Schritte/ml\n", newStepsPerML);

    // Speichere neue Kalibrierung
    switch(pumpCal.calibratingPump) {
      case 1: pumpCal.stepsPerML_Sample = newStepsPerML; break;
      case 2: pumpCal.stepsPerML_Reagent = newStepsPerML; break;
      case 3: pumpCal.stepsPerML_Rinse = newStepsPerML; break;
    }

    pumpCal.isCalibrating = false;
    saveSettings();

    Serial.println("✓ Kalibrierung gespeichert\n");

    request->send(200, "text/plain", "OK");
  });

  // ==================== OTA UPDATE ENDPOINT ====================
  // Firmware Update via Web-Upload
  server.on("/api/ota/update", HTTP_POST,
    // Handler für Response nach Upload
    [](AsyncWebServerRequest *request){
      bool success = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
        success ? "{\"success\":true,\"message\":\"Update erfolgreich! Gerät startet neu...\"}"
                : "{\"success\":false,\"message\":\"Update fehlgeschlagen!\"}");
      response->addHeader("Connection", "close");
      request->send(response);
      if (success) {
        delay(1000);
        ESP.restart();
      }
    },
    // Handler für File Upload
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if (!index) {
        Serial.printf("\n[OTA] Update Start: %s\n", filename.c_str());
        // Berechne maximale Sketch-Größe
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      }
      // Schreibe Daten
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      } else {
        Serial.printf("[OTA] Progress: %d bytes\r", index + len);
      }
      // Finalisiere Update
      if (final) {
        if (Update.end(true)) {
          Serial.printf("\n[OTA] Update erfolgreich! Größe: %d bytes\n", index + len);
        } else {
          Update.printError(Serial);
        }
      }
    }
  );

  // Firmware-Info Endpoint
  server.on("/api/ota/info", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(256);
    doc["version"] = "1.0.0";  // Hier kannst du deine Version pflegen
    doc["freeSketchSpace"] = ESP.getFreeSketchSpace();
    doc["sketchSize"] = ESP.getSketchSize();
    doc["flashSize"] = ESP.getFlashChipSize();
    doc["psramSize"] = ESP.getPsramSize();
    doc["freeHeap"] = ESP.getFreeHeap();
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
}

// ==================== HTML INTERFACE (ausgelagert in webpage.h) ====================
#include "webpage.h"
