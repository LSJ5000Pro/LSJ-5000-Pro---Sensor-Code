#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "Adafruit_OPT4048.h"

// --- Network Setup ---
const char* ssid = "lsj5000 pro";
const char* password = "password123";

Adafruit_OPT4048 sensor;
WebServer server(80);

// --- Hardware Calibration ---
const float CALIBRATION_MULTIPLIER = 1.10; 

// --- History Logging Variables ---
#define MAX_LOGS 24
float luxHistory[MAX_LOGS];
float cctHistory[MAX_LOGS];  
int rHistory[MAX_LOGS];
int gHistory[MAX_LOGS];
int bHistory[MAX_LOGS];     
unsigned long timeHistory[MAX_LOGS];
int logCount = 0;
int logIndex = 0;

unsigned long lastLogTime = 0;
const unsigned long logInterval = 15000; // 60,000 ms = 1 Minute Log Rate

// Helper: Format milliseconds into HH:MM:SS
String formatTime(unsigned long ms) {
  unsigned long totalSeconds = ms / 1000;
  int hours = totalSeconds / 3600;
  int mins = (totalSeconds % 3600) / 60;
  int secs = totalSeconds % 60;
  char buf[10];
  sprintf(buf, "%02d:%02d:%02d", hours, mins, secs);
  return String(buf);
}

// Environmental Status Functions
String getLuxStatus(float lx) {
  if (lx < 0.5) return "L1: Baseline";
  if (lx <= 5.0) return "L2: Elevated";
  return "L3: High Intensity";
}

String getKelvinStatus(float k) {
  if (k < 100) return "No Light";
  if (k < 2700) return "L1: Amber";
  if (k <= 3000) return "L2: Transitional";
  return "L3: Blue-Rich";
}

// Math Matrix: Convert CIE xy to Screen RGB
void calculateRGB(double x, double y, double lux, int &r, int &g, int &b) {
  if (lux < 0.01 || y <= 0) {
    r = 0; g = 0; b = 0; return; 
  }
  double X = x / y;
  double Y = 1.0;
  double Z = (1.0 - x - y) / y;

  double R =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
  double G = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
  double B =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;

  if (R < 0) R = 0;
  if (G < 0) G = 0;
  if (B < 0) B = 0;

  double maxVal = R;
  if (G > maxVal) maxVal = G;
  if (B > maxVal) maxVal = B;
  
  if (maxVal > 0) {
    R = (R / maxVal) * 255.0;
    G = (G / maxVal) * 255.0;
    B = (B / maxVal) * 255.0;
  }
  r = (int)R; g = (int)G; b = (int)B;
}

// CSS Style Block (Custom Hex Palette)
String getCSS() {
  String css = "<style>";
  css += ":root { --bg: #F0EFF4; --card: #FFFFFF; --text: #5C2751; --accent: #08A045; --border: #C3B299; }";
  css += "body { font-family: system-ui, -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 2rem 1rem; margin: 0; line-height: 1.5; }";
  css += ".container { max-width: 1000px; margin: 0 auto; }";
  css += "h2 { font-size: 1.5rem; font-weight: 600; margin-bottom: 1.5rem; color: var(--text); }";
  
  // Dashboard Grid System
  css += ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 1.5rem; margin-bottom: 2rem; }";
  
  // Card Styling
  css += ".card { background: var(--card); border: 1px solid var(--border); border-radius: 0.5rem; padding: 1.5rem; box-shadow: 0 4px 6px rgba(92, 39, 81, 0.05); transition: transform 0.2s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.2s; }";
  css += ".card:hover { transform: translateY(-4px); box-shadow: 0 10px 15px -3px rgba(92, 39, 81, 0.15); }";
  
  // Card Typography
  css += ".card-header { display: flex; justify-content: space-between; align-items: center; color: var(--text); font-size: 0.875rem; font-weight: 600; margin-bottom: 0.75rem; opacity: 0.85; }";
  css += ".card-value { font-size: 2rem; font-weight: 700; color: var(--accent); margin-bottom: 0.25rem; }";
  css += ".card-status { font-size: 0.75rem; font-weight: 600; padding: 0.25rem 0.5rem; background: var(--border); color: var(--text); border-radius: 9999px; display: inline-block; }";
  
  // Tables & Buttons
  css += "table { width: 100%; border-collapse: collapse; margin-top: 1rem; font-size: 0.875rem; }";
  css += "th, td { border-bottom: 1px solid var(--border); padding: 0.75rem; text-align: left; }";
  css += "th { background-color: var(--border); color: var(--text); font-weight: 600; border-top-left-radius: 4px; border-top-right-radius: 4px; }";
  css += ".color-box { display: inline-block; width: 24px; height: 24px; border-radius: 4px; border: 1px solid var(--border); vertical-align: middle; }";
  css += ".btn { display: inline-flex; align-items: center; justify-content: center; padding: 0.5rem 1rem; background-color: var(--card); color: var(--text); border: 2px solid var(--border); border-radius: 0.375rem; font-weight: 600; text-decoration: none; transition: all 0.2s; margin-right: 0.5rem; }";
  css += ".btn:hover { background-color: var(--border); }";
  css += ".btn-primary { background-color: var(--accent); color: white; border: 2px solid var(--accent); }";
  css += ".btn-primary:hover { opacity: 0.85; background-color: var(--accent); }";
  css += "</style>";
  return css;
}

