// ==================== HTML INTERFACE ====================
// WICHTIG: const char* statt String verwenden um Speicher zu sparen!
// Der raw literal String wird direkt aus dem Flash gelesen.
const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>KH-Titrationssystem</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 15px;
            padding: 30px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        h1 {
            color: #2c3e50;
            margin-bottom: 10px;
            text-align: center;
        }
        .header-info {
            text-align: center;
            color: #7f8c8d;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .tabs {
            display: flex;
            border-bottom: 2px solid #ecf0f1;
            margin-bottom: 20px;
        }
        .tab {
            padding: 15px 25px;
            cursor: pointer;
            background: #f8f9fa;
            border: none;
            margin-right: 5px;
            border-radius: 10px 10px 0 0;
            font-size: 16px;
            transition: all 0.3s;
        }
        .tab.active {
            background: #3498db;
            color: white;
        }
        .tab-content {
            display: none;
            padding: 20px;
        }
        .tab-content.active {
            display: block;
        }
        .status-card {
            background: linear-gradient(45deg, #27ae60, #2ecc71);
            color: white;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            text-align: center;
        }
        .status-card.error {
            background: linear-gradient(45deg, #e74c3c, #c0392b);
        }
        .status-card.working {
            background: linear-gradient(45deg, #f39c12, #e67e22);
        }
        .ph-display {
            font-size: 48px;
            font-weight: bold;
            margin: 10px 0;
        }
        .control-panel {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            margin-bottom: 20px;
        }
        .btn {
            padding: 15px 30px;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            cursor: pointer;
            transition: all 0.3s;
            font-weight: bold;
        }
        .btn-primary {
            background: #3498db;
            color: white;
        }
        .btn-primary:hover {
            background: #2980b9;
        }
        .btn-danger {
            background: #e74c3c;
            color: white;
        }
        .btn-danger:hover {
            background: #c0392b;
        }
        .btn:disabled {
            background: #95a5a6;
            cursor: not-allowed;
        }
        .spinner {
            border: 3px solid #f3f3f3;
            border-top: 3px solid #3498db;
            border-radius: 50%;
            width: 24px;
            height: 24px;
            animation: spin 1s linear infinite;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
        .settings-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
        }
        .setting-group {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 8px;
        }
        .setting-group label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
            color: #2c3e50;
        }
        .setting-group input, .setting-group select {
            width: 100%;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 5px;
            font-size: 14px;
        }
        .chart-card {
            background: #ecf0f1;
            padding: 20px;
            border-radius: 10px;
            margin-top: 20px;
        }
        .chart-card canvas {
            width: 100% !important;
            height: auto !important;
            max-height: 400px;
            background: white;
            border-radius: 5px;
        }
        .result-card {
            background: #ecf0f1;
            padding: 20px;
            border-radius: 10px;
            margin-top: 20px;
        }
        .result-item {
            display: flex;
            justify-content: space-between;
            padding: 10px 0;
            border-bottom: 1px solid #bdc3c7;
        }
        .result-item:last-child {
            border-bottom: none;
        }
        .result-value {
            font-weight: bold;
            color: #2c3e50;
        }
        .measurements-table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 20px;
        }
        .measurements-table th,
        .measurements-table td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ecf0f1;
        }
        .measurements-table th {
            background: #3498db;
            color: white;
            font-weight: bold;
        }
        .measurements-table tr:hover {
            background: #f8f9fa;
        }
        .wifi-list {
            list-style: none;
            margin-top: 10px;
        }
        .wifi-item {
            padding: 10px;
            background: #f8f9fa;
            margin-bottom: 5px;
            border-radius: 5px;
            cursor: pointer;
            display: flex;
            justify-content: space-between;
        }
        .wifi-item:hover {
            background: #e8e9ea;
        }
        .wifi-item.selected {
            background: #3498db;
            color: white;
        }
        @media (max-width: 768px) {
            .control-panel {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🧪 KH-Titrationssystem</h1>
        <div class="header-info" id="headerInfo">
            Verbunden mit ESP32 | IP: ... | Zeit: ...
        </div>

        <div class="tabs">
            <button class="tab active" onclick="showTab('dashboard')">Dashboard</button>
            <button class="tab" onclick="showTab('settings')">Einstellungen</button>
            <button class="tab" onclick="showTab('measurements')">Messungen</button>
            <button class="tab" onclick="showTab('pumps')">Pumpen</button>
            <button class="tab" onclick="showTab('wifi')">WiFi</button>
            <button class="tab" onclick="showTab('calibration')">pH-Kalibrierung</button>
            <button class="tab" onclick="showTab('containers')">Behälter</button>
        </div>

        <!-- Dashboard Tab -->
        <div id="dashboard" class="tab-content active">
            <div class="status-card" id="statusCard">
                <div id="stateText">Bereit</div>
                <div class="ph-display" id="phDisplay">KH --</div>
                <div id="additionalInfo">Temperatur: 25°C</div>
            </div>

            <div id="clearanceBanner" style="display:none; background:linear-gradient(45deg, #e74c3c, #c0392b); color:white; padding:15px; border-radius:10px; margin-top:15px; text-align:center;">
                <div style="font-size:1.2em; margin-bottom:8px;">⚠️ Letzte Messung wurde unterbrochen!</div>
                <div style="margin-bottom:12px;">Bitte Behälter prüfen und ggf. leeren.</div>
                <button onclick="releaseMeasurement()" style="background:white; color:#e74c3c; border:none; padding:10px 24px; border-radius:6px; font-weight:bold; cursor:pointer; font-size:1em;">
                    ✓ Freigeben
                </button>
            </div>

            <div class="control-panel">
                <button class="btn btn-primary" id="btnStart" onclick="startMeasurement()">
                    ▶️ Messung starten
                </button>
                <button class="btn btn-danger" id="btnStop" onclick="stopMeasurement()">
                    🛑 NOTFALL-STOP
                </button>
            </div>
            <div style="text-align: center; margin-top: 10px; font-size: 12px; color: #888;">
                ⚠️ NOTFALL-STOP bricht sofort ab - Behälter manuell leeren!
            </div>

            <!-- Vorratsbehälter-Schnellübersicht -->
            <div class="result-card" style="margin-top: 20px;">
                <h3>📦 Vorratsbehälter <span style="font-size: 0.7em; font-weight: normal; color: #888;">(Details im Tab "Behälter")</span></h3>
                <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px;">
                    <!-- Säure -->
                    <div style="text-align: center; padding: 8px; background: #f8f9fa; border-radius: 8px;">
                        <div style="font-size: 0.85em; color: #666;">🧴 Säure</div>
                        <div id="dashAcidBar" style="height: 8px; background: #e9ecef; border-radius: 4px; margin: 5px 0; overflow: hidden;">
                            <div id="dashAcidLevel" style="height: 100%; width: 0%; background: #2ecc71;"></div>
                        </div>
                        <div style="font-weight: bold; font-size: 0.9em;" id="dashAcidPercent">0%</div>
                    </div>
                    <!-- Abwasser -->
                    <div style="text-align: center; padding: 8px; background: #f8f9fa; border-radius: 8px;">
                        <div style="font-size: 0.85em; color: #666;">🗑️ Abwasser</div>
                        <div id="dashWasteBar" style="height: 8px; background: #e9ecef; border-radius: 4px; margin: 5px 0; overflow: hidden;">
                            <div id="dashWasteLevel" style="height: 100%; width: 0%; background: #2ecc71;"></div>
                        </div>
                        <div style="font-weight: bold; font-size: 0.9em;" id="dashWastePercent">0%</div>
                    </div>
                    <!-- Aquarium -->
                    <div style="text-align: center; padding: 8px; background: #f8f9fa; border-radius: 8px;">
                        <div style="font-size: 0.85em; color: #666;">🐠 Aquarium</div>
                        <div style="height: 8px; background: #3498db; border-radius: 4px; margin: 5px 0;"></div>
                        <div style="font-weight: bold; font-size: 0.9em;" id="dashAquariumUsed">0 ml</div>
                    </div>
                </div>
            </div>

            <!-- Live Titration Chart -->
            <div id="chartCard" class="chart-card" style="display: none;">
                <h3>📈 Titrationsverlauf</h3>
                <canvas id="titrationChart" width="800" height="400"></canvas>
                <div id="liveKH" style="margin-top: 15px; padding: 10px; background: linear-gradient(45deg, #3498db, #2980b9); color: white; border-radius: 8px; text-align: center; font-size: 18px; font-weight: bold; display: none;">
                    Aktueller KH-Wert: <span id="liveKHValue">0.00</span> dKH
                </div>

                <!-- Dosierprotokoll-Graph: Einzelwerte pro Schritt -->
                <div id="stepChartContainer" style="display: none; margin-top: 20px;">
                    <h3>📊 Dosierprotokoll</h3>
                    <canvas id="stepChart" width="800" height="300"></canvas>
                    <div style="text-align: center; font-size: 0.85em; color: #666; margin-top: 5px;">
                        <span style="color: #e74c3c;">● Dosis (ml)</span> &nbsp;&nbsp;
                        <span style="color: #2ecc71;">● Auswertung (s)</span>
                    </div>
                </div>
            </div>

            <div id="resultCard" class="result-card" style="display: none;">
                <h3>📊 Messergebnis</h3>
                <div class="result-item">
                    <span>KH-Wert:</span>
                    <span class="result-value" id="resultKH">-</span>
                </div>
                <div class="result-item">
                    <span>Säureverbrauch:</span>
                    <span class="result-value" id="resultAcid">-</span>
                </div>
                <div class="result-item">
                    <span>Initial pH:</span>
                    <span class="result-value" id="resultInitialPH">-</span>
                </div>
                <div class="result-item">
                    <span>Final pH:</span>
                    <span class="result-value" id="resultFinalPH">-</span>
                </div>
            </div>
        </div>

        <!-- Settings Tab -->
        <div id="settings" class="tab-content">
            <h3>⚙️ Messparameter</h3>
            <div class="settings-grid">
                <div class="setting-group">
                    <label>Probenvolumen (ml)</label>
                    <input type="number" id="sampleVolume" step="0.1" value="50">
                </div>
                <div class="setting-group">
                    <label>Spülwasser (ml)</label>
                    <input type="number" id="rinseVolume" step="0.1" value="10">
                </div>
                <div class="setting-group">
                    <label>Überpumpen (%)</label>
                    <input type="number" id="overpumpPercent" step="1" value="10">
                </div>
                <div class="setting-group">
                    <label>Säurekonzentration (mol/l)</label>
                    <input type="number" id="acidConcentration" step="0.01" value="0.1">
                </div>
                <div class="setting-group">
                    <label>Säure-Korrekturfaktor</label>
                    <div style="display: flex; gap: 5px; align-items: center;">
                        <input type="number" id="acidCorrectionFactor" step="0.001" value="1.0" min="0.5" max="1.5" style="flex: 1;">
                        <button type="button" onclick="resetAcidCorrectionFactor()" style="padding: 5px 10px; font-size: 12px;">Reset</button>
                    </div>
                    <div style="font-size: 12px; color: #666; margin-top: 5px;">
                        Effektiv: <span id="effectiveAcidConcentration">0.100</span> mol/l
                    </div>
                </div>
                <div class="setting-group" style="background: #f5f5f5; padding: 10px; border-radius: 5px; border: 1px solid #ddd;">
                    <label style="font-weight: bold;">🧮 Korrekturfaktor berechnen</label>
                    <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 10px;">
                        <div>
                            <span style="font-size: 12px; color: #666;">Letzte Messung:</span>
                            <div id="lastMeasuredKH" style="font-weight: bold; font-size: 16px;">-- dKH</div>
                        </div>
                        <div>
                            <span style="font-size: 12px; color: #666;">Soll-Wert (dKH):</span>
                            <input type="number" id="targetKHValue" step="0.1" min="0.5" max="20" value="7.0" style="width: 100%;">
                        </div>
                    </div>
                    <div style="margin-top: 10px; text-align: center;">
                        <span style="font-size: 12px; color: #666;">Berechneter Faktor: </span>
                        <span id="calculatedCorrectionFactor" style="font-weight: bold;">1.000</span>
                    </div>
                    <button type="button" onclick="applyCalculatedCorrectionFactor()" style="width: 100%; margin-top: 10px; padding: 8px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer;">
                        ✓ Berechneten Faktor übernehmen
                    </button>
                </div>
                <div class="setting-group">
                    <label>Ziel-pH</label>
                    <input type="number" id="targetPH" step="0.1" value="4.5">
                </div>
                <div class="setting-group">
                    <label>Akklimatisierungszeit (s)</label>
                    <input type="number" id="phAcclimatizationTime" step="5" value="30" min="10" max="120">
                    <small style="color: #666; font-size: 0.9em;">Wartezeit nach Probenentnahme für pH-Stabilisierung und Mischung (10-120s)</small>
                </div>
                <div class="setting-group">
                    <label>Max. Wartezeit Stabilität (s)</label>
                    <input type="number" id="maxStabilityTimeout" step="10" value="120" min="10" max="600">
                    <small style="color: #666; font-size: 0.9em;">Max. Wartezeit pro Schritt bis pH stabil (10-600s). Bei Timeout wird trotzdem weiter dosiert.</small>
                </div>
            </div>

            <h3 style="margin-top: 30px;">Dynamische Titration</h3>
            <div class="settings-grid">
                <div class="setting-group">
                    <label>Dosis-Volumen (ml)</label>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <span>Min:</span>
                        <input type="number" id="doseVolumeMin" step="0.001" value="0.01" min="0.001" max="1.0" style="flex:1;">
                        <span>Max:</span>
                        <input type="number" id="doseVolumeMax" step="0.01" value="0.5" min="0.01" max="2.0" style="flex:1;">
                    </div>
                    <small style="color: #666; font-size: 0.9em;">Min = nahe Ziel-pH (fein), Max = weit entfernt (grob)</small>
                </div>
                <div class="setting-group">
                    <label>Auswertungszeitraum (s)</label>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <span>Min:</span>
                        <input type="number" id="stabilityTimeMin" step="1" value="5" min="1" max="60" style="flex:1;">
                        <span>Max:</span>
                        <input type="number" id="stabilityTimeMax" step="1" value="30" min="2" max="120" style="flex:1;">
                    </div>
                    <small style="color: #666; font-size: 0.9em;">Min = weit entfernt (kurz), Max = nahe Ziel (lang, mehr Datenpunkte)</small>
                </div>
                <div class="setting-group">
                    <label>Toleranz (pH)</label>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <span>Min:</span>
                        <input type="number" id="toleranceMin" step="0.01" value="0.05" min="0.01" max="1.0" style="flex:1;">
                        <span>Max:</span>
                        <input type="number" id="toleranceMax" step="0.01" value="0.3" min="0.05" max="2.0" style="flex:1;">
                    </div>
                    <small style="color: #666; font-size: 0.9em;">Min = nahe Ziel (streng), Max = weit entfernt (locker)</small>
                </div>
                <div class="setting-group">
                    <label>Interpolations-Exponent</label>
                    <input type="number" id="interpolationExponent" step="0.1" value="2.5" min="1.0" max="5.0">
                    <small style="color: #666; font-size: 0.9em;">1.0 = linear, 2-3 = exponentiell (empfohlen). Steuert wie aggressiv Parameter sich nahe dem Ziel ändern.</small>
                </div>
                <div class="setting-group">
                    <label>Säure-Prime-Volumen (ml)</label>
                    <input type="number" id="acidPrimeVolume" step="0.01" value="0.2" min="0" max="1.0">
                    <small style="color: #666; font-size: 0.9em;">Festes Volumen zum Primen der Säurepumpe beim Spülen (0 = deaktiviert)</small>
                </div>
                <div class="setting-group">
                    <label>Untertauch-Volumen (ml)</label>
                    <input type="number" id="submersionVolume" step="0.5" value="5.0" min="0" max="20">
                    <small style="color: #666; font-size: 0.9em;">Zusatzwasser vor Säure-Spülung damit Spritze untertaucht (0 = deaktiviert)</small>
                </div>
                <div class="setting-group">
                    <label>Rührer-Geschwindigkeit: <span id="stirrerSpeedValue">80</span>%</label>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <input type="range" id="stirrerSpeed" min="0" max="100" value="80"
                               oninput="document.getElementById('stirrerSpeedValue').textContent = this.value"
                               style="flex: 1; margin-top: 5px;">
                        <button class="btn btn-primary"
                                onmousedown="testStirrer(true)"
                                onmouseup="testStirrer(false)"
                                onmouseleave="testStirrer(false)"
                                ontouchstart="testStirrer(true)"
                                ontouchend="testStirrer(false)"
                                style="padding: 8px 15px; font-size: 14px; white-space: nowrap;">
                            🔄 Test
                        </button>
                    </div>
                    <small style="color: #666; font-size: 0.9em;">PWM Duty Cycle (0% = AUS, 100% = Maximale Geschwindigkeit). Halte "Test" gedrückt zum Testen.</small>
                </div>
                <div class="setting-group">
                    <label>Rührer-Frequenz: <span id="stirrerFreqValue">25000</span> Hz</label>
                    <div style="display: flex; gap: 10px; align-items: center;">
                        <input type="range" id="stirrerPwmFreq" min="100" max="40000" step="100" value="25000"
                               oninput="document.getElementById('stirrerFreqValue').textContent = this.value"
                               style="flex: 1; margin-top: 5px;">
                    </div>
                    <small style="color: #666; font-size: 0.9em;">100-20000 Hz = hörbar (Fiepen möglich), >20000 Hz = unhörbar. Test-Button oben zum Ausprobieren.</small>
                </div>
            </div>

            <h3 style="margin-top: 30px;">🛡️ Sicherheit</h3>
            <div class="settings-grid">
                <div class="setting-group">
                    <label>Maximales Behältervolumen (ml)</label>
                    <input type="number" id="maxContainerVolume" value="100" min="10" max="500" step="5">
                    <small style="color: #666; font-size: 0.9em;">Überlaufschutz: Messung stoppt bei Erreichen dieses Volumens</small>
                </div>
            </div>

            <h3 style="margin-top: 30px;">🤖 Automatische Messung (RTC-basiert)</h3>
            <div class="settings-grid">
                <div class="setting-group">
                    <label>Automatische Messung</label>
                    <select id="autoMeasureEnabled">
                        <option value="0">Deaktiviert</option>
                        <option value="1">Aktiviert</option>
                    </select>
                    <small style="color: #666; font-size: 0.9em;">Aktiviert zeitgesteuerte automatische Messungen</small>
                </div>

                <div class="setting-group">
                    <label>Erste Messung (Uhrzeit)</label>
                    <div style="display: flex; gap: 10px;">
                        <input type="number" id="firstMeasurementHour" min="0" max="23" value="6" style="width: 80px;">
                        <span style="align-self: center;">:</span>
                        <input type="number" id="firstMeasurementMinute" min="0" max="59" value="0" style="width: 80px;">
                    </div>
                    <small style="color: #666; font-size: 0.9em;">Uhrzeit der ersten Messung (z.B. 06:00)</small>
                </div>

                <div class="setting-group">
                    <label>Messungen alle X Stunden</label>
                    <input type="number" id="measurementIntervalHours" min="0" max="23" value="0" step="1">
                    <small style="color: #666; font-size: 0.9em;"><strong>0</strong> = nur einmal pro Tag | <strong>1-23</strong> = Intervall in ganzen Stunden</small>
                </div>

                <div class="setting-group">
                    <label>Messungen alle X Tage wiederholen</label>
                    <input type="number" id="measurementRepeatDays" min="1" max="30" value="1">
                    <small style="color: #666; font-size: 0.9em;">1 = täglich, 2 = jeden 2. Tag, 7 = wöchentlich</small>
                </div>
            </div>

            <!-- Mess-Plan Anzeige -->
            <div id="measurementPlan" style="margin-top: 20px; padding: 15px; background: #f0f0f0; border-radius: 8px; display: none;">
                <h4 style="margin-top: 0;">📅 Mess-Plan:</h4>
                <div id="measurementPlanContent"></div>
                <div id="nextMeasurement" style="margin-top: 10px; font-weight: bold;"></div>
            </div>

            <h3 style="margin-top: 30px;">📤 Automatischer Versand an Dosierpumpe</h3>
            <div class="settings-grid">
                <div class="setting-group">
                    <label>Automatisch senden</label>
                    <select id="autoSendToPumps">
                        <option value="false">Nein</option>
                        <option value="true">Ja</option>
                    </select>
                </div>
                <div class="setting-group">
                    <label>Dosierpumpen IP-Adresse</label>
                    <input type="text" id="pumpsIP" value="192.168.1.100" placeholder="192.168.1.100">
                </div>
            </div>

            <button class="btn btn-primary" onclick="saveSettingsWithAlert()" style="margin-top: 20px; width: 100%;">
                💾 Einstellungen speichern
            </button>

            <h3 style="margin-top: 30px; color: #e74c3c;">⚠️ Zurücksetzen</h3>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 15px;">
                <button class="btn btn-danger" onclick="resetSettings()" style="width: 100%;">
                    🔄 Einstellungen zurücksetzen
                </button>
                <button class="btn btn-danger" onclick="resetMeasurements()" style="width: 100%;">
                    🗑️ Messungen löschen
                </button>
            </div>
            <small style="color: #666; display: block; margin-top: 10px;">
                ⚠️ Diese Aktionen können nicht rückgängig gemacht werden!
            </small>

            <div style="background: #e3f2fd; border-left: 4px solid #2196f3; padding: 15px; margin-top: 20px; border-radius: 5px;">
                <strong>ℹ️ Hinweis:</strong> Die Pumpen-Kalibrierung (Schritte/ml) findest du im Tab "Pumpen".
            </div>

            <h3 style="margin-top: 30px;">🔄 Firmware Update (OTA)</h3>
            <div class="result-card">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div>
                        <span style="color: #7f8c8d; font-size: 0.9em;">Aktuelle Version:</span>
                        <div id="firmwareVersion" style="font-weight: bold; font-size: 1.2em;">--</div>
                    </div>
                    <div>
                        <span style="color: #7f8c8d; font-size: 0.9em;">Freier Speicher:</span>
                        <div id="freeSketchSpace" style="font-weight: bold; font-size: 1.2em;">--</div>
                    </div>
                </div>
                <div style="margin-bottom: 15px;">
                    <label style="display: block; margin-bottom: 5px;">Firmware-Datei (.bin) auswählen:</label>
                    <input type="file" id="firmwareFile" accept=".bin" style="width: 100%; padding: 10px; border: 2px dashed #ddd; border-radius: 5px;">
                </div>
                <button class="btn btn-primary" onclick="uploadFirmware()" style="width: 100%;">
                    📤 Firmware hochladen & installieren
                </button>
                <div id="otaProgress" style="display: none; margin-top: 15px;">
                    <div style="background: #ecf0f1; border-radius: 5px; overflow: hidden;">
                        <div id="otaProgressBar" style="height: 20px; background: #3498db; width: 0%; transition: width 0.3s;"></div>
                    </div>
                    <div id="otaStatus" style="text-align: center; margin-top: 5px; color: #7f8c8d;"></div>
                </div>
                <div style="margin-top: 15px; padding: 10px; background: #fff3cd; border-radius: 5px; font-size: 0.9em;">
                    <strong>⚠️ Hinweis:</strong> Nach dem Update startet das Gerät automatisch neu. Nicht während einer Messung updaten!
                </div>
            </div>
        </div>

        <!-- Measurements Tab -->
        <div id="measurements" class="tab-content">
            <h3>📈 Messverlauf</h3>
            <table class="measurements-table">
                <thead>
                    <tr>
                        <th>Zeitstempel</th>
                        <th>KH (dKH)</th>
                        <th>Säure (ml)</th>
                        <th>Initial pH</th>
                        <th>Final pH</th>
                        <th style="width: 40px;"></th>
                    </tr>
                </thead>
                <tbody id="measurementsTableBody">
                    <tr>
                        <td colspan="6" style="text-align: center;">Keine Messungen vorhanden</td>
                    </tr>
                </tbody>
            </table>
        </div>

        <!-- Pumps Tab -->
        <div id="pumps" class="tab-content">
            <h3>💧 Pumpen-Kalibrierung & Test</h3>
            
            <!-- Aktuelle Kalibrierung -->
            <div class="result-card">
                <h4>📊 Aktuelle Kalibrierung & Einstellungen</h4>
                <p style="margin-bottom: 15px; color: #7f8c8d; font-size: 0.9em;">
                    Schritte/ml werden durch die automatische Kalibrierung ermittelt (siehe unten).
                    Geschwindigkeit und Beschleunigung können hier angepasst werden.
                </p>

                <!-- Pumpe 1 -->
                <h5 style="margin-top: 15px; margin-bottom: 10px;">Pumpe 1 (Probenwasser)</h5>
                <div class="settings-grid">
                    <div class="setting-group">
                        <label>Schritte/ml (kalibriert)</label>
                        <div class="result-item">
                            <span class="result-value" id="stepsPerML_Sample_display" style="font-size: 1.2em; font-weight: bold; color: #2c3e50;">200</span>
                        </div>
                    </div>
                    <div class="setting-group">
                        <label>Geschwindigkeit (Hz)</label>
                        <input type="number" id="speed_Sample" value="2000" min="100" max="10000" step="100">
                    </div>
                    <div class="setting-group">
                        <label>Beschleunigung</label>
                        <input type="number" id="accel_Sample" value="1000" min="100" max="10000" step="100">
                    </div>
                </div>

                <!-- Pumpe 2 -->
                <h5 style="margin-top: 15px; margin-bottom: 10px;">Pumpe 2 (Säure/Reagenz)</h5>
                <div class="settings-grid">
                    <div class="setting-group">
                        <label>Schritte/ml (kalibriert)</label>
                        <div class="result-item">
                            <span class="result-value" id="stepsPerML_Reagent_display" style="font-size: 1.2em; font-weight: bold; color: #2c3e50;">200</span>
                        </div>
                    </div>
                    <div class="setting-group">
                        <label>Geschwindigkeit (Hz)</label>
                        <input type="number" id="speed_Reagent" value="1000" min="100" max="10000" step="100">
                    </div>
                    <div class="setting-group">
                        <label>Beschleunigung</label>
                        <input type="number" id="accel_Reagent" value="500" min="100" max="10000" step="100">
                    </div>
                </div>

                <!-- Pumpe 3 -->
                <h5 style="margin-top: 15px; margin-bottom: 10px;">Pumpe 3 (Spülung)</h5>
                <div class="settings-grid">
                    <div class="setting-group">
                        <label>Schritte/ml (kalibriert)</label>
                        <div class="result-item">
                            <span class="result-value" id="stepsPerML_Rinse_display" style="font-size: 1.2em; font-weight: bold; color: #2c3e50;">200</span>
                        </div>
                    </div>
                    <div class="setting-group">
                        <label>Geschwindigkeit (Hz)</label>
                        <input type="number" id="speed_Rinse" value="2000" min="100" max="10000" step="100">
                    </div>
                    <div class="setting-group">
                        <label>Beschleunigung</label>
                        <input type="number" id="accel_Rinse" value="1000" min="100" max="10000" step="100">
                    </div>
                </div>

                <button class="btn btn-primary" onclick="savePumpSettings()" style="width: 100%; margin-top: 15px;">
                    💾 Geschwindigkeit & Beschleunigung speichern
                </button>
            </div>
            
            <!-- Manueller Pumpentest -->
            <h4 style="margin-top: 30px; margin-bottom: 15px;">🔧 Manueller Pumpentest</h4>
            <p style="margin-bottom: 15px; color: #7f8c8d;">
                Teste die Pumpen manuell um zu prüfen ob sie funktionieren oder um Schläuche zu füllen/entleeren.
            </p>
            
            <div class="settings-grid">
                <div class="setting-group">
                    <label>Pumpe auswählen</label>
                    <select id="testPump">
                        <option value="1">Pumpe 1 - Probenwasser</option>
                        <option value="2">Pumpe 2 - Säure/Reagenz</option>
                        <option value="3">Pumpe 3 - Spülung</option>
                    </select>
                </div>
                <div class="setting-group">
                    <label>Volumen (ml)</label>
                    <input type="number" id="testVolume" value="5" step="0.1" min="0.1" max="100">
                </div>
                <div class="setting-group">
                    <label>Richtung</label>
                    <select id="testDirection">
                        <option value="false">Vorwärts (Pumpen)</option>
                        <option value="true">Rückwärts (Saugen)</option>
                    </select>
                </div>
            </div>

            <div style="display: flex; align-items: center; gap: 10px; margin-top: 15px;">
                <button id="btnTestPump" class="btn btn-primary" onclick="testPump()" style="flex: 1;">
                    ▶️ Pumpe testen
                </button>
                <div id="testPumpSpinner" style="display: none;">
                    <div class="spinner"></div>
                </div>
            </div>

            <!-- Automatische Kalibrierung -->
            <h4 style="margin-top: 40px; margin-bottom: 15px;">📏 Automatische Kalibrierung</h4>
            <p style="margin-bottom: 15px; color: #7f8c8d;">
                Kalibriere die Pumpen für exakte Dosierung. Die Pumpe pumpt eine definierte Menge, 
                du misst das tatsächliche Volumen mit einem Messzylinder und gibst es ein.
            </p>
            
            <div id="calibrationStep1" style="display: block;">
                <div class="settings-grid">
                    <div class="setting-group">
                        <label>Pumpe zum Kalibrieren</label>
                        <select id="calibratePump">
                            <option value="1">Pumpe 1 - Probenwasser</option>
                            <option value="2">Pumpe 2 - Säure/Reagenz</option>
                            <option value="3">Pumpe 3 - Spülung</option>
                        </select>
                    </div>
                    <div class="setting-group">
                        <label>Anzahl Schritte</label>
                        <input type="number" id="calibrateSteps" value="2000" step="100" min="1">
                        <small style="color: #7f8c8d;">Mindestens 1 Schritt</small>
                    </div>
                </div>

                <div style="display: flex; align-items: center; gap: 10px; margin-top: 15px;">
                    <button id="btnStartCalibration" class="btn btn-primary" onclick="startCalibration()" style="flex: 1;">
                        🚀 Kalibrierung starten
                    </button>
                    <div id="calibrationSpinner" style="display: none;">
                        <div class="spinner"></div>
                    </div>
                </div>
            </div>
            
            <div id="calibrationStep2" style="display: none;">
                <div class="status-card working">
                    <div>⏳ Kalibrierung läuft...</div>
                    <div style="margin-top: 10px;">
                        Die Pumpe hat <strong id="calStepsDisplay">2000</strong> Schritte ausgeführt.
                        <br>Messe das geförderte Volumen mit einem Messzylinder.
                    </div>
                </div>
                
                <div class="setting-group" style="margin-top: 20px;">
                    <label>Gemessenes Volumen (ml)</label>
                    <input type="number" id="actualVolume" step="0.1" min="0.1" placeholder="z.B. 9.8">
                    <small style="color: #7f8c8d;">Gib das tatsächlich geförderte Volumen ein</small>
                </div>
                
                <div class="control-panel" style="margin-top: 15px;">
                    <button class="btn btn-primary" onclick="finishCalibration()">
                        ✅ Kalibrierung abschließen
                    </button>
                    <button class="btn btn-danger" onclick="cancelCalibration()">
                        ❌ Abbrechen
                    </button>
                </div>
            </div>
            
            <!-- Hinweise -->
            <div style="background: #fff3cd; border-left: 4px solid #ffc107; padding: 15px; margin-top: 30px; border-radius: 5px;">
                <strong>💡 Kalibrierungs-Tipps:</strong>
                <ul style="margin: 10px 0 0 20px; line-height: 1.6;">
                    <li>Verwende einen genauen Messzylinder (mindestens 0.1ml Genauigkeit)</li>
                    <li>Führe die Kalibrierung 2-3 mal durch und nimm den Durchschnitt</li>
                    <li>Kalibriere bei der Temperatur, bei der später auch gemessen wird</li>
                    <li>Stelle sicher, dass keine Luftblasen im Schlauch sind</li>
                    <li>Für die Säure-Pumpe (Pumpe 2) ist höchste Präzision wichtig!</li>
                </ul>
            </div>
        </div>

        <!-- WiFi Tab -->
        <div id="wifi" class="tab-content">
            <h3>📡 WiFi-Konfiguration</h3>
            <div class="setting-group">
                <label>Verfügbare Netzwerke</label>
                <button class="btn btn-primary" onclick="scanWiFi()" style="width: 100%; margin-bottom: 10px;">
                    🔍 Netzwerke scannen
                </button>
                <ul class="wifi-list" id="wifiList">
                    <li style="text-align: center; padding: 20px;">Klicke auf "Scannen"</li>
                </ul>
            </div>
            <div class="setting-group">
                <label>SSID</label>
                <input type="text" id="wifiSSID" placeholder="Netzwerk-Name">
            </div>
            <div class="setting-group">
                <label>Passwort</label>
                <input type="password" id="wifiPassword" placeholder="WiFi-Passwort">
            </div>
            <button class="btn btn-primary" onclick="saveWiFi()" style="width: 100%;">
                💾 WiFi speichern & neu starten
            </button>
        </div>

        <!-- Calibration Tab -->
        <div id="calibration" class="tab-content">
            <h3>🎯 pH-Sensor Kalibrierung</h3>
            <p style="margin-bottom: 20px;">Tauche die pH-Sonde in die Kalibrierlösung und klicke auf den entsprechenden Button.</p>

            <div class="control-panel">
                <button id="btnCalPH4" class="btn btn-primary" onclick="calibratePH(4)">
                    Kalibriere pH 4.0
                </button>
                <button id="btnCalPH7" class="btn btn-primary" onclick="calibratePH(7)">
                    Kalibriere pH 7.0
                </button>
            </div>

            <!-- Kalibrierungs-Fortschritt (nur sichtbar während Kalibrierung) -->
            <div id="calibrationProgress" class="status-card" style="margin-top: 20px; display: none;">
                <h4 style="margin-bottom: 10px;">⏳ Kalibrierung läuft...</h4>
                <div style="margin-bottom: 10px; font-size: 0.95em; color: #555;" id="calProgressText">
                    Warte auf stabile Messung (30 Sekunden)
                </div>
                <div style="width: 100%; height: 25px; background: #e9ecef; border-radius: 12px; overflow: hidden; position: relative;">
                    <div id="calProgressBar" style="height: 100%; background: linear-gradient(90deg, #4CAF50, #45a049); width: 0%; transition: width 0.3s ease; display: flex; align-items: center; justify-content: center;">
                        <span id="calProgressPercent" style="color: white; font-weight: bold; font-size: 0.85em; position: absolute; left: 50%; transform: translateX(-50%);">0%</span>
                    </div>
                </div>
                <div style="margin-top: 8px; font-size: 0.85em; color: #666;" id="calRemainingTime">
                    Verbleibend: 30s
                </div>
            </div>

            <!-- Kalibrierungsstatus -->
            <div class="status-card" style="margin-top: 20px;">
                <h4 style="margin-bottom: 15px;">Kalibrierungsstatus</h4>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px;">
                    <div style="padding: 10px; background: #f8f9fa; border-radius: 5px;">
                        <div style="font-weight: bold; margin-bottom: 5px;">pH 4.0</div>
                        <div id="ph4Status" style="font-size: 0.9em; color: #888;">Nicht kalibriert</div>
                    </div>
                    <div style="padding: 10px; background: #f8f9fa; border-radius: 5px;">
                        <div style="font-weight: bold; margin-bottom: 5px;">pH 7.0</div>
                        <div id="ph7Status" style="font-size: 0.9em; color: #888;">Nicht kalibriert</div>
                    </div>
                </div>
            </div>

            <div class="status-card">
                <div>Aktueller pH-Wert</div>
                <div class="ph-display" id="phDisplayCal">pH 7.00</div>
            </div>

            <!-- Sonden-Gesundheitscheck -->
            <div class="status-card" style="margin-top: 30px;">
                <h4 style="margin-bottom: 15px;">🩺 Sonden-Gesundheitscheck</h4>

                <!-- Effizienz-Anzeige -->
                <div style="text-align: center; margin-bottom: 20px;">
                    <div style="font-size: 14px; color: #666; margin-bottom: 5px;">Sonden-Effizienz</div>
                    <div id="sensorEfficiency" style="font-size: 48px; font-weight: bold; color: #27ae60;">-- %</div>
                    <div id="sensorEfficiencyStatus" style="font-size: 14px; color: #666; margin-top: 5px;">Noch keine Kalibrierungsdaten</div>
                </div>

                <!-- Effizienz-Legende -->
                <div style="display: flex; justify-content: center; gap: 20px; margin-bottom: 20px; font-size: 12px;">
                    <span><span style="color: #27ae60;">●</span> 95-102%: Wie neu</span>
                    <span><span style="color: #f39c12;">●</span> 85-95%: OK</span>
                    <span><span style="color: #e74c3c;">●</span> &lt;85%: Ersetzen</span>
                </div>

                <!-- Sensor-Info -->
                <div style="background: #f8f9fa; padding: 10px; border-radius: 5px; font-size: 13px; margin-bottom: 15px;">
                    <div style="display: flex; justify-content: space-between;">
                        <span>Sonde installiert:</span>
                        <span id="sensorInstallDate">--</span>
                    </div>
                </div>

                <!-- Reset Button -->
                <button class="btn btn-danger" onclick="resetPHCalibrationHistory()" style="width: 100%;">
                    🗑️ Neue Sonde - Alle Kalibrierungen löschen
                </button>
            </div>

            <!-- Kalibrierungs-Chart -->
            <div class="status-card" style="margin-top: 20px;">
                <h4 style="margin-bottom: 15px;">📈 Kalibrierungsverlauf</h4>
                <canvas id="phCalibrationChart" width="700" height="300" style="width: 100%; max-width: 700px;"></canvas>
                <div style="font-size: 11px; color: #888; margin-top: 10px; text-align: center;">
                    Gestrichelt: Ideale Steigung (-59.16 mV/pH) | Steigung wird nur aus Kalibrierungen berechnet, die innerhalb von 2 Stunden durchgeführt wurden.
                </div>
            </div>

            <!-- Kalibrierungs-Tabelle -->
            <div class="status-card" style="margin-top: 20px;">
                <h4 style="margin-bottom: 15px;">📋 Kalibrierungshistorie</h4>
                <div style="overflow-x: auto;">
                    <table id="phCalibrationTable" style="width: 100%; border-collapse: collapse; font-size: 13px;">
                        <thead>
                            <tr style="background: #f8f9fa;">
                                <th style="padding: 10px; text-align: left; border-bottom: 2px solid #dee2e6;">Datum</th>
                                <th style="padding: 10px; text-align: left; border-bottom: 2px solid #dee2e6;">Typ</th>
                                <th style="padding: 10px; text-align: right; border-bottom: 2px solid #dee2e6;">mV</th>
                                <th style="padding: 10px; text-align: center; border-bottom: 2px solid #dee2e6; width: 50px;">Löschen</th>
                            </tr>
                        </thead>
                        <tbody id="phCalibrationTableBody">
                            <tr><td colspan="4" style="text-align: center; padding: 20px; color: #888;">Keine Kalibrierungen vorhanden</td></tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>

        <!-- Behälter Tab -->
        <div id="containers" class="tab-content">
            <h3>🧪 Behälter-Überwachung</h3>
            <p style="margin-bottom: 20px; color: #666;">Überwachung der Füllstände aller Behälter.</p>

            <!-- Säurebehälter -->
            <div class="status-card" id="acidContainerCard">
                <h4 style="margin-bottom: 15px;">🧴 Säurebehälter</h4>
                <div style="display: flex; justify-content: space-between; margin-bottom: 10px;">
                    <span>Aktuell: <strong id="acidCurrentLevel">0.0</strong> ml</span>
                    <span>Max: <input type="number" id="acidMaxVolume" style="width: 70px; text-align: right;" step="100" min="100"> ml</span>
                </div>
                <div style="background: #e9ecef; height: 30px; border-radius: 15px; overflow: hidden; position: relative;">
                    <div id="acidContainerBar" style="height: 100%; width: 0%; transition: width 0.5s, background 0.5s;"></div>
                    <div style="position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); font-weight: bold; color: #2c3e50; text-shadow: 0 0 3px white;" id="acidContainerPercent">0%</div>
                </div>
                <!-- Prognose -->
                <div style="margin-top: 10px; padding: 8px 10px; background: #f8f9fa; border-radius: 6px; font-size: 0.85em; color: #333;">
                    <span style="color: #666;">Ø</span> <strong id="acidAvgUsage">-</strong> ml &nbsp;|&nbsp;
                    <span style="color: #666;">Reicht:</span> <strong id="acidMeasurementsLeft">-</strong> Messungen &nbsp;|&nbsp;
                    <span id="acidDaysLeft">-</span>
                </div>
                <div style="margin-top: 8px; font-size: 0.8em; color: #666;" id="acidTimestamp">Zuletzt nachgefüllt: -</div>
                <div style="margin-top: 10px; text-align: center;">
                    <button class="btn btn-primary" onclick="showAcidRefillDialog()">🔄 Nachgefüllt</button>
                </div>
            </div>

            <!-- Abwasserbehälter -->
            <div class="status-card" id="wasteContainerCard">
                <h4 style="margin-bottom: 15px;">🗑️ Abwasserbehälter</h4>
                <div style="display: flex; justify-content: space-between; margin-bottom: 10px;">
                    <span>Aktuell: <strong id="wasteCurrentLevel">0.0</strong> ml</span>
                    <span>Max: <input type="number" id="wasteMaxVolume" style="width: 70px; text-align: right;" step="100" min="100"> ml</span>
                </div>
                <div style="background: #e9ecef; height: 30px; border-radius: 15px; overflow: hidden; position: relative;">
                    <div id="wasteContainerBar" style="height: 100%; width: 0%; transition: width 0.5s, background 0.5s;"></div>
                    <div style="position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); font-weight: bold; color: #2c3e50; text-shadow: 0 0 3px white;" id="wasteContainerPercent">0%</div>
                </div>
                <!-- Prognose -->
                <div style="margin-top: 10px; padding: 8px 10px; background: #f8f9fa; border-radius: 6px; font-size: 0.85em; color: #333;">
                    <strong id="wastePerMeasurement">-</strong> ml/Messung &nbsp;|&nbsp;
                    <span style="color: #666;">Voll in:</span> <strong id="wasteMeasurementsUntilFull">-</strong> Messungen &nbsp;|&nbsp;
                    <span id="wasteDaysUntilFull">-</span>
                </div>
                <div style="margin-top: 8px; font-size: 0.8em; color: #666;" id="wasteTimestamp">Zuletzt entleert: -</div>
                <div style="margin-top: 10px; text-align: center;">
                    <button class="btn btn-primary" onclick="emptyWasteContainer()">✓ Entleert</button>
                </div>
            </div>

            <!-- Aquarium-Wasserverbrauch -->
            <div class="status-card" id="aquariumContainerCard" style="border-left: 4px solid #3498db;">
                <h4 style="margin-bottom: 15px;">🐠 Aquarium-Wasserverbrauch</h4>
                <div style="display: flex; justify-content: space-between; margin-bottom: 10px;">
                    <span>Gesamt entnommen: <strong id="aquariumTotalUsed">0.0</strong> ml</span>
                </div>
                <div style="background: #e9ecef; height: 30px; border-radius: 15px; overflow: hidden; position: relative;">
                    <div id="aquariumContainerBar" style="height: 100%; background: linear-gradient(90deg, #3498db, #2980b9); width: 100%;"></div>
                    <div style="position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); font-weight: bold; color: white;" id="aquariumDisplayText">0.0 ml</div>
                </div>
                <!-- Prognose -->
                <div style="margin-top: 10px; padding: 8px 10px; background: #f8f9fa; border-radius: 6px; font-size: 0.85em; color: #333;">
                    <strong id="aquariumPerMeasurement">-</strong> ml/Messung &nbsp;|&nbsp;
                    <span id="aquariumPerDay">-</span>/Tag &nbsp;|&nbsp;
                    <span id="aquariumPerWeek">-</span>/Woche
                </div>
                <div style="margin-top: 8px; font-size: 0.8em; color: #666;" id="aquariumTimestamp">Zuletzt zurückgesetzt: -</div>
                <div style="margin-top: 10px; text-align: center;">
                    <button class="btn btn-secondary" onclick="resetAquariumCounter()">↻ Zähler zurücksetzen</button>
                </div>
            </div>

            <!-- Einstellungen speichern -->
            <div style="margin-top: 20px; text-align: center;">
                <button class="btn btn-primary" onclick="saveContainerSettings()">💾 Max-Volumen speichern</button>
            </div>
        </div>

        <!-- Acid Refill Dialog (Modal) -->
        <div id="acidRefillModal" style="display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); z-index: 1000; justify-content: center; align-items: center;">
            <div style="background: white; padding: 25px; border-radius: 10px; max-width: 400px; width: 90%;">
                <h3 style="margin-bottom: 15px;">🧴 Säurebehälter nachgefüllt</h3>
                <p style="margin-bottom: 15px; color: #666;">Wie viel ml Säure ist jetzt im Behälter?</p>
                <div style="margin-bottom: 20px;">
                    <input type="number" id="acidRefillLevel" style="width: 100%; padding: 10px; font-size: 18px; text-align: center;" step="10" min="0">
                    <small style="color: #888;">ml (Max: <span id="acidRefillMax">1000</span> ml)</small>
                </div>
                <div style="display: flex; gap: 10px;">
                    <button class="btn btn-secondary" onclick="closeAcidRefillDialog()" style="flex: 1;">Abbrechen</button>
                    <button class="btn btn-primary" onclick="confirmAcidRefill()" style="flex: 1;">✓ Bestätigen</button>
                </div>
            </div>
        </div>
    </div>

    <script>
        let selectedSSID = '';
        let calibrationInProgress = false;
        let calibrationTargetVolume = 0;
        
        function showTab(tabName) {
            document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.getElementById(tabName).classList.add('active');
            event.target.classList.add('active');

            if (tabName === 'measurements') {
                loadMeasurements();
            } else if (tabName === 'settings') {
                loadSettings();
                loadOTAInfo();
            } else if (tabName === 'pumps') {
                loadPumpSettings();
            } else if (tabName === 'calibration') {
                updateCalibrationStatus();
                loadPHCalibrationHistory();
            } else if (tabName === 'containers') {
                loadContainerLevels();
            }
        }
        
        function testPump() {
            const pump = parseInt(document.getElementById('testPump').value);
            const volume = parseFloat(document.getElementById('testVolume').value);
            const reverse = document.getElementById('testDirection').value === 'true';

            if (volume <= 0 || volume > 100) {
                alert('Volumen muss zwischen 0.1 und 100 ml sein');
                return;
            }

            // Button deaktivieren und Spinner anzeigen
            const btn = document.getElementById('btnTestPump');
            const spinner = document.getElementById('testPumpSpinner');
            btn.disabled = true;
            spinner.style.display = 'block';

            fetch('/api/pump/test', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({pump, volume, reverse})
            }).then(response => {
                // Spinner ausblenden und Button aktivieren
                spinner.style.display = 'none';
                btn.disabled = false;

                if (response.ok) {
                    // Erfolgreich - keine Alert mehr
                } else {
                    return response.text().then(text => alert('Fehler: ' + text));
                }
            });
        }
        
        function startCalibration() {
            const pump = parseInt(document.getElementById('calibratePump').value);
            const steps = parseInt(document.getElementById('calibrateSteps').value);

            if (steps < 1) {
                alert('Schritte müssen mindestens 1 sein');
                return;
            }

            calibrationTargetVolume = steps; // Speichere Schritte

            // Button deaktivieren und Spinner anzeigen
            const btn = document.getElementById('btnStartCalibration');
            const spinner = document.getElementById('calibrationSpinner');
            btn.disabled = true;
            spinner.style.display = 'block';

            fetch('/api/pump/calibrate/start', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({pump, steps})
            }).then(response => {
                // Spinner ausblenden und Button aktivieren
                spinner.style.display = 'none';
                btn.disabled = false;

                if (response.ok) {
                    calibrationInProgress = true;
                    document.getElementById('calibrationStep1').style.display = 'none';
                    document.getElementById('calibrationStep2').style.display = 'block';
                    document.getElementById('calStepsDisplay').textContent = steps;
                    document.getElementById('actualVolume').value = '';
                    document.getElementById('actualVolume').focus();
                } else {
                    return response.text().then(text => alert('Fehler: ' + text));
                }
            });
        }
        
        function finishCalibration() {
            const actualVolume = parseFloat(document.getElementById('actualVolume').value);
            const steps = calibrationTargetVolume; // Enthält die Anzahl Schritte

            if (!actualVolume || actualVolume <= 0) {
                alert('Bitte gib das gemessene Volumen ein');
                return;
            }

            // Berechne Schritte/ml
            const newStepsPerML = Math.round(steps / actualVolume);

            fetch('/api/pump/calibrate/finish', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    steps: steps,
                    actualVolume: actualVolume
                })
            }).then(response => {
                if (response.ok) {
                    calibrationInProgress = false;
                    document.getElementById('calibrationStep1').style.display = 'block';
                    document.getElementById('calibrationStep2').style.display = 'none';
                    loadPumpSettings();  // Aktualisiere die Anzeige
                } else {
                    return response.text().then(text => alert('Fehler: ' + text));
                }
            });
        }
        
        function cancelCalibration() {
            calibrationInProgress = false;
            document.getElementById('calibrationStep1').style.display = 'block';
            document.getElementById('calibrationStep2').style.display = 'none';
        }

        function loadPumpSettings() {
            fetch('/api/pumps')
                .then(r => r.json())
                .then(data => {
                    // Schritte/ml nur in Display-Feldern anzeigen (read-only)
                    document.getElementById('stepsPerML_Sample_display').textContent = data.stepsPerML_Sample;
                    document.getElementById('stepsPerML_Reagent_display').textContent = data.stepsPerML_Reagent;
                    document.getElementById('stepsPerML_Rinse_display').textContent = data.stepsPerML_Rinse;

                    // Geschwindigkeit und Beschleunigung in Eingabefelder laden
                    document.getElementById('speed_Sample').value = data.speed_Sample;
                    document.getElementById('speed_Reagent').value = data.speed_Reagent;
                    document.getElementById('speed_Rinse').value = data.speed_Rinse;
                    document.getElementById('accel_Sample').value = data.accel_Sample;
                    document.getElementById('accel_Reagent').value = data.accel_Reagent;
                    document.getElementById('accel_Rinse').value = data.accel_Rinse;
                });

            // Prüfe Kalibrierungsstatus vom Backend
            fetch('/api/pump/calibration')
                .then(r => r.json())
                .then(data => {
                    if (data.isCalibrating) {
                        // Backend hat aktive Kalibrierung - UI synchronisieren
                        calibrationInProgress = true;
                        calibrationTargetVolume = data.calibrationSteps || 1000; // Fallback
                        document.getElementById('calibrationStep1').style.display = 'none';
                        document.getElementById('calibrationStep2').style.display = 'block';
                        document.getElementById('calStepsDisplay').textContent = calibrationTargetVolume;
                        console.log('Kalibrierung vom Backend wiederhergestellt');
                    }
                });
        }

        function savePumpSettings() {
            // Hole aktuelle Schritte/ml aus dem Backend (nicht vom User änderbar)
            fetch('/api/pumps')
                .then(r => r.json())
                .then(currentData => {
                    // Speichere: Alte Schritte/ml + neue Speed/Accel
                    const pumpSettings = {
                        stepsPerML_Sample: currentData.stepsPerML_Sample,    // Unverändert
                        stepsPerML_Reagent: currentData.stepsPerML_Reagent,  // Unverändert
                        stepsPerML_Rinse: currentData.stepsPerML_Rinse,      // Unverändert
                        speed_Sample: parseInt(document.getElementById('speed_Sample').value),
                        speed_Reagent: parseInt(document.getElementById('speed_Reagent').value),
                        speed_Rinse: parseInt(document.getElementById('speed_Rinse').value),
                        accel_Sample: parseInt(document.getElementById('accel_Sample').value),
                        accel_Reagent: parseInt(document.getElementById('accel_Reagent').value),
                        accel_Rinse: parseInt(document.getElementById('accel_Rinse').value)
                    };

                    fetch('/api/pumps', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify(pumpSettings)
                    }).then(() => {
                        alert('✓ Geschwindigkeit & Beschleunigung gespeichert');
                        loadPumpSettings();
                    });
                });
        }

        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    // Header
                    document.getElementById('headerInfo').textContent =
                        `${data.apMode ? 'AP-Modus' : 'WiFi verbunden'} | IP: ${data.ip} | Zeit: ${data.time}`;

                    // Dashboard: Letzter KH-Wert (mit Null-Prüfung)
                    const khText = (data.lastKH !== null && data.lastKH !== undefined)
                        ? `KH ${data.lastKH.toFixed(1)} dKH`
                        : 'KH --';
                    document.getElementById('phDisplay').textContent = khText;

                    // Kalibrierungs-Tab: Live pH-Wert (mit Null-Prüfung)
                    const phText = (data.phValue !== null && data.phValue !== undefined)
                        ? `pH ${data.phValue.toFixed(2)}`
                        : 'pH --';
                    document.getElementById('phDisplayCal').textContent = phText;

                    // Status
                    document.getElementById('stateText').textContent = data.stateDescription;
                    const tempText = (data.temperature !== null && data.temperature !== undefined)
                        ? `${data.temperature.toFixed(1)}°C`
                        : '--°C';
                    document.getElementById('additionalInfo').textContent =
                        `Temperatur: ${tempText} | pH stabil: ${data.phStable ? 'Ja' : 'Nein'}`;

                    // Status-Card Farbe
                    const card = document.getElementById('statusCard');
                    card.className = 'status-card';
                    if (data.state === 8) card.classList.add('error');  // ERROR_STATE = 8
                    else if (data.state > 0 && data.state < 7) card.classList.add('working');  // RINSING(1) bis CLEANUP(6)

                    // Buttons
                    const isRunning = data.state > 0 && data.state < 7;

                    // Clearance-Banner nur anzeigen wenn NICHT aktiv messend (= nach Reboot/Stromausfall)
                    const banner = document.getElementById('clearanceBanner');
                    if (data.measurementCompleted === false && !isRunning) {
                        banner.style.display = 'block';
                    } else {
                        banner.style.display = 'none';
                    }

                    const blocked = data.measurementCompleted === false && !isRunning;
                    document.getElementById('btnStart').disabled = isRunning || blocked;
                    document.getElementById('btnStop').disabled = !isRunning;

                    // Chart initialisieren bei laufender Messung (state 3-6)
                    // Erlaubt Graph-Anzeige auch nach Seiten-Reload während Messung
                    if (data.state >= 3 && data.state <= 6 && !chartVisible) {
                        initChart();
                    }

                    // Chart verstecken bei IDLE (0), COMPLETED (7) oder ERROR (8)
                    if (data.state === 0 || data.state === 7 || data.state === 8) {
                        hideChart();
                    }

                    // Chart aktualisieren während gesamter Messung (state 3-6: pH-Messung, Titration, Berechnung, Cleanup)
                    if ((data.state >= 3 && data.state <= 6) && chartVisible) {
                        // Säurevolumen: Bei State 3 (pH-Akklimatisierung) immer 0 verwenden
                        // da der ESP acidUsed erst beim State-Wechsel zurücksetzt
                        const acidVolume = (data.state === 3) ? 0 : (data.acidUsed || 0);
                        addChartDataPoint(data.phValue, acidVolume);

                        // Live-KH-Wert berechnen und anzeigen (nur wenn Säure > 0)
                        if (acidVolume > 0) {
                            const effectiveConc = currentSettings.acidConcentration * (currentSettings.acidCorrectionFactor || 1.0);
                            const liveKH = (acidVolume * effectiveConc * 1000.0 / currentSettings.sampleVolume) * 2.8;
                            document.getElementById('liveKHValue').textContent = liveKH.toFixed(2);
                            document.getElementById('liveKH').style.display = 'block';
                        }
                    }

                    // Dosierprotokoll: neuen Step sammeln wenn vorhanden
                    if (data.stepCount && data.lastStep && data.stepCount > lastKnownStepCount) {
                        collectedSteps.push(data.lastStep);
                        lastKnownStepCount = data.stepCount;
                    }
                    if (collectedSteps.length > 0) {
                        document.getElementById('stepChartContainer').style.display = 'block';
                        drawStepChart(collectedSteps);
                    }

                    // Titrations-Info während Messung
                    if (data.state === 4) {  // TITRATING = 4
                        let info = ` | Initial pH: ${data.initialPH.toFixed(2)}`;
                        if (data.currentDoseVolume !== undefined) {
                            info += ` | Dosis: ${data.currentDoseVolume.toFixed(4)} ml`;
                            info += ` | Auswertung: ${data.currentStabilityTime.toFixed(0)}s`;
                            info += ` | Toleranz: ${data.currentTolerance.toFixed(3)}`;
                        }
                        document.getElementById('additionalInfo').textContent += info;

                        lastStabilityReached = data.stabilityReached || false;
                    }

                    // Vorratsbehälter-Schnellübersicht im Dashboard aktualisieren
                    updateDashboardContainers();

                    // Ergebnis anzeigen
                    if (data.state === 7 && data.result) {  // COMPLETED = 7
                        document.getElementById('resultCard').style.display = 'block';
                        document.getElementById('resultKH').textContent = `${data.result.khValue.toFixed(2)} dKH`;
                        document.getElementById('resultAcid').textContent = `${data.result.acidUsed.toFixed(3)} ml`;
                        document.getElementById('resultInitialPH').textContent = data.result.initialPH.toFixed(3);
                        document.getElementById('resultFinalPH').textContent = data.result.finalPH.toFixed(3);

                        // Messungen-Tabelle im Hintergrund aktualisieren (auch wenn Tab nicht offen)
                        // Damit beim nächsten Tab-Wechsel die neue Messung sofort da ist
                        loadMeasurements();
                    } else {
                        document.getElementById('resultCard').style.display = 'none';
                    }

                    // pH-Kalibrierungs-Status
                    if (data.phCalibrating) {
                        // Kalibrierung läuft - Buttons deaktivieren, Progress anzeigen
                        document.getElementById('btnCalPH4').disabled = true;
                        document.getElementById('btnCalPH7').disabled = true;
                        document.getElementById('calibrationProgress').style.display = 'block';

                        // Progress-Bar aktualisieren
                        const progress = data.phCalProgress || 0;
                        const remaining = data.phCalRemaining || 0;
                        const phVal = data.phCalValue || 0;

                        document.getElementById('calProgressBar').style.width = `${progress}%`;
                        document.getElementById('calProgressPercent').textContent = `${progress}%`;
                        document.getElementById('calRemainingTime').textContent = `Verbleibend: ${remaining}s`;

                        if (data.phCalStable) {
                            document.getElementById('calProgressText').textContent = `✓ pH ${phVal.toFixed(1)} kalibriert - Speichere...`;
                        } else {
                            document.getElementById('calProgressText').textContent = `Kalibriere pH ${phVal.toFixed(1)} - Warte auf stabile Messung...`;
                        }
                    } else {
                        // Kalibrierung beendet - Buttons wieder aktivieren, Progress verstecken
                        document.getElementById('btnCalPH4').disabled = false;
                        document.getElementById('btnCalPH7').disabled = false;
                        document.getElementById('calibrationProgress').style.display = 'none';

                        // Kalibrierungsstatus aktualisieren nach Abschluss
                        updateCalibrationStatus();
                    }
                });
        }

        // Vorratsbehälter-Schnellübersicht im Dashboard aktualisieren
        function updateDashboardContainers() {
            fetch('/api/containers')
                .then(r => r.json())
                .then(data => {
                    // Säurebehälter - Dashboard Mini-Anzeige
                    const acidPercent = (data.acidContainerLevel / data.acidContainerMax) * 100;
                    document.getElementById('dashAcidLevel').style.width = acidPercent + '%';
                    document.getElementById('dashAcidPercent').textContent = acidPercent.toFixed(0) + '%';
                    // Farbe basierend auf Füllstand (leerer = schlechter)
                    if (acidPercent < 10) {
                        document.getElementById('dashAcidLevel').style.background = '#e74c3c';
                    } else if (acidPercent < 30) {
                        document.getElementById('dashAcidLevel').style.background = '#f39c12';
                    } else {
                        document.getElementById('dashAcidLevel').style.background = '#2ecc71';
                    }

                    // Abwasserbehälter - Dashboard Mini-Anzeige
                    const wastePercent = (data.wasteContainerLevel / data.wasteContainerMax) * 100;
                    document.getElementById('dashWasteLevel').style.width = wastePercent + '%';
                    document.getElementById('dashWastePercent').textContent = wastePercent.toFixed(0) + '%';
                    // Farbe basierend auf Füllstand (voller = schlechter)
                    if (wastePercent > 90) {
                        document.getElementById('dashWasteLevel').style.background = '#e74c3c';
                    } else if (wastePercent > 70) {
                        document.getElementById('dashWasteLevel').style.background = '#f39c12';
                    } else {
                        document.getElementById('dashWasteLevel').style.background = '#2ecc71';
                    }

                    // Aquarium
                    document.getElementById('dashAquariumUsed').textContent = data.aquariumTotalUsed.toFixed(0) + ' ml';
                });
        }

        // Mess-Plan berechnen und anzeigen
        function updateMeasurementPlan() {
            const enabled = parseInt(document.getElementById('autoMeasureEnabled').value);
            const firstHour = parseInt(document.getElementById('firstMeasurementHour').value);
            const firstMinute = parseInt(document.getElementById('firstMeasurementMinute').value);
            const intervalHours = parseInt(document.getElementById('measurementIntervalHours').value);
            const repeatDays = parseInt(document.getElementById('measurementRepeatDays').value);

            const planDiv = document.getElementById('measurementPlan');
            const contentDiv = document.getElementById('measurementPlanContent');
            const nextDiv = document.getElementById('nextMeasurement');

            if (enabled === 0) {
                planDiv.style.display = 'none';
                return;
            }

            planDiv.style.display = 'block';

            // Aktuelle Zeit holen (Browser-Zeit)
            const now = new Date();
            const nowMinutes = now.getHours() * 60 + now.getMinutes();

            // Berechne welcher Tag im Zyklus heute ist
            const daysSinceEpoch = Math.floor(now.getTime() / (1000 * 60 * 60 * 24));
            const currentDayInCycle = daysSinceEpoch % repeatDays;
            const isMeasurementDay = (currentDayInCycle === 0);

            // Berechne alle Mess-Zeiten für den Zyklus
            let times = [];
            let currentTime = firstHour * 60 + firstMinute;

            times.push(currentTime);

            if (intervalHours > 0) {
                while (true) {
                    currentTime += intervalHours * 60;
                    if (currentTime >= 24 * 60) break;
                    times.push(currentTime);
                }
            }

            // Finde nächste Mess-Zeit
            let nextMeasurementTime = null;
            if (isMeasurementDay) {
                for (let time of times) {
                    if (time > nowMinutes) {
                        nextMeasurementTime = time;
                        break;
                    }
                }
            }

            // HTML generieren
            let html = '<div style="font-size: 1.1em;">';

            if (repeatDays === 1) {
                html += '<strong>Wiederholt sich täglich:</strong><br>';
            } else {
                html += `<strong>Wiederholt sich alle ${repeatDays} Tage:</strong><br>`;
                html += '<div style="margin-top: 8px;"><strong>Tag 1:</strong></div>';
            }

            times.forEach(time => {
                const h = Math.floor(time / 60);
                const m = time % 60;
                const timeStr = `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}`;

                // Fett wenn das die nächste Messung ist
                const isBold = (isMeasurementDay && time === nextMeasurementTime);
                if (isBold) {
                    html += `<div style="margin-left: 20px; padding: 3px 0;"><strong>• ${timeStr} ← Nächste Messung</strong></div>`;
                } else {
                    html += `<div style="margin-left: 20px; padding: 3px 0;">• ${timeStr}</div>`;
                }
            });

            if (repeatDays > 1) {
                html += `<div style="margin-top: 8px;"><strong>Tag 2-${repeatDays}:</strong></div>`;
                html += '<div style="margin-left: 20px; padding: 3px 0;">• Keine Messungen</div>';
            }

            html += '</div>';
            contentDiv.innerHTML = html;

            // Nächste Messung Info
            if (!isMeasurementDay) {
                nextDiv.innerHTML = '⏰ <strong>Heute keine Messung</strong> (nicht im Mess-Zyklus)';
            } else if (nextMeasurementTime === null) {
                nextDiv.innerHTML = '⏰ <strong>Heute keine Messung mehr</strong> (alle Messungen abgeschlossen)';
            } else {
                const h = Math.floor(nextMeasurementTime / 60);
                const m = nextMeasurementTime % 60;
                nextDiv.innerHTML = `⏰ Nächste Messung: <strong>Heute um ${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}</strong>`;
            }
        }

        function releaseMeasurement() {
            fetch('/api/clearance', { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        document.getElementById('clearanceBanner').style.display = 'none';
                        document.getElementById('btnStart').disabled = false;
                    }
                })
                .catch(e => alert('Fehler: ' + e));
        }

        function startMeasurement() {
            if (confirm('Messung starten?')) {
                fetch('/api/start', {method: 'POST'})
                    .then(() => {
                        hideChart();  // Reset chart bei neuem Start
                        updateStatus();
                    });
            }
        }
        
        function stopMeasurement() {
            const btn = document.getElementById('btnStop');

            // Verhindere mehrfaches Klicken
            if (btn.disabled) {
                return;
            }

            const msg = "⚠️ NOTFALL-STOP ⚠️\n\n" +
                "• Alle Pumpen werden SOFORT gestoppt\n" +
                "• Rührer wird SOFORT gestoppt\n" +
                "• Messung wird abgebrochen\n\n" +
                "❗ Behälter muss MANUELL geleert werden!";

            if (confirm(msg)) {
                // Button deaktivieren und Text ändern
                btn.disabled = true;
                const originalText = btn.innerHTML;
                btn.innerHTML = '⏳ Stoppe...';

                fetch('/api/stop', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({})
                }).then(() => {

                    // Warte bis System wieder IDLE ist
                    const checkInterval = setInterval(() => {
                        fetch('/api/status')
                            .then(r => r.json())
                            .then(data => {
                                // State 0 = IDLE, State 7 = COMPLETED
                                if (data.state === 0 || data.state === 7) {
                                    clearInterval(checkInterval);
                                    btn.disabled = false;
                                    btn.innerHTML = originalText;
                                    updateStatus(); // Finales Update
                                }
                            });
                    }, 500); // Prüfe alle 500ms

                    // Sicherheits-Timeout: Nach 30 Sekunden aufgeben
                    setTimeout(() => {
                        clearInterval(checkInterval);
                        btn.disabled = false;
                        btn.innerHTML = originalText;
                        updateStatus();
                    }, 30000);

                }).catch(err => {
                    console.error('Stop-Fehler:', err);
                    btn.disabled = false;
                    btn.innerHTML = originalText;
                });
            }
        }

        // ==================== OTA UPDATE FUNKTIONEN ====================
        function loadOTAInfo() {
            fetch('/api/ota/info')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('firmwareVersion').textContent = data.version || '1.0.0';
                    const freeKB = Math.round(data.freeSketchSpace / 1024);
                    document.getElementById('freeSketchSpace').textContent = freeKB + ' KB';
                })
                .catch(err => {
                    console.error('OTA Info Fehler:', err);
                });
        }

        function uploadFirmware() {
            const fileInput = document.getElementById('firmwareFile');
            const file = fileInput.files[0];

            if (!file) {
                alert('Bitte wähle eine Firmware-Datei (.bin) aus!');
                return;
            }

            if (!file.name.endsWith('.bin')) {
                alert('Nur .bin Dateien sind erlaubt!');
                return;
            }

            if (!confirm('Firmware wirklich aktualisieren?\\n\\nDas Gerät wird nach dem Update automatisch neu gestartet.\\nNicht während einer Messung durchführen!')) {
                return;
            }

            const progressDiv = document.getElementById('otaProgress');
            const progressBar = document.getElementById('otaProgressBar');
            const statusText = document.getElementById('otaStatus');

            progressDiv.style.display = 'block';
            progressBar.style.width = '0%';
            statusText.textContent = 'Upload wird vorbereitet...';

            const formData = new FormData();
            formData.append('firmware', file);

            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/ota/update', true);

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    const percent = Math.round((e.loaded / e.total) * 100);
                    progressBar.style.width = percent + '%';
                    statusText.textContent = 'Hochladen: ' + percent + '%';
                }
            };

            xhr.onload = function() {
                if (xhr.status === 200) {
                    const response = JSON.parse(xhr.responseText);
                    if (response.success) {
                        progressBar.style.background = '#27ae60';
                        statusText.textContent = '✓ Update erfolgreich! Gerät startet neu...';
                        setTimeout(() => {
                            statusText.textContent = 'Warte auf Neustart...';
                            // Nach 5 Sekunden Seite neu laden
                            setTimeout(() => location.reload(), 5000);
                        }, 2000);
                    } else {
                        progressBar.style.background = '#e74c3c';
                        statusText.textContent = '✗ ' + response.message;
                    }
                } else {
                    progressBar.style.background = '#e74c3c';
                    statusText.textContent = '✗ Upload fehlgeschlagen (HTTP ' + xhr.status + ')';
                }
            };

            xhr.onerror = function() {
                progressBar.style.background = '#e74c3c';
                statusText.textContent = '✗ Verbindungsfehler';
            };

            xhr.send(formData);
        }

        function loadSettings() {
            fetch('/api/settings')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('sampleVolume').value = data.sampleVolume;
                    document.getElementById('rinseVolume').value = data.rinseVolume;
                    document.getElementById('overpumpPercent').value = data.overpumpPercent;
                    document.getElementById('acidConcentration').value = data.acidConcentration.toFixed(2);
                    document.getElementById('acidCorrectionFactor').value = (data.acidCorrectionFactor || 1.0).toFixed(3);
                    updateEffectiveAcidConcentration();
                    document.getElementById('targetPH').value = data.targetPH.toFixed(2);
                    document.getElementById('phAcclimatizationTime').value = Math.round(data.phAcclimatizationTime / 1000);
                    document.getElementById('maxStabilityTimeout').value = data.maxStabilityTimeout || 120;

                    // Dynamische Titration
                    document.getElementById('doseVolumeMin').value = data.doseVolumeMin || 0.01;
                    document.getElementById('doseVolumeMax').value = data.doseVolumeMax || 0.5;
                    document.getElementById('stabilityTimeMin').value = data.stabilityTimeMin || 5;
                    document.getElementById('stabilityTimeMax').value = data.stabilityTimeMax || 30;
                    document.getElementById('toleranceMin').value = data.toleranceMin || 0.05;
                    document.getElementById('toleranceMax').value = data.toleranceMax || 0.3;
                    document.getElementById('interpolationExponent').value = data.interpolationExponent || 2.5;
                    document.getElementById('acidPrimeVolume').value = data.acidPrimeVolume || 0.2;
                    document.getElementById('submersionVolume').value = data.submersionVolume || 5.0;

                    document.getElementById('maxContainerVolume').value = data.maxContainerVolume || 100;

                    // Rührer-Geschwindigkeit
                    const stirrerSpeed = data.stirrerSpeed || 80;
                    document.getElementById('stirrerSpeed').value = stirrerSpeed;
                    document.getElementById('stirrerSpeedValue').textContent = stirrerSpeed;
                    const stirrerFreq = data.stirrerPwmFreq || 25000;
                    document.getElementById('stirrerPwmFreq').value = stirrerFreq;
                    document.getElementById('stirrerFreqValue').textContent = stirrerFreq;

                    // Auto-Messung Einstellungen (RTC-basiert)
                    document.getElementById('autoMeasureEnabled').value = data.autoMeasureEnabled || 0;
                    document.getElementById('firstMeasurementHour').value = data.firstMeasurementHour || 6;
                    document.getElementById('firstMeasurementMinute').value = data.firstMeasurementMinute || 0;
                    document.getElementById('measurementIntervalHours').value = data.measurementIntervalHours || 0;
                    document.getElementById('measurementRepeatDays').value = data.measurementRepeatDays || 1;

                    // Mess-Plan berechnen und anzeigen
                    updateMeasurementPlan();

                    // Auto-Send Einstellungen
                    document.getElementById('autoSendToPumps').value = data.autoSendToPumps ? 'true' : 'false';
                    document.getElementById('pumpsIP').value = data.pumpsIP || '192.168.1.100';

                    // Settings für Live-KH-Berechnung speichern
                    currentSettings.sampleVolume = data.sampleVolume;
                    currentSettings.acidConcentration = data.acidConcentration;
                    currentSettings.acidCorrectionFactor = data.acidCorrectionFactor || 1.0;

                    // Letzte Messung laden für Korrekturfaktor-Berechnung
                    loadLastMeasurementForCorrection();
                });
        }

        function testStirrer(activate) {
            if (activate) {
                const speed = parseInt(document.getElementById('stirrerSpeed').value);
                const freq = parseInt(document.getElementById('stirrerPwmFreq').value);
                fetch('/api/stirrer/test', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({speed: speed, freq: freq})
                });
            } else {
                fetch('/api/stirrer/stop', {
                    method: 'POST'
                });
            }
        }

        function saveSettings() {
            // Validierung: Mess-Intervall
            let interval = parseInt(document.getElementById('measurementIntervalHours').value);
            if (interval !== 0 && interval < 1) {
                alert('⚠️ Warnung: Ungültiges Intervall!\n\nErlaubte Werte:\n• 0 = nur einmal pro Tag\n• 1-23 = Intervall in Stunden\n\nWert wird auf 1 Stunde gesetzt.');
                document.getElementById('measurementIntervalHours').value = 1;
                interval = 1;
            }

            const settings = {
                sampleVolume: parseFloat(document.getElementById('sampleVolume').value),
                rinseVolume: parseFloat(document.getElementById('rinseVolume').value),
                overpumpPercent: parseFloat(document.getElementById('overpumpPercent').value),
                acidConcentration: parseFloat(document.getElementById('acidConcentration').value),
                acidCorrectionFactor: parseFloat(document.getElementById('acidCorrectionFactor').value),
                targetPH: parseFloat(document.getElementById('targetPH').value),
                doseVolumeMin: parseFloat(document.getElementById('doseVolumeMin').value),
                doseVolumeMax: parseFloat(document.getElementById('doseVolumeMax').value),
                stabilityTimeMin: parseFloat(document.getElementById('stabilityTimeMin').value),
                stabilityTimeMax: parseFloat(document.getElementById('stabilityTimeMax').value),
                toleranceMin: parseFloat(document.getElementById('toleranceMin').value),
                toleranceMax: parseFloat(document.getElementById('toleranceMax').value),
                interpolationExponent: parseFloat(document.getElementById('interpolationExponent').value),
                acidPrimeVolume: parseFloat(document.getElementById('acidPrimeVolume').value),
                submersionVolume: parseFloat(document.getElementById('submersionVolume').value),
                phAcclimatizationTime: Math.round(parseFloat(document.getElementById('phAcclimatizationTime').value) * 1000),
                maxStabilityTimeout: parseInt(document.getElementById('maxStabilityTimeout').value),
                maxContainerVolume: parseFloat(document.getElementById('maxContainerVolume').value),

                // Rührer-Geschwindigkeit
                stirrerSpeed: parseInt(document.getElementById('stirrerSpeed').value),
                stirrerPwmFreq: parseInt(document.getElementById('stirrerPwmFreq').value),

                // Auto-Messung Einstellungen (RTC-basiert)
                autoMeasureEnabled: parseInt(document.getElementById('autoMeasureEnabled').value),
                firstMeasurementHour: parseInt(document.getElementById('firstMeasurementHour').value),
                firstMeasurementMinute: parseInt(document.getElementById('firstMeasurementMinute').value),
                measurementIntervalHours: interval,
                measurementRepeatDays: parseInt(document.getElementById('measurementRepeatDays').value),

                // Auto-Send Einstellungen
                autoSendToPumps: document.getElementById('autoSendToPumps').value === 'true',
                pumpsIP: document.getElementById('pumpsIP').value
            };

            return fetch('/api/settings', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(settings)
            }).then(() => {
                updateMeasurementPlan();  // Aktualisiere Mess-Plan
                return true;  // Erfolgreich
            });
        }

        // Wrapper für manuelles Speichern mit Alert
        function saveSettingsWithAlert() {
            saveSettings().then(() => {
                alert('✓ Einstellungen gespeichert');
            }).catch(err => {
                alert('✗ Fehler beim Speichern: ' + err);
            });
        }
        
        // ============== Säure-Korrekturfaktor Funktionen ==============
        function updateEffectiveAcidConcentration() {
            const nominalEl = document.getElementById('acidConcentration');
            const factorEl = document.getElementById('acidCorrectionFactor');
            const effectiveEl = document.getElementById('effectiveAcidConcentration');
            if (!nominalEl || !factorEl || !effectiveEl) return;  // Elemente nicht vorhanden

            const nominal = parseFloat(nominalEl.value) || 0.1;
            const factor = parseFloat(factorEl.value) || 1.0;
            const effective = nominal * factor;
            effectiveEl.textContent = effective.toFixed(4);
            updateCalculatedCorrectionFactor();
        }

        function resetAcidCorrectionFactor() {
            document.getElementById('acidCorrectionFactor').value = '1.000';
            updateEffectiveAcidConcentration();
        }

        function loadLastMeasurementForCorrection() {
            const el = document.getElementById('lastMeasuredKH');
            if (!el) return;  // Element nicht vorhanden

            fetch('/api/measurements')
                .then(r => r.json())
                .then(data => {
                    if (data.measurements && data.measurements.length > 0) {
                        const lastMeasurement = data.measurements[data.measurements.length - 1];
                        // khValue ist jetzt immer der ROHWERT (ohne Korrekturfaktor)
                        const rawKH = lastMeasurement.khValue || 0;
                        // Für Anzeige: Korrigierten Wert berechnen (wenn Faktor gespeichert)
                        const factor = lastMeasurement.correctionFactor || 1.0;
                        const correctedKH = rawKH * factor;

                        // Zeige korrigierten Wert + Rohwert in Klammern
                        el.innerHTML = correctedKH.toFixed(2) + ' dKH <small style="color: #888;">(Roh: ' + rawKH.toFixed(2) + ')</small>';
                        el.dataset.rawValue = rawKH;  // Rohwert für Berechnung
                        el.dataset.factor = factor;   // Alter Faktor zum Vergleich
                        updateCalculatedCorrectionFactor();
                    } else {
                        el.textContent = '-- dKH';
                        el.dataset.rawValue = '0';
                        el.dataset.factor = '1.0';
                    }
                });
        }

        function updateCalculatedCorrectionFactor() {
            const lastKHEl = document.getElementById('lastMeasuredKH');
            const targetKHEl = document.getElementById('targetKHValue');
            const resultEl = document.getElementById('calculatedCorrectionFactor');
            if (!lastKHEl || !targetKHEl || !resultEl) return;  // Elemente nicht vorhanden

            // Mit Rohwert ist die Berechnung einfach: Neuer Faktor = Soll / Rohwert
            const rawKH = parseFloat(lastKHEl.dataset.rawValue) || 0;
            const targetKH = parseFloat(targetKHEl.value) || 0;

            if (rawKH > 0 && targetKH > 0) {
                // Einfache Berechnung: Neuer Faktor = Soll-Wert / Rohwert
                const newFactor = targetKH / rawKH;
                resultEl.textContent = newFactor.toFixed(3);
            } else {
                resultEl.textContent = '--';
            }
        }

        function applyCalculatedCorrectionFactor() {
            const rawKH = parseFloat(document.getElementById('lastMeasuredKH').dataset.rawValue) || 0;
            const targetKH = parseFloat(document.getElementById('targetKHValue').value) || 0;

            if (rawKH <= 0) {
                alert('⚠️ Keine letzte Messung vorhanden!');
                return;
            }
            if (targetKH < 0.5 || targetKH > 20) {
                alert('⚠️ Soll-Wert muss zwischen 0.5 und 20 dKH liegen!');
                return;
            }

            // Mit Rohwert: Neuer Faktor = Soll / Rohwert
            const newFactor = targetKH / rawKH;

            // Warnung bei extremen Faktoren
            if (newFactor < 0.8 || newFactor > 1.2) {
                if (!confirm('⚠️ Warnung: Der berechnete Faktor (' + newFactor.toFixed(3) + ') weicht stark von 1.0 ab!\n\n' +
                    'Dies könnte auf ein Problem hindeuten:\n' +
                    '• Pumpen-Kalibrierung überprüfen\n' +
                    '• Säure möglicherweise verdorben\n' +
                    '• Referenzmessung wiederholen\n\n' +
                    'Trotzdem übernehmen?')) {
                    return;
                }
            }

            // Faktor auf Bereich begrenzen
            const clampedFactor = Math.max(0.5, Math.min(1.5, newFactor));
            if (clampedFactor !== newFactor) {
                alert('ℹ️ Faktor wurde auf den erlaubten Bereich (0.5 - 1.5) begrenzt.');
            }

            document.getElementById('acidCorrectionFactor').value = clampedFactor.toFixed(3);
            updateEffectiveAcidConcentration();

            // Einstellungen direkt speichern
            saveSettings().then(() => {
                alert('✓ Korrekturfaktor ' + clampedFactor.toFixed(3) + ' übernommen und gespeichert.');
            }).catch(err => {
                alert('✓ Korrekturfaktor übernommen, aber Speichern fehlgeschlagen: ' + err);
            });
        }

        function resetSettings() {
            if (!confirm('⚠️ Wirklich ALLE Einstellungen zurücksetzen?\n\nDies kann nicht rückgängig gemacht werden!')) {
                return;
            }

            fetch('/api/reset/settings', { method: 'POST' })
                .then(r => r.text())
                .then(() => {
                    alert('✓ Einstellungen zurückgesetzt. Seite wird neu geladen...');
                    location.reload();
                })
                .catch(err => {
                    alert('✗ Fehler beim Zurücksetzen: ' + err);
                });
        }

        function resetMeasurements() {
            if (!confirm('⚠️ Wirklich ALLE Messungen löschen?\n\nDies kann nicht rückgängig gemacht werden!')) {
                return;
            }

            fetch('/api/reset/measurements', { method: 'POST' })
                .then(r => r.text())
                .then(() => {
                    alert('✓ Alle Messungen gelöscht.');
                    loadMeasurements(); // Refresh table
                })
                .catch(err => {
                    alert('✗ Fehler beim Löschen: ' + err);
                });
        }

        function loadMeasurements() {
            // Cache-Buster: Timestamp anhängen um Browser-Cache zu umgehen
            fetch('/api/measurements?t=' + Date.now())
                .then(r => r.json())
                .then(data => {
                    const tbody = document.getElementById('measurementsTableBody');
                    tbody.innerHTML = '';

                    if (!data.measurements || data.measurements.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="7" style="text-align: center;">Keine Messungen vorhanden</td></tr>';
                        return;
                    }

                    // Reverse für Anzeige (neueste zuerst), aber Original-Index behalten
                    const total = data.measurements.length;
                    data.measurements.slice().reverse().forEach((m, displayIdx) => {
                        const originalIdx = total - 1 - displayIdx;  // Original-Index in der JSON-Datei
                        // khValue = Rohwert, correctionFactor = Faktor zum Messzeitpunkt
                        const rawKH = m.khValue || 0;
                        const factor = m.correctionFactor || 1.0;  // Fallback für alte Messungen
                        const correctedKH = rawKH * factor;
                        // Zeige korrigierten Wert, Rohwert als Tooltip
                        const hasTimeout = m.stabilityTimeout || false;
                        const warningIcon = hasTimeout ? ' <span title="Stabilitäts-Timeout aufgetreten">⚠️</span>' : '';
                        const khDisplay = factor !== 1.0
                            ? `<strong title="Roh: ${rawKH.toFixed(2)} × ${factor.toFixed(3)}">${correctedKH.toFixed(2)}</strong>${warningIcon}`
                            : `<strong>${rawKH.toFixed(2)}</strong>${warningIcon}`;
                        const row = tbody.insertRow();
                        row.innerHTML = `
                            <td>${m.timestamp}</td>
                            <td>${khDisplay}</td>
                            <td>${m.acidUsed.toFixed(3)}</td>
                            <td>${m.initialPH.toFixed(3)}</td>
                            <td>${m.finalPH.toFixed(3)}</td>
                            <td><button onclick="deleteMeasurement(${originalIdx})" style="background: #e74c3c; color: white; border: none; border-radius: 4px; width: 28px; height: 28px; cursor: pointer; font-size: 14px;" title="Messung löschen">✕</button></td>
                        `;
                    });
                });
        }

        function deleteMeasurement(index) {
            if (!confirm('Diese Messung wirklich löschen?')) return;

            fetch('/api/measurements/delete?index=' + index, { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        loadMeasurements();
                    } else {
                        alert('Fehler: ' + (data.error || 'Unbekannt'));
                    }
                })
                .catch(e => alert('Fehler: ' + e));
        }
        
        function scanWiFi() {
            const list = document.getElementById('wifiList');
            list.innerHTML = '<li style="text-align: center;">Scanne...</li>';

            // Scan starten
            fetch('/api/wifi/scan/start')
                .then(() => {
                    // Nach 3 Sekunden Ergebnisse abrufen
                    setTimeout(pollScanResults, 3000);
                })
                .catch(e => {
                    list.innerHTML = '<li style="text-align: center; color: red;">Fehler beim Starten</li>';
                });
        }

        function pollScanResults() {
            const list = document.getElementById('wifiList');

            fetch('/api/wifi/scan')
                .then(r => r.json())
                .then(data => {
                    if (data.status === 'scanning') {
                        // Noch nicht fertig, nochmal versuchen
                        setTimeout(pollScanResults, 1000);
                        return;
                    }

                    list.innerHTML = '';

                    if (!data.networks || data.networks.length === 0) {
                        list.innerHTML = '<li style="text-align: center;">Keine Netzwerke gefunden</li>';
                        return;
                    }

                    data.networks.forEach(net => {
                        const li = document.createElement('li');
                        li.className = 'wifi-item';
                        li.innerHTML = `
                            <span>${net.ssid}</span>
                            <span>${net.rssi} dBm | ${net.encryption}</span>
                        `;
                        li.onclick = () => {
                            document.querySelectorAll('.wifi-item').forEach(i => i.classList.remove('selected'));
                            li.classList.add('selected');
                            selectedSSID = net.ssid;
                            document.getElementById('wifiSSID').value = net.ssid;
                        };
                        list.appendChild(li);
                    });
                })
                .catch(e => {
                    list.innerHTML = '<li style="text-align: center; color: red;">Fehler: ' + e + '</li>';
                });
        }
        
        function saveWiFi() {
            const ssid = document.getElementById('wifiSSID').value;
            const password = document.getElementById('wifiPassword').value;
            
            if (!ssid) {
                alert('Bitte SSID eingeben');
                return;
            }
            
            if (confirm(`Mit "${ssid}" verbinden? ESP wird neu starten.`)) {
                fetch('/api/wifi', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ssid, password})
                }).then(() => {
                    alert('WiFi-Konfiguration gespeichert. ESP startet neu...');
                });
            }
        }
        
        function calibratePH(target) {
            const key = target === 4 ? 'ph4' : 'ph7';

            // Buttons deaktivieren
            document.getElementById('btnCalPH4').disabled = true;
            document.getElementById('btnCalPH7').disabled = true;

            // Progress anzeigen
            document.getElementById('calibrationProgress').style.display = 'block';
            document.getElementById('calProgressText').textContent = `Kalibriere pH ${target}.0 - Warte auf stabile Messung...`;

            fetch('/api/calibrate', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({[key]: true})
            }).then(response => response.text())
              .then(text => {
                  console.log(`pH ${target}.0: ${text}`);
              });
        }

        function updateCalibrationStatus() {
            fetch('/api/calibrate/info')
                .then(r => r.json())
                .then(data => {
                    // pH 4.0 Status
                    const ph4StatusEl = document.getElementById('ph4Status');
                    if (data.ph4Calibrated) {
                        ph4StatusEl.innerHTML = `✓ ${data.ph4Voltage.toFixed(1)} mV<br><span style="font-size: 0.85em;">${data.ph4Timestamp}</span>`;
                        ph4StatusEl.style.color = '#28a745';
                    } else {
                        ph4StatusEl.innerHTML = 'Nicht kalibriert';
                        ph4StatusEl.style.color = '#888';
                    }

                    // pH 7.0 Status
                    const ph7StatusEl = document.getElementById('ph7Status');
                    if (data.ph7Calibrated) {
                        ph7StatusEl.innerHTML = `✓ ${data.ph7Voltage.toFixed(1)} mV<br><span style="font-size: 0.85em;">${data.ph7Timestamp}</span>`;
                        ph7StatusEl.style.color = '#28a745';
                    } else {
                        ph7StatusEl.innerHTML = 'Nicht kalibriert';
                        ph7StatusEl.style.color = '#888';
                    }
                })
                .catch(err => console.error('Fehler beim Laden der Kalibrierungsdaten:', err));
        }

        // ==================== pH KALIBRIERUNGSHISTORIE ====================
        let phCalHistoryData = null;

        function loadPHCalibrationHistory() {
            fetch('/api/ph/calibration-history')
                .then(r => r.json())
                .then(data => {
                    phCalHistoryData = data;
                    updateSensorEfficiencyDisplay(data.efficiency);
                    updateSensorInstallDate(data.sensorInstallDate);
                    updatePHCalibrationTable(data);
                    drawPHCalibrationChart(data);
                })
                .catch(err => console.error('Fehler beim Laden der pH-Kalibrierungshistorie:', err));
        }

        function updateSensorEfficiencyDisplay(efficiency) {
            const effEl = document.getElementById('sensorEfficiency');
            const statusEl = document.getElementById('sensorEfficiencyStatus');
            if (!effEl || !statusEl) return;

            if (efficiency < 0) {
                effEl.textContent = '-- %';
                effEl.style.color = '#888';
                statusEl.textContent = 'Noch keine vollständige Kalibrierung (pH 4 + pH 7)';
                return;
            }

            effEl.textContent = efficiency.toFixed(1) + ' %';

            if (efficiency >= 95 && efficiency <= 102) {
                effEl.style.color = '#27ae60';
                statusEl.textContent = 'Ausgezeichnet - Sonde ist wie neu!';
            } else if (efficiency >= 85 && efficiency < 95) {
                effEl.style.color = '#f39c12';
                statusEl.textContent = 'OK - Sonde altert, funktioniert aber noch gut';
            } else if (efficiency > 102) {
                effEl.style.color = '#f39c12';
                statusEl.textContent = 'Ungewöhnlich hoch - Kalibrierung prüfen';
            } else {
                effEl.style.color = '#e74c3c';
                statusEl.textContent = 'Kritisch - Zeit für eine neue Sonde!';
            }
        }

        function updateSensorInstallDate(dateStr) {
            const el = document.getElementById('sensorInstallDate');
            if (el) el.textContent = dateStr || '--';
        }

        function updatePHCalibrationTable(data) {
            const tbody = document.getElementById('phCalibrationTableBody');
            if (!tbody) return;

            // Alle Einträge zusammenführen und nach Datum sortieren
            let entries = [];
            if (data.calibrations_pH4) {
                data.calibrations_pH4.forEach((e, i) => {
                    entries.push({ ...e, type: 'pH 4', phType: 4, index: i });
                });
            }
            if (data.calibrations_pH7) {
                data.calibrations_pH7.forEach((e, i) => {
                    entries.push({ ...e, type: 'pH 7', phType: 7, index: i });
                });
            }

            // Nach Datum sortieren (neueste zuerst)
            entries.sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp));

            if (entries.length === 0) {
                tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; padding: 20px; color: #888;">Keine Kalibrierungen vorhanden</td></tr>';
                return;
            }

            tbody.innerHTML = entries.map(e => `
                <tr>
                    <td style="padding: 8px; border-bottom: 1px solid #dee2e6;">${e.timestamp}</td>
                    <td style="padding: 8px; border-bottom: 1px solid #dee2e6; color: ${e.phType === 4 ? '#3498db' : '#27ae60'}; font-weight: bold;">${e.type}</td>
                    <td style="padding: 8px; border-bottom: 1px solid #dee2e6; text-align: right;">${e.mV.toFixed(1)}</td>
                    <td style="padding: 8px; border-bottom: 1px solid #dee2e6; text-align: center;">
                        <button onclick="deletePHCalibrationEntry(${e.phType}, ${e.index})" style="background: #e74c3c; color: white; border: none; padding: 4px 8px; border-radius: 3px; cursor: pointer; font-size: 12px;">✕</button>
                    </td>
                </tr>
            `).join('');
        }

        function deletePHCalibrationEntry(phType, index) {
            if (!confirm('Diese Kalibrierung wirklich löschen?')) return;

            fetch('/api/ph/calibration-history/delete', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ phType: phType, index: index })
            })
            .then(() => loadPHCalibrationHistory())
            .catch(err => alert('Fehler beim Löschen: ' + err));
        }

        function resetPHCalibrationHistory() {
            if (!confirm('Wirklich ALLE Kalibrierungsdaten löschen?\n\nDies sollte nur bei einer neuen pH-Sonde gemacht werden!')) return;

            fetch('/api/ph/calibration-history/reset', { method: 'POST' })
                .then(() => {
                    alert('Kalibrierungshistorie zurückgesetzt.');
                    loadPHCalibrationHistory();
                })
                .catch(err => alert('Fehler: ' + err));
        }

        function drawPHCalibrationChart(data) {
            const canvas = document.getElementById('phCalibrationChart');
            if (!canvas) return;
            const ctx = canvas.getContext('2d');

            const width = canvas.width;
            const height = canvas.height;
            const padding = { top: 30, right: 70, bottom: 50, left: 60 };

            // Canvas leeren
            ctx.clearRect(0, 0, width, height);
            ctx.fillStyle = '#fafafa';
            ctx.fillRect(0, 0, width, height);

            const ph4Data = data.calibrations_pH4 || [];
            const ph7Data = data.calibrations_pH7 || [];

            if (ph4Data.length === 0 && ph7Data.length === 0) {
                ctx.fillStyle = '#888';
                ctx.font = '14px Arial';
                ctx.textAlign = 'center';
                ctx.fillText('Noch keine Kalibrierungsdaten vorhanden', width / 2, height / 2);
                return;
            }

            // Alle Timestamps und mV-Werte sammeln
            let allData = [];
            ph4Data.forEach(e => allData.push({ ...e, type: 4 }));
            ph7Data.forEach(e => allData.push({ ...e, type: 7 }));
            allData.sort((a, b) => new Date(a.timestamp) - new Date(b.timestamp));

            // Zeitbereich
            const times = allData.map(e => new Date(e.timestamp).getTime());
            const minTime = Math.min(...times);
            const maxTime = Math.max(...times);
            const timeRange = maxTime - minTime || 1;

            // mV Bereich (linke Y-Achse)
            const allMV = allData.map(e => e.mV);
            const minMV = Math.min(...allMV) - 50;
            const maxMV = Math.max(...allMV) + 50;
            const mvRange = maxMV - minMV || 1;

            // Steigung berechnen (rechte Y-Achse) - Paare innerhalb 2 Stunden
            let slopes = [];
            const twoHours = 2 * 60 * 60 * 1000;
            ph4Data.forEach(p4 => {
                const t4 = new Date(p4.timestamp).getTime();
                // Finde pH7 innerhalb von 2 Stunden
                let closest7 = null;
                let minDiff = Infinity;
                ph7Data.forEach(p7 => {
                    const t7 = new Date(p7.timestamp).getTime();
                    const diff = Math.abs(t7 - t4);
                    if (diff < twoHours && diff < minDiff) {
                        minDiff = diff;
                        closest7 = p7;
                    }
                });
                if (closest7) {
                    const slope = Math.abs(closest7.mV - p4.mV) / 3.0;  // mV pro pH (absolut)
                    const avgTime = (t4 + new Date(closest7.timestamp).getTime()) / 2;
                    slopes.push({ time: avgTime, slope: slope });
                }
            });

            // Referenz-Steigung aus API-Daten (= 100% bei erster Kalibrierung)
            const referenceSlope = data.referenceSlope || (slopes.length > 0 ? slopes[0].slope : 130);

            // Steigungsbereich
            let minSlope = referenceSlope - 30;
            let maxSlope = referenceSlope + 30;
            if (slopes.length > 0) {
                const slopeVals = slopes.map(s => s.slope);
                minSlope = Math.min(minSlope, Math.min(...slopeVals) - 10);
                maxSlope = Math.max(maxSlope, Math.max(...slopeVals) + 10);
            }
            const slopeRange = maxSlope - minSlope || 1;

            // Hilfsfunktionen
            const xScale = (t) => padding.left + ((t - minTime) / timeRange) * (width - padding.left - padding.right);
            const yScaleMV = (mv) => padding.top + ((maxMV - mv) / mvRange) * (height - padding.top - padding.bottom);
            const yScaleSlope = (s) => padding.top + ((maxSlope - s) / slopeRange) * (height - padding.top - padding.bottom);

            // Achsen zeichnen
            ctx.strokeStyle = '#ccc';
            ctx.lineWidth = 1;

            // Linke Y-Achse (mV)
            ctx.beginPath();
            ctx.moveTo(padding.left, padding.top);
            ctx.lineTo(padding.left, height - padding.bottom);
            ctx.stroke();

            // Rechte Y-Achse (Steigung)
            ctx.beginPath();
            ctx.moveTo(width - padding.right, padding.top);
            ctx.lineTo(width - padding.right, height - padding.bottom);
            ctx.stroke();

            // X-Achse
            ctx.beginPath();
            ctx.moveTo(padding.left, height - padding.bottom);
            ctx.lineTo(width - padding.right, height - padding.bottom);
            ctx.stroke();

            // Y-Achsen Beschriftung (mV - links)
            ctx.fillStyle = '#333';
            ctx.font = '11px Arial';
            ctx.textAlign = 'right';
            for (let i = 0; i <= 5; i++) {
                const mv = minMV + (mvRange * i / 5);
                const y = yScaleMV(mv);
                ctx.fillText(mv.toFixed(0), padding.left - 5, y + 4);
                ctx.strokeStyle = '#eee';
                ctx.beginPath();
                ctx.moveTo(padding.left, y);
                ctx.lineTo(width - padding.right, y);
                ctx.stroke();
            }
            ctx.save();
            ctx.translate(15, height / 2);
            ctx.rotate(-Math.PI / 2);
            ctx.textAlign = 'center';
            ctx.fillText('mV', 0, 0);
            ctx.restore();

            // Y-Achsen Beschriftung (Steigung - rechts)
            ctx.textAlign = 'left';
            ctx.fillStyle = '#e67e22';
            for (let i = 0; i <= 5; i++) {
                const s = minSlope + (slopeRange * i / 5);
                const y = yScaleSlope(s);
                ctx.fillText(s.toFixed(1), width - padding.right + 5, y + 4);
            }
            ctx.save();
            ctx.translate(width - 10, height / 2);
            ctx.rotate(Math.PI / 2);
            ctx.textAlign = 'center';
            ctx.fillText('mV/pH', 0, 0);
            ctx.restore();

            // Referenz-Steigung (gestrichelte Linie = 100% Effizienz)
            if (referenceSlope > 0) {
                ctx.strokeStyle = '#e67e22';
                ctx.setLineDash([5, 5]);
                ctx.lineWidth = 2;
                ctx.beginPath();
                const refY = yScaleSlope(referenceSlope);
                ctx.moveTo(padding.left, refY);
                ctx.lineTo(width - padding.right, refY);
                ctx.stroke();
                ctx.setLineDash([]);
            }

            // pH4 Kurve (blau)
            if (ph4Data.length > 0) {
                ctx.strokeStyle = '#3498db';
                ctx.fillStyle = '#3498db';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ph4Data.forEach((p, i) => {
                    const x = xScale(new Date(p.timestamp).getTime());
                    const y = yScaleMV(p.mV);
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                });
                ctx.stroke();
                // Punkte
                ph4Data.forEach(p => {
                    const x = xScale(new Date(p.timestamp).getTime());
                    const y = yScaleMV(p.mV);
                    ctx.beginPath();
                    ctx.arc(x, y, 4, 0, Math.PI * 2);
                    ctx.fill();
                });
            }

            // pH7 Kurve (grün)
            if (ph7Data.length > 0) {
                ctx.strokeStyle = '#27ae60';
                ctx.fillStyle = '#27ae60';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ph7Data.forEach((p, i) => {
                    const x = xScale(new Date(p.timestamp).getTime());
                    const y = yScaleMV(p.mV);
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                });
                ctx.stroke();
                // Punkte
                ph7Data.forEach(p => {
                    const x = xScale(new Date(p.timestamp).getTime());
                    const y = yScaleMV(p.mV);
                    ctx.beginPath();
                    ctx.arc(x, y, 4, 0, Math.PI * 2);
                    ctx.fill();
                });
            }

            // Steigungskurve (orange)
            if (slopes.length > 0) {
                ctx.strokeStyle = '#e67e22';
                ctx.fillStyle = '#e67e22';
                ctx.lineWidth = 2;
                ctx.beginPath();
                slopes.forEach((s, i) => {
                    const x = xScale(s.time);
                    const y = yScaleSlope(s.slope);
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                });
                ctx.stroke();
                // Punkte
                slopes.forEach(s => {
                    const x = xScale(s.time);
                    const y = yScaleSlope(s.slope);
                    ctx.beginPath();
                    ctx.arc(x, y, 5, 0, Math.PI * 2);
                    ctx.fill();
                });
            }

            // Legende
            ctx.font = 'bold 12px Arial';
            ctx.textAlign = 'left';
            ctx.fillStyle = '#3498db';
            ctx.fillText('● pH 4', padding.left + 10, 20);
            ctx.fillStyle = '#27ae60';
            ctx.fillText('● pH 7', padding.left + 70, 20);
            ctx.fillStyle = '#e67e22';
            ctx.fillText('● Steigung', padding.left + 130, 20);
            ctx.fillText('- - Referenz (100%)', padding.left + 210, 20);

            // X-Achse Beschriftung (Datum)
            ctx.fillStyle = '#333';
            ctx.font = '10px Arial';
            ctx.textAlign = 'center';
            const dateLabels = 5;
            for (let i = 0; i <= dateLabels; i++) {
                const t = minTime + (timeRange * i / dateLabels);
                const x = xScale(t);
                const date = new Date(t);
                const label = date.toLocaleDateString('de-DE', { day: '2-digit', month: '2-digit' });
                ctx.fillText(label, x, height - padding.bottom + 20);
            }
        }

        // ==================== TITRATION CHART ====================
        let chartData = {
            startTime: 0,
            phValues: [],
            acidVolumes: [],
            times: []
        };

        let canvas, ctx;
        let chartVisible = false;
        let currentSettings = {
            sampleVolume: 50.0,
            acidConcentration: 0.1,
            acidCorrectionFactor: 1.0
        };

        let lastStabilityReached = false;

        // Dosierprotokoll: Steps werden im Frontend gesammelt
        let collectedSteps = [];
        let lastKnownStepCount = 0;

        function initChart() {
            canvas = document.getElementById('titrationChart');
            ctx = canvas.getContext('2d');
            chartData = {
                startTime: Date.now(),
                phValues: [],
                acidVolumes: [],
                times: []
            };
            // Dosierprotokoll zurücksetzen
            collectedSteps = [];
            lastKnownStepCount = 0;
            document.getElementById('stepChartContainer').style.display = 'none';

            document.getElementById('chartCard').style.display = 'block';
            chartVisible = true;
            // Säurevolumen-Werte unter 0.001 ml ignorieren (Altdaten von vorheriger Messung)
            // Der ESP setzt acidUsed erst auf 0 wenn der State-Wechsel komplett ist

        }

        function hideChart() {
            document.getElementById('chartCard').style.display = 'none';
            document.getElementById('liveKH').style.display = 'none';
            document.getElementById('stepChartContainer').style.display = 'none';
            chartVisible = false;
        }

        function addChartDataPoint(ph, acidVolume) {
            if (!chartVisible) return;

            const time = (Date.now() - chartData.startTime) / 1000; // Sekunden

            // Alle Sekunden ein Datenpunkt (durch 1s Polling-Intervall)
            chartData.times.push(time);
            chartData.phValues.push(ph);
            chartData.acidVolumes.push(acidVolume);

            // KEINE Begrenzung - komplette Titration wird aufgezeichnet
            // Bei max 15 Min Titration = ~900 Punkte (Browser kann das problemlos)

            drawChart();
        }

        function drawChart() {
            if (!ctx || !canvas) return;

            const width = canvas.width;
            const height = canvas.height;
            const padding = 60;
            const plotWidth = width - 2 * padding;
            const plotHeight = height - 2 * padding;

            // Clear canvas
            ctx.fillStyle = 'white';
            ctx.fillRect(0, 0, width, height);

            if (chartData.times.length === 0) return;

            // Find ranges - DYNAMISCH für bessere Lesbarkeit
            const maxTime = Math.max(...chartData.times, 60);

            // pH-Achse: automatisch an Daten anpassen mit 10% Puffer
            const minPHRaw = Math.min(...chartData.phValues);
            const maxPHRaw = Math.max(...chartData.phValues);
            const phRange = maxPHRaw - minPHRaw;
            const phBuffer = Math.max(phRange * 0.1, 0.5); // Mindestens 0.5 pH Puffer
            // Datenpunkte bleiben an korrekter Position (nicht gerundet)
            const minPH = Math.max(minPHRaw - phBuffer, 0);
            const maxPH = maxPHRaw + phBuffer;

            // Säure-Achse: automatisch an Daten anpassen mit 10% Puffer
            const maxAcidRaw = Math.max(...chartData.acidVolumes);
            const maxAcid = Math.max(maxAcidRaw * 1.1, 0.1); // Mindestens 0.1 ml anzeigen

            // Draw axes
            ctx.strokeStyle = '#2c3e50';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.moveTo(padding, padding);
            ctx.lineTo(padding, height - padding);
            ctx.lineTo(width - padding, height - padding);
            ctx.stroke();

            // Draw grid
            ctx.strokeStyle = '#ecf0f1';
            ctx.lineWidth = 1;
            for (let i = 0; i <= 10; i++) {
                const y = padding + (plotHeight * i / 10);
                ctx.beginPath();
                ctx.moveTo(padding, y);
                ctx.lineTo(width - padding, y);
                ctx.stroke();
            }

            // Y-axis labels (pH - left) - nur ganze Zahlen, keine Duplikate
            ctx.fillStyle = '#3498db';
            ctx.font = '12px Arial';
            ctx.textAlign = 'right';

            // Berechne welche ganzzahligen pH-Werte im Bereich liegen
            const minPHLabel = Math.ceil(minPH);   // Aufrunden auf nächste ganze Zahl
            const maxPHLabel = Math.floor(maxPH);  // Abrunden auf nächste ganze Zahl

            // Zeichne nur die ganzzahligen pH-Werte im Bereich
            for (let ph = maxPHLabel; ph >= minPHLabel; ph--) {
                // Berechne Y-Position für diesen pH-Wert
                const normalizedPos = (maxPH - ph) / (maxPH - minPH);
                const y = padding + (plotHeight * normalizedPos);
                ctx.fillText(ph, padding - 10, y + 4);
            }
            ctx.fillText('pH', padding - 10, padding - 10);

            // Y-axis labels (Acid - right)
            ctx.fillStyle = '#e74c3c';
            ctx.textAlign = 'left';
            for (let i = 0; i <= 5; i++) {
                const acid = maxAcid * (1 - i / 5);  // Von oben nach unten (maxAcid → 0)
                const y = padding + (plotHeight * i / 5);
                ctx.fillText(acid.toFixed(2) + ' ml', width - padding + 10, y + 4);
            }
            ctx.fillText('Säure', width - padding + 10, padding - 10);

            // X-axis labels (Time)
            ctx.fillStyle = '#2c3e50';
            ctx.textAlign = 'center';
            for (let i = 0; i <= 5; i++) {
                const time = maxTime * i / 5;
                const x = padding + (plotWidth * i / 5);
                ctx.fillText(Math.round(time) + 's', x, height - padding + 20);
            }

            // Draw pH line (glatte Linie ohne Punkte)
            ctx.strokeStyle = '#3498db';
            ctx.lineWidth = 3;
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';
            ctx.beginPath();
            for (let i = 0; i < chartData.times.length; i++) {
                const x = padding + (chartData.times[i] / maxTime) * plotWidth;
                const y = padding + plotHeight - ((chartData.phValues[i] - minPH) / (maxPH - minPH)) * plotHeight;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();

            // Draw acid volume line (glatte Linie ohne Punkte)
            ctx.strokeStyle = '#e74c3c';
            ctx.lineWidth = 3;
            ctx.lineCap = 'round';
            ctx.lineJoin = 'round';
            ctx.beginPath();
            for (let i = 0; i < chartData.times.length; i++) {
                const x = padding + (chartData.times[i] / maxTime) * plotWidth;
                const y = padding + plotHeight - (chartData.acidVolumes[i] / maxAcid) * plotHeight;
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            ctx.stroke();

            // Legend
            ctx.font = 'bold 14px Arial';
            ctx.fillStyle = '#3498db';
            ctx.fillText('━ pH-Wert', padding + 20, 30);
            ctx.fillStyle = '#e74c3c';
            ctx.fillText('━ Säurevolumen', padding + 130, 30);
        }

        // ==================== DOSIERPROTOKOLL-GRAPH ====================
        function drawStepChart(steps) {
            const canvas = document.getElementById('stepChart');
            if (!canvas) return;
            const ctx2 = canvas.getContext('2d');

            const width = canvas.width;
            const height = canvas.height;
            const padding = 60;
            const plotWidth = width - 2 * padding;
            const plotHeight = height - 2 * padding;

            // Clear
            ctx2.fillStyle = 'white';
            ctx2.fillRect(0, 0, width, height);

            if (steps.length < 2) {
                ctx2.fillStyle = '#999';
                ctx2.font = '14px Arial';
                ctx2.textAlign = 'center';
                ctx2.fillText('Warte auf Daten (mind. 2 Schritte)...', width / 2, height / 2);
                return;
            }

            // Datenreihen berechnen
            const doses = steps.map(s => s.dose);
            const stabTimes = steps.map(s => s.stabT);

            // Achsenbereiche (eigene Y-Achse pro Reihe)
            const maxDose = Math.max(...doses) * 1.1 || 0.1;
            const maxStabT = Math.max(...stabTimes) * 1.1 || 1;
            const maxStep = steps.length;

            // Achsen zeichnen
            ctx2.strokeStyle = '#2c3e50';
            ctx2.lineWidth = 2;
            ctx2.beginPath();
            ctx2.moveTo(padding, padding);
            ctx2.lineTo(padding, height - padding);
            ctx2.lineTo(width - padding, height - padding);
            ctx2.stroke();

            // Gitterlinien
            ctx2.strokeStyle = '#ecf0f1';
            ctx2.lineWidth = 1;
            for (let i = 0; i <= 5; i++) {
                const y = padding + (plotHeight * i / 5);
                ctx2.beginPath();
                ctx2.moveTo(padding, y);
                ctx2.lineTo(width - padding, y);
                ctx2.stroke();
            }

            // X-Achse: Schrittnummern
            ctx2.fillStyle = '#2c3e50';
            ctx2.font = '12px Arial';
            ctx2.textAlign = 'center';
            const xStep = Math.max(1, Math.floor(maxStep / 10));
            for (let i = 0; i < maxStep; i += xStep) {
                const x = padding + ((i + 0.5) / maxStep) * plotWidth;
                ctx2.fillText(i + 1, x, height - padding + 18);
            }
            ctx2.fillText('Schritt', width / 2, height - padding + 35);

            // Y-Achsen-Labels: links Dosis, rechts Auswertung
            ctx2.font = '11px Arial';

            // Links: Dosis (ml) - rot
            ctx2.fillStyle = '#e74c3c';
            ctx2.textAlign = 'right';
            for (let i = 0; i <= 4; i++) {
                const val = maxDose * (1 - i / 4);
                const y = padding + (plotHeight * i / 4);
                ctx2.fillText(val.toFixed(3), padding - 8, y + 4);
            }
            ctx2.fillText('ml', padding - 8, padding - 10);

            // Rechts: Auswertungszeitraum (s) - grün
            ctx2.fillStyle = '#2ecc71';
            ctx2.textAlign = 'left';
            for (let i = 0; i <= 4; i++) {
                const val = maxStabT * (1 - i / 4);
                const y = padding + (plotHeight * i / 4);
                ctx2.fillText(val.toFixed(0) + 's', width - padding + 8, y + 4);
            }
            ctx2.fillText('Ausw.', width - padding + 8, padding - 10);

            // Hilfsfunktion: Linie mit Punkten zeichnen
            function drawLine(data, maxVal, color, drawDots) {
                ctx2.strokeStyle = color;
                ctx2.lineWidth = 2.5;
                ctx2.lineCap = 'round';
                ctx2.lineJoin = 'round';
                ctx2.beginPath();
                for (let i = 0; i < data.length; i++) {
                    const x = padding + ((i + 0.5) / maxStep) * plotWidth;
                    const y = padding + plotHeight - (data[i] / maxVal) * plotHeight;
                    if (i === 0) ctx2.moveTo(x, y);
                    else ctx2.lineTo(x, y);
                }
                ctx2.stroke();

                // Datenpunkte als kleine Kreise
                if (drawDots) {
                    ctx2.fillStyle = color;
                    for (let i = 0; i < data.length; i++) {
                        const x = padding + ((i + 0.5) / maxStep) * plotWidth;
                        const y = padding + plotHeight - (data[i] / maxVal) * plotHeight;
                        ctx2.beginPath();
                        ctx2.arc(x, y, 3, 0, Math.PI * 2);
                        ctx2.fill();
                    }
                }
            }

            // 2 Kurven zeichnen
            drawLine(doses, maxDose, '#e74c3c', true);      // Dosis - rot
            drawLine(stabTimes, maxStabT, '#2ecc71', true);  // Auswertung - grün

            // Legende mit aktuellen Werten des letzten Schritts
            const last = steps.length - 1;
            ctx2.font = 'bold 12px Arial';
            ctx2.textAlign = 'left';
            ctx2.fillStyle = '#e74c3c';
            ctx2.fillText('● Dosis: ' + doses[last].toFixed(4) + ' ml', padding + 10, 20);
            ctx2.fillStyle = '#2ecc71';
            ctx2.fillText('● Ausw.: ' + stabTimes[last].toFixed(0) + ' s', padding + 200, 20);
        }

        // Initialisierung
        function initializePage() {
            // Event Listener für Säure-Korrekturfaktor
            const acidConcEl = document.getElementById('acidConcentration');
            const acidCorrEl = document.getElementById('acidCorrectionFactor');
            const targetKHEl = document.getElementById('targetKHValue');
            if (acidConcEl) acidConcEl.addEventListener('input', updateEffectiveAcidConcentration);
            if (acidCorrEl) {
                acidCorrEl.addEventListener('input', updateEffectiveAcidConcentration);
                acidCorrEl.addEventListener('input', updateCalculatedCorrectionFactor);  // Auch berechneten Faktor aktualisieren
            }
            if (targetKHEl) targetKHEl.addEventListener('input', updateCalculatedCorrectionFactor);

            // pH-Kalibrierungsstatus initial laden
            updateCalibrationStatus();

            // Settings für Live-KH-Berechnung initial laden
            fetch('/api/settings')
                .then(r => r.json())
                .then(data => {
                    currentSettings.sampleVolume = data.sampleVolume;
                    currentSettings.acidConcentration = data.acidConcentration;
                    currentSettings.acidCorrectionFactor = data.acidCorrectionFactor || 1.0;
                });
        }

        // ============== Behälter-Funktionen ==============
        function loadContainerLevels() {
            fetch('/api/containers')
                .then(r => r.json())
                .then(data => {
                    // Säurebehälter
                    const acidPercent = (data.acidContainerLevel / data.acidContainerMax) * 100;
                    document.getElementById('acidCurrentLevel').textContent = data.acidContainerLevel.toFixed(1);
                    document.getElementById('acidMaxVolume').value = data.acidContainerMax;
                    document.getElementById('acidContainerPercent').textContent = acidPercent.toFixed(0) + '%';

                    const acidBar = document.getElementById('acidContainerBar');
                    acidBar.style.width = acidPercent + '%';
                    // Farbe: Grün wenn voll, Gelb wenn <30%, Rot wenn <10%
                    if (acidPercent < 10) {
                        acidBar.style.background = 'linear-gradient(90deg, #e74c3c, #c0392b)';
                    } else if (acidPercent < 30) {
                        acidBar.style.background = 'linear-gradient(90deg, #f39c12, #e67e22)';
                    } else {
                        acidBar.style.background = 'linear-gradient(90deg, #2ecc71, #27ae60)';
                    }

                    // Abwasserbehälter
                    const wastePercent = (data.wasteContainerLevel / data.wasteContainerMax) * 100;
                    document.getElementById('wasteCurrentLevel').textContent = data.wasteContainerLevel.toFixed(1);
                    document.getElementById('wasteMaxVolume').value = data.wasteContainerMax;
                    document.getElementById('wasteContainerPercent').textContent = wastePercent.toFixed(0) + '%';

                    const wasteBar = document.getElementById('wasteContainerBar');
                    wasteBar.style.width = wastePercent + '%';
                    // Farbe: Grün wenn leer, Gelb wenn >70%, Rot wenn >90%
                    if (wastePercent > 90) {
                        wasteBar.style.background = 'linear-gradient(90deg, #e74c3c, #c0392b)';
                    } else if (wastePercent > 70) {
                        wasteBar.style.background = 'linear-gradient(90deg, #f39c12, #e67e22)';
                    } else {
                        wasteBar.style.background = 'linear-gradient(90deg, #2ecc71, #27ae60)';
                    }

                    // Aquarium
                    document.getElementById('aquariumTotalUsed').textContent = data.aquariumTotalUsed.toFixed(1);
                    document.getElementById('aquariumDisplayText').textContent = data.aquariumTotalUsed.toFixed(1) + ' ml';

                    // Timestamps anzeigen
                    document.getElementById('acidTimestamp').textContent = data.acidLastRefill
                        ? 'Zuletzt nachgefüllt: ' + data.acidLastRefill
                        : 'Zuletzt nachgefüllt: -';
                    document.getElementById('wasteTimestamp').textContent = data.wasteLastEmpty
                        ? 'Zuletzt entleert: ' + data.wasteLastEmpty
                        : 'Zuletzt entleert: -';
                    document.getElementById('aquariumTimestamp').textContent = data.aquariumLastReset
                        ? 'Zuletzt zurückgesetzt: ' + data.aquariumLastReset
                        : 'Zuletzt zurückgesetzt: -';

                    // ============== Prognosen anzeigen ==============
                    const autoMode = data.autoMeasureEnabled;

                    // Säure-Prognose
                    document.getElementById('acidAvgUsage').textContent = data.acidAvgPerMeasurement.toFixed(2);
                    document.getElementById('acidMeasurementsLeft').textContent = data.acidMeasurementsLeft;
                    if (data.acidDaysLeft >= 0) {
                        document.getElementById('acidDaysLeft').innerHTML = '<strong>~' + Math.round(data.acidDaysLeft) + ' Tage</strong>';
                    } else {
                        document.getElementById('acidDaysLeft').textContent = 'Manueller Betrieb';
                    }

                    // Abwasser-Prognose
                    document.getElementById('wastePerMeasurement').textContent = data.wastePerMeasurement.toFixed(1);
                    document.getElementById('wasteMeasurementsUntilFull').textContent = data.wasteMeasurementsUntilFull;
                    if (data.wasteDaysUntilFull >= 0) {
                        document.getElementById('wasteDaysUntilFull').innerHTML = '<strong>~' + Math.round(data.wasteDaysUntilFull) + ' Tage</strong>';
                    } else {
                        document.getElementById('wasteDaysUntilFull').textContent = 'Manueller Betrieb';
                    }

                    // Aquarium-Prognose
                    document.getElementById('aquariumPerMeasurement').textContent = data.aquariumPerMeasurement.toFixed(1);
                    if (data.aquariumPerDay > 0) {
                        document.getElementById('aquariumPerDay').innerHTML = '<strong>' + data.aquariumPerDay.toFixed(1) + ' ml</strong>';
                        document.getElementById('aquariumPerWeek').innerHTML = '<strong>' + data.aquariumPerWeek.toFixed(0) + ' ml</strong>';
                    } else {
                        document.getElementById('aquariumPerDay').textContent = 'Manueller Betrieb';
                        document.getElementById('aquariumPerWeek').textContent = '-';
                    }

                    // Für Refill-Dialog merken
                    document.getElementById('acidRefillMax').textContent = data.acidContainerMax;
                    document.getElementById('acidRefillLevel').max = data.acidContainerMax;
                    document.getElementById('acidRefillLevel').value = data.acidContainerMax;
                });
        }

        function showAcidRefillDialog() {
            document.getElementById('acidRefillModal').style.display = 'flex';
        }

        function closeAcidRefillDialog() {
            document.getElementById('acidRefillModal').style.display = 'none';
        }

        function confirmAcidRefill() {
            const newLevel = parseFloat(document.getElementById('acidRefillLevel').value);
            const maxLevel = parseFloat(document.getElementById('acidRefillMax').textContent);

            if (isNaN(newLevel) || newLevel < 0) {
                alert('Bitte gültigen Wert eingeben');
                return;
            }
            if (newLevel > maxLevel) {
                alert('Wert darf nicht größer als Maximum (' + maxLevel + ' ml) sein');
                return;
            }

            fetch('/api/containers/acid/refill', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({level: newLevel})
            }).then(r => {
                if (r.ok) {
                    closeAcidRefillDialog();
                    loadContainerLevels();
                } else {
                    alert('Fehler beim Speichern');
                }
            });
        }

        function emptyWasteContainer() {
            if (!confirm('Abwasserbehälter als entleert markieren?')) return;

            fetch('/api/containers/waste/empty', {
                method: 'POST'
            }).then(r => {
                if (r.ok) {
                    loadContainerLevels();
                } else {
                    alert('Fehler beim Speichern');
                }
            });
        }

        function resetAquariumCounter() {
            if (!confirm('Aquarium-Zähler wirklich zurücksetzen?')) return;

            fetch('/api/containers/aquarium/reset', {
                method: 'POST'
            }).then(r => {
                if (r.ok) {
                    loadContainerLevels();
                } else {
                    alert('Fehler beim Speichern');
                }
            });
        }

        function saveContainerSettings() {
            const acidMax = parseFloat(document.getElementById('acidMaxVolume').value);
            const wasteMax = parseFloat(document.getElementById('wasteMaxVolume').value);

            if (isNaN(acidMax) || acidMax < 100 || isNaN(wasteMax) || wasteMax < 100) {
                alert('Max-Volumen muss mindestens 100 ml sein');
                return;
            }

            fetch('/api/containers/settings', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    acidContainerMax: acidMax,
                    wasteContainerMax: wasteMax
                })
            }).then(r => {
                if (r.ok) {
                    alert('✓ Max-Volumen gespeichert');
                    loadContainerLevels();
                } else {
                    alert('Fehler beim Speichern');
                }
            });
        }

        // Event-Listener für Auto-Messung Felder (live Plan-Update)
        // Mit Null-Checks um Script-Abbruch zu verhindern
        const autoMeasureEl = document.getElementById('autoMeasureEnabled');
        const firstHourEl = document.getElementById('firstMeasurementHour');
        const firstMinEl = document.getElementById('firstMeasurementMinute');
        const intervalEl = document.getElementById('measurementIntervalHours');
        const repeatEl = document.getElementById('measurementRepeatDays');
        if (autoMeasureEl) autoMeasureEl.addEventListener('change', updateMeasurementPlan);
        if (firstHourEl) firstHourEl.addEventListener('change', updateMeasurementPlan);
        if (firstMinEl) firstMinEl.addEventListener('change', updateMeasurementPlan);
        if (intervalEl) intervalEl.addEventListener('change', updateMeasurementPlan);
        if (repeatEl) repeatEl.addEventListener('change', updateMeasurementPlan);

        // Auto-Update
        initializePage();
        setInterval(updateStatus, 1000);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";