// Lucide-style SVG Icons
const String iconLux = "<svg width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><circle cx='12' cy='12' r='4'/><path d='M12 2v2m0 16v2M4.93 4.93l1.41 1.41m11.32 11.32l1.41 1.41M2 12h2m16 0h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41'/></svg>";
const String iconTemp = "<svg width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z'/></svg>";
const String iconColor = "<svg width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><path d='M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z'/></svg>";

void handleRoot() {
  double CIEx, CIEy, rawLux;
  float currentLux = 0;
  float currentCCT = 0;
  int currentR = 0, currentG = 0, currentB = 0;
  bool sensorError = true;

  if (sensor.getCIE(&CIEx, &CIEy, &rawLux)) {
    sensorError = false;
    currentLux = (float)(rawLux * CALIBRATION_MULTIPLIER); 
    currentCCT = (float)sensor.calculateColorTemperature(CIEx, CIEy);
    calculateRGB(CIEx, CIEy, currentLux, currentR, currentG, currentB);
  }

  if (!sensorError && (millis() - lastLogTime >= logInterval || logCount == 0)) {
    luxHistory[logIndex] = currentLux;
    cctHistory[logIndex] = currentCCT; 
    rHistory[logIndex] = currentR;
    gHistory[logIndex] = currentG;
    bHistory[logIndex] = currentB;
    timeHistory[logIndex] = millis();
    logIndex = (logIndex + 1) % MAX_LOGS;
    if (logCount < MAX_LOGS) logCount++;
    lastLogTime = millis();
  }

  String html = "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='3'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += getCSS() + "</head><body>";
  html += "<div class='container'>";
  html += "<h2>LSJ 5000 Pro Dashboard</h2>";

  if (sensorError) {
    html += "<div class='card' style='border-color: red;'><div class='card-header' style='color:red;'>System Alert</div><div class='card-value'>Sensor Error</div><div class='card-status' style='background:red; color:white;'>Check I2C Wiring or Power</div></div>";
  } else {
    // Top Dashboard Grid
    html += "<div class='grid'>";
    
    // Card 1: Lux
    html += "<div class='card'><div class='card-header'><span>Light Intensity</span>" + iconLux + "</div>";
    html += "<div class='card-value'>" + String(currentLux, 3) + "</div>";
    html += "<div class='card-status'>" + getLuxStatus(currentLux) + "</div></div>";

    // Card 2: Kelvin
    html += "<div class='card'><div class='card-header'><span>Appearance</span>" + iconTemp + "</div>";
    html += "<div class='card-value'>" + String(currentCCT, 0) + "K</div>";
    html += "<div class='card-status'>" + getKelvinStatus(currentCCT) + "</div></div>";

    // Card 3: Color Visualizer
    html += "<div class='card'><div class='card-header'><span>Spectrum Visualizer</span>" + iconColor + "</div>";
    html += "<div style='width:100%; height:48px; border-radius:6px; background-color: rgb(" + String(currentR) + "," + String(currentG) + "," + String(currentB) + "); border: 1px solid var(--border); margin-bottom: 0.5rem;'></div>";
    html += "<div class='card-status'>RGB(" + String(currentR) + ", " + String(currentG) + ", " + String(currentB) + ")</div></div>";

    html += "</div>"; // End Grid

    // Action Bar
    html += "<div style='margin-bottom: 2rem;'>";
    html += "<a href='/guide' class='btn btn-primary'>View Reference Guide</a>";
    html += "<a href='/download' class='btn'>Download CSV Data</a>";
    html += "</div>";

    // Data Log Table
    html += "<div class='card'><div class='card-header' style='margin-bottom:0;'>Recent Activity Log</div>";
    html += "<table><tr><th>Time</th><th>Lux</th><th>Temp (K)</th><th>Color</th></tr>";
    
    for (int i = 0; i < logCount; i++) {
      int idx = (logIndex - 1 - i + MAX_LOGS) % MAX_LOGS;
      html += "<tr><td>" + formatTime(timeHistory[idx]) + "</td>";
      html += "<td><strong>" + String(luxHistory[idx], 3) + "</strong></td>";
      html += "<td>" + String(cctHistory[idx], 0) + "</td>";
      html += "<td><div class='color-box' style='background-color: rgb(" + String(rHistory[idx]) + "," + String(gHistory[idx]) + "," + String(bHistory[idx]) + ");'></div></td></tr>";
    }
    html += "</table></div>";
  }
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleGuide() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += getCSS() + "</head><body><div class='container'>";
  
  html += "<h2>LSJ 5000 Pro - Environmental Reference</h2>";
  
  html += "<div class='card' style='margin-bottom: 1.5rem;'>";
  html += "<div class='card-header' style='font-size:1rem; color:var(--text); opacity: 1;'>Light Intensity (Lux)</div>";
  html += "<p style='font-size:0.875rem;'>Intensity measures the volume of light. These levels indicate the degree of departure from a natural nighttime state.</p>";
  html += "<table><tr><th>Level</th><th>Range</th><th>Description</th></tr>";
  html += "<tr><td><b>1: Baseline</b></td><td>< 0.5 lx</td><td>Matches a natural environment without artificial light. Supports standard biological cycles for humans and wildlife.</td></tr>";
  html += "<tr><td><b>2: Elevated</b></td><td>0.5 - 5.0 lx</td><td>Common with nearby streetlighting. Can alter nocturnal animal behavior and influence human sleep quality over time.</td></tr>";
  html += "<tr><td><b>3: High Intensity</b></td><td>> 5.0 lx</td><td>Excessive for natural environments. Associated with a measurable decrease in rest and significant ecosystem disruption.</td></tr></table>";
  html += "</div>";

  html += "<div class='card' style='margin-bottom: 1.5rem;'>";
  html += "<div class='card-header' style='font-size:1rem; color:var(--text); opacity: 1;'>Color Temperature (Kelvin)</div>";
  html += "<p style='font-size:0.875rem;'>Identifies the 'tint' of the light. Higher values indicate more blue-toned light, which has a stronger physiological impact.</p>";
  html += "<table><tr><th>Level</th><th>Range</th><th>Description</th></tr>";
  html += "<tr><td><b>1: Amber</b></td><td>< 2700 K</td><td>Minimal blue-toned energy. Least likely to interfere with biological clocks or create skyglow in the atmosphere.</td></tr>";
  html += "<tr><td><b>2: Transitional</b></td><td>2700 - 3000 K</td><td>A neutral white light. Maximum allowable limit for outdoor lighting to balance safety with habitat protection.</td></tr>";
  html += "<tr><td><b>3: Blue-Rich</b></td><td>> 3000 K</td><td>Cool-toned light. Scatters easily in the air, increasing light pollution, and has the strongest impact on rest cycles.</td></tr></table>";
  html += "</div>";

  html += "<a href='/' class='btn'>&larr; Back to Dashboard</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleDownload() {
  String csv = "Uptime,Lux,Color_Temp_K,RGB_Color,Intensity_Level,Appearance_Level\n";
  for (int i = 0; i < logCount; i++) {
    int idx = (logIndex - logCount + i + MAX_LOGS) % MAX_LOGS;
    float loggedLux = luxHistory[idx];
    float loggedCCT = cctHistory[idx];
    int r = rHistory[idx];
    int g = gHistory[idx];
    int b = bHistory[idx];
    
    csv += formatTime(timeHistory[idx]) + ",";
    csv += String(loggedLux, 3) + ",";
    csv += String(loggedCCT, 0) + ",";
    csv += "(" + String(r) + " " + String(g) + " " + String(b) + "),";
    csv += getLuxStatus(loggedLux) + ",";
    csv += getKelvinStatus(loggedCCT) + "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"light_trespass_data.csv\"");
  server.send(200, "text/csv", csv);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); 

  if (!sensor.begin()) {
    Serial.println("Failed to find OPT4048 chip");
  } else {
    Serial.println("OPT4048 sensor found!");
    sensor.setRange(OPT4048_RANGE_AUTO);  
    sensor.setConversionTime(OPT4048_CONVERSION_TIME_100MS); 
    sensor.setMode(OPT4048_MODE_CONTINUOUS);  
  }

  WiFi.softAP(ssid, password);
  
  server.on("/", handleRoot);
  server.on("/guide", handleGuide); 
  server.on("/download", handleDownload); 
  
  server.begin();
}

void loop() {
  server.handleClient();
}