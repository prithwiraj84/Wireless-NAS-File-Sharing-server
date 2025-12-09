#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <map>
#include <list>
#include <set>
#include "esp_wifi.h"

#include <esp_netif.h>    

const char* apSSID = "wireless server";
const char* apPassword = "password123";
#define CS_PIN 5

WebServer server(80);
File uploadFile;
unsigned long startTime = 0;

enum UserRole { ADMIN, USER, VIEWER };

struct User {
  String password;
  UserRole role;
};

std::map<String, User> users = {
  {"admin", {"adminpass", ADMIN}},
  {"user", {"userpass", USER}},
  {"viewer", {"viewerpass", VIEWER}}
};

String currentUsername = "";

struct LogEntry {
  unsigned long timestamp;
  String ipAddress;
  String username;
  String action;
};

std::list<LogEntry> serverLogs;
const size_t MAX_LOG_ENTRIES = 100;

void logEvent(String ip, String user, String action) {
  if (serverLogs.size() >= MAX_LOG_ENTRIES) {
    serverLogs.pop_front();
  }
  LogEntry entry;
  entry.timestamp = millis();
  entry.ipAddress = ip;
  entry.username = user.isEmpty() ? "N/A" : user;
  entry.action = action;
  serverLogs.push_back(entry);

  Serial.printf("[LOG] Time: %lu, IP: %s, User: %s, Action: %s\n",
                entry.timestamp, ip.c_str(), entry.username.c_str(), action.c_str());
}


struct LoginAttempt {
  String ipAddress;
  unsigned long timestamp;
};

std::list<LoginAttempt> loginAttempts;
std::set<String> bannedIPs;
const int MAX_LOGIN_ATTEMPTS = 5;
const unsigned long BLOCK_DURATION = 5 * 60 * 1000;

bool isIpBlocked(String ip) {
  if (bannedIPs.count(ip)) {
    Serial.printf("Access denied for manually banned IP: %s\n", ip.c_str());
    return true;
  }

  unsigned long currentTime = millis();
  int attemptCount = 0;
  auto it = loginAttempts.begin();

  while (it != loginAttempts.end()) {
      if (currentTime - it->timestamp >= BLOCK_DURATION) {
          it = loginAttempts.erase(it);
      } else {
          if (it->ipAddress == ip) {
              attemptCount++;
          }
          ++it;
      }
  }

  while (loginAttempts.size() > MAX_LOGIN_ATTEMPTS * 10 &&
         currentTime - loginAttempts.front().timestamp >= BLOCK_DURATION * 2 ) {
     loginAttempts.pop_front();
  }

  if (attemptCount >= MAX_LOGIN_ATTEMPTS) {
    Serial.printf("Access denied for IP %s due to too many attempts (%d)\n", ip.c_str(), attemptCount);
    return true;
  }

  return false;
}

String userRoleStr(UserRole role) {
    switch(role) {
        case ADMIN: return "Admin";
        case USER: return "User";
        case VIEWER: return "Viewer";
        default: return "Unknown";
    }
}

void handleFileUpload() {
  String clientIP = server.client().remoteIP().toString();

  if (currentUsername.isEmpty()) {
    server.send(401, "text/plain", "Unauthorized: Login required.");
    logEvent(clientIP, "", "File Upload Attempt - Unauthorized");
    return;
  }
  if (!users.count(currentUsername) || users[currentUsername].role == VIEWER) {
    server.send(403, "text/plain", "Forbidden: Viewers cannot upload files.");
    logEvent(clientIP, currentUsername, "File Upload Attempt - Forbidden (" + (users.count(currentUsername) ? userRoleStr(users[currentUsername].role) : "Unknown User") + ")");
    return;
  }

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (filename.isEmpty() || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1 || filename.indexOf("..") != -1) {
        Serial.println("Upload blocked: Invalid filename '" + filename + "'");
        logEvent(clientIP, currentUsername, "File Upload Attempt - Invalid Filename: " + filename);
        server.send(400, "text/plain", "Invalid filename provided.");
        if(uploadFile) uploadFile.close();
        uploadFile = File();
        return;
    }

    String filepath = "/" + filename;
    Serial.print("Upload Start: "); Serial.println(filepath);
    logEvent(clientIP, currentUsername, "Upload Start: " + filename);

    if (SD.exists(filepath)) {
        if (!SD.remove(filepath)) {
             Serial.println("Failed to remove existing file: " + filepath);
             logEvent(clientIP, currentUsername, "Upload Error: Failed to remove existing file " + filename);
             server.send(500, "text/plain", "Server error: Could not replace existing file.");
             if(uploadFile) uploadFile.close();
             uploadFile = File();
             return;
        }
    }

    uploadFile = SD.open(filepath, FILE_WRITE);
    if (!uploadFile) {
        Serial.println("Failed to open file for writing: " + filepath);
        logEvent(clientIP, currentUsername, "Upload Error: Failed to open " + filename + " for writing");
        server.send(500, "text/plain", "Server error: Could not open file for writing.");
        return;
    }
    Serial.println("File opened for writing: " + filepath);

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
        size_t bytesWritten = uploadFile.write(upload.buf, upload.currentSize);
        if (bytesWritten != upload.currentSize) {
            Serial.println("Upload write error! Wrote " + String(bytesWritten) + "/" + String(upload.currentSize) + " bytes.");
            logEvent(clientIP, currentUsername, "Upload Error: Write failed for " + upload.filename);
            uploadFile.close();
            uploadFile = File();
            SD.remove(uploadFile.name());
        } else {
        }
    } else {
         Serial.println("Upload Error: Write attempted on invalid file handle.");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      String filename = uploadFile.name();
      uploadFile.close();
      Serial.println("Upload Success: " + filename);
      logEvent(clientIP, currentUsername, "Upload Success: " + filename.substring(1));
      server.send(200, "text/plain", "File Uploaded Successfully: " + filename.substring(1));
      uploadFile = File();
    } else {
      Serial.println("Upload Failed (End state, file handle invalid).");
      logEvent(clientIP, currentUsername, "Upload Failed: " + (upload.filename.isEmpty() ? "(unknown file)" : upload.filename));
      server.send(500, "text/plain", "Upload Failed.");
      uploadFile = File();
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Serial.println("Upload Aborted by client.");
      logEvent(clientIP, currentUsername, "Upload Aborted: " + (upload.filename.isEmpty() ? "(unknown file)" : upload.filename));
      if(uploadFile) {
          String filename = uploadFile.name();
          uploadFile.close();
          SD.remove(filename);
          Serial.println("Removed partial file: " + filename);
      }
      uploadFile = File();
  }
}


void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\nESP32 NAS Server Starting...");

  startTime = millis();

  Serial.print("Starting WiFi AP: ");
  Serial.println(apSSID);
  WiFi.softAP(apSSID, apPassword);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("Initializing SD card...");
  if (!SD.begin(CS_PIN)) {
    Serial.println(" Card Mount Failed or Card not present!");
    logEvent("SERVER", "SYSTEM", "SD Card Mount Failed - Halting");
    while(1) { delay(1000); }
  } else {
      uint8_t cardType = SD.cardType();
      if(cardType == CARD_NONE){
          Serial.println(" No SD card attached!");
          logEvent("SERVER", "SYSTEM", "SD Card Type: None - Halting");
          while(1) { delay(1000); }
      } else {
        Serial.print(" Type: ");
        if(cardType == CARD_MMC) Serial.print("MMC");
        else if(cardType == CARD_SD) Serial.print("SDSC");
        else if(cardType == CARD_SDHC) Serial.print("SDHC");
        else Serial.print("UNKNOWN");
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf(", Size: %llu MB\n", cardSize);
        logEvent("SERVER", "SYSTEM", "SD Card Initialized Successfully");
      }
  }

  server.on("/", HTTP_GET, handleRootRedirect);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLoginSubmit);
  server.on("/logout", HTTP_GET, handleLogout);

  server.on("/home", HTTP_GET, handleRoot);
  server.on("/list", HTTP_GET, listFiles);

  server.on("/upload", HTTP_POST, []() {
  }, handleFileUpload);

  server.on("/download", HTTP_GET, handleFileDownload);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/storage", HTTP_GET, handleStorageInfo);
  server.on("/system", HTTP_GET, handleSystemInfo);

  server.on("/console", HTTP_GET, handleConsole);
  server.on("/console/data", HTTP_GET, handleConsoleData);
  server.on("/ban", HTTP_POST, handleBanIP);
  server.on("/unban", HTTP_POST, handleUnbanIP);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started.");
  logEvent("SERVER", "SYSTEM", "HTTP Server Started");
}

void loop() {
  server.handleClient();
}


String getCommonCSS() {
    String css = "<style>";
    css += "body{font-family:'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;margin:0;padding:0;background:#f5f7fa;color:#333;}";
    css += ".container{max-width:950px;margin:0 auto;padding:15px;}";
    css += "h1,h2,h3{color:#2c3e50;}h1{text-align:center;margin-bottom:30px;color:#3498db;}";
    css += ".card{background:#fff;border-radius:8px;box-shadow:0 4px 6px rgba(0,0,0,0.1);padding:20px;margin-bottom:20px;}";
    css += "button,input[type='file'],input[type='text'],input[type='password'],input[type='submit']{padding:10px 15px;border:1px solid #ccc;border-radius:4px;cursor:pointer;transition:background 0.3s, box-shadow 0.3s;margin:5px 3px; font-size: 14px;}";
    css += "button{background:#3498db;color:white;border:none;}button:hover{background:#2980b9;box-shadow:0 2px 4px rgba(0,0,0,0.2);}";
    css += "button:active{transform: translateY(1px);}";
    css += "button.danger{background:#e74c3c;}button.danger:hover{background:#c0392b;}";
    css += "button.success{background:#2ecc71;}button.success:hover{background:#27ae60;}";
    css += "button.secondary{background:#95a5a6;}button.secondary:hover{background:#7f8c8d;}";
    css += "table{width:100%;border-collapse:collapse;margin:15px 0;}";
    css += "th,td{padding:10px 12px;text-align:left;border-bottom:1px solid #ddd; word-wrap: break-word;}";
    css += "th{background:#3498db;color:white;white-space: nowrap;}";
    css += "tr:hover{background:#f0f5f9;}";
    css += "progress{width:100%;height:20px;border-radius:4px; border: 1px solid #ccc;} progress::-webkit-progress-bar{background-color:#eee;} progress::-webkit-progress-value{background-color:#3498db;} progress::-moz-progress-bar{background-color:#3498db;}";
    css += ".progress-container{margin:20px 0;}";
    css += ".flex-container{display:flex;justify-content:space-between;flex-wrap:wrap;gap:15px;}";
    css += ".flex-item{flex:1;min-width:300px;margin:0px;}";
    css += ".stats-grid{display:grid;grid-template-columns:repeat(auto-fill, minmax(180px, 1fr));gap:15px;margin:20px 0;}";
    css += ".stat-card{background:#ffffff;border-radius:8px;padding:15px;box-shadow:0 2px 4px rgba(0,0,0,0.08);}";
    css += ".stat-value{font-size:20px;font-weight:bold;color:#3498db;margin:5px 0;}";
    css += ".stat-label{color:#7f8c8d;font-size:13px; text-transform: uppercase;}";
    css += ".action-buttons{text-align:center;margin-top:20px;}";
    css += ".navbar{background:#34495e;padding:8px 0;margin-bottom:20px;text-align:center; box-shadow: 0 2px 5px rgba(0,0,0,0.2);}";
    css += ".navbar a{color:white;text-decoration:none;padding:8px 15px;margin:0 5px;border-radius:4px;transition:background 0.3s;}";
    css += ".navbar a:hover, .navbar a.active{background:#4a627a;}";
    css += ".logout-btn{background:#e74c3c !important; color:white !important;} .logout-btn:hover{background:#c0392b !important;}";

    css += ".log-table-container{max-height:450px;overflow-y:auto;margin-top:15px; border: 1px solid #ddd; border-radius: 4px;}";
    css += ".form-inline{display:inline-block;margin-left:10px;} .form-inline input[type='text']{width:180px;}";

    css += ".login-container { display: flex; justify-content: center; align-items: center; min-height: 90vh; }";
    css += ".login-form { background: #fff; padding: 35px; border-radius: 8px; box-shadow: 0 5px 15px rgba(0, 0, 0, 0.1); width: 320px; }";
    css += ".login-form h1 { text-align: center; margin-bottom: 25px; color: #3498db; font-size: 24px; }";
    css += ".login-form label { display: block; margin-bottom: 8px; color: #555; font-weight: 600;}";
    css += ".login-form input[type='text'], .login-form input[type='password'] { width: 100%; padding: 12px; margin-bottom: 18px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; font-size: 15px;}";
    css += ".login-form button { width: 100%; background: #3498db; color: white; padding: 12px 15px; border: none; border-radius: 4px; cursor: pointer; transition: background 0.3s; font-size: 16px; font-weight: bold;}";
    css += ".login-form button:hover { background: #2980b9; }";
    css += ".error-msg { color: #e74c3c; text-align: center; margin-top: 15px; font-weight: bold; }";

    css += "@media (max-width: 768px) { .flex-item{min-width: 100%;} .stats-grid{grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));} }";
    css += "@media (max-width: 600px) { .navbar a{display:block;margin:5px auto; width: 80%;} }";

    css += "</style>";
    return css;
}

String getNavBar(String activePage) {
    String nav = "<div class='navbar'>";
    if (!currentUsername.isEmpty()) {
        nav += "<a href='/home'";
        if (activePage == "home") nav += " class='active'";
        nav += ">Home</a>";
    }

    nav += "<a href='/list'";
    if (activePage == "list") nav += " class='active'";
    nav += ">Files</a>";

    nav += "<a href='/storage'";
    if (activePage == "storage") nav += " class='active'";
    nav += ">Storage</a>";

    if (!currentUsername.isEmpty() && users.count(currentUsername) && users[currentUsername].role == ADMIN) {
        nav += "<a href='/system'";
        if (activePage == "system") nav += " class='active'";
        nav += ">System</a>";

        nav += "<a href='/console'";
        if (activePage == "console") nav += " class='active'";
        nav += ">Console</a>";
    }
    if (!currentUsername.isEmpty()) {
        nav += "<a href='/logout' class='logout-btn'>Logout (" + currentUsername + ")</a>";
    }
    nav += "</div>";
    return nav;
}


String getHeader(String title, String activePage = "") {
    String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>" + title + " - ESP32 NAS</title>";
    html += getCommonCSS();
    html += "</head><body>";
    if (!currentUsername.isEmpty() && activePage != "login") {
        html += getNavBar(activePage);
    }
    html += "<div class='container'>";
    return html;
}

String getFooter() {
    String html = "</div>";
    html += "<footer style='text-align:center; margin-top:30px; padding:15px; color:#7f8c8d; font-size:12px; border-top: 1px solid #eee;'>";
    html += "ESP32 NAS Server | Uptime: " + formatUptime(millis() - startTime);
    html += " | Free Heap: " + formatBytes(ESP.getFreeHeap());
    html += "</footer>";
    html += "</body></html>";
    return html;
}

String formatBytes(size_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    else if (bytes < (1024 * 1024)) return String(bytes / 1024.0, 1) + " KB";
    else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0, 2) + " MB";
    else return String(bytes / 1024.0 / 1024.0 / 1024.0, 2) + " GB";
}

String formatUptime(unsigned long milliseconds) {
    if (milliseconds < 1000) return String(milliseconds) + " ms";
    unsigned long seconds = milliseconds / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;

    seconds %= 60;
    minutes %= 60;
    hours %= 24;

    String result = "";
    if (days > 0) result += String(days) + "d ";
    if (hours > 0 || days > 0) result += String(hours) + "h ";
    if (minutes > 0 || hours > 0 || days > 0) result += String(minutes) + "m ";
    result += String(seconds) + "s";

    if (result.length() == 1 && result[0] == 's') return "0s";

    return result.length() > 0 ? result : "0s";
}


void handleRootRedirect() {
     if (currentUsername.isEmpty()) {
        server.sendHeader("Location", "/login");
        server.send(302, "text/plain", "Redirecting to login...");
    } else {
        server.sendHeader("Location", "/home");
        server.send(302, "text/plain", "Redirecting to home...");
    }
}

void handleLogin() {
    String ip = server.client().remoteIP().toString();
    if (isIpBlocked(ip)) {
        String page = getHeader("Login Blocked", "login");
        page += "<h1>Access Temporarily Blocked</h1>";
        page += "<div class='card error-msg'><p>Too many failed login attempts from your IP address (" + ip + "). Access is blocked for a few minutes. Please try again later.</p></div>";
        page += getFooter();
        server.send(429, "text/html", page);
        logEvent(ip, "", "Login Page Access - Blocked (Rate Limit)");
        return;
    }
    logEvent(ip, "", "Login Page Accessed");

    String page = getHeader("Login", "login");
    page += "<div class='login-container'>";
    page += "<div class='login-form'>";
    page += "<h1>ESP32 NAS Login</h1>";
    page += "<form action='/login' method='post'>";
    page += "<label for='username'>Username:</label><input type='text' id='username' name='username' required autofocus><br>";
    page += "<label for='password'>Password:</label><input type='password' id='password' name='password' required><br>";
    page += "<button type='submit' class='success'>Login</button>";
    if (server.hasArg("error")) {
        page += "<p class='error-msg'>Invalid username or password.</p>";
    }
    page += "</form>";
    page += "</div>";
    page += "</div>";
    server.send(200, "text/html", page);
}

void handleLoginSubmit() {
    String username = server.arg("username");
    String password = server.arg("password");
    String ip = server.client().remoteIP().toString();

    if (isIpBlocked(ip)) {
        String page = getHeader("Login Blocked", "login");
        page += "<h1>Access Temporarily Blocked</h1>";
        page += "<div class='card error-msg'><p>Too many failed login attempts from your IP address (" + ip + "). Access is blocked for a few minutes. Please try again later.</p></div>";
        page += getFooter();
        server.send(429, "text/html", page);
        logEvent(ip, username, "Login Submit - Blocked (Rate Limit)");
        return;
    }

    if (users.count(username) && users[username].password == password) {
        currentUsername = username;
        logEvent(ip, username, "Login Successful");

        loginAttempts.remove_if([&](const LoginAttempt& a){ return a.ipAddress == ip; });

        server.sendHeader("Location", "/home");
        server.send(302, "text/plain", "Login successful. Redirecting...");

    } else {
        LoginAttempt attempt;
        attempt.ipAddress = ip;
        attempt.timestamp = millis();
        loginAttempts.push_back(attempt);
        logEvent(ip, username, "Login Failed (Invalid Credentials)");

        server.sendHeader("Location", "/login?error=1");
        server.send(302, "text/plain", "Login failed. Redirecting...");
    }
}

void handleLogout() {
    String clientIP = server.client().remoteIP().toString();
    if (!currentUsername.isEmpty()) {
        logEvent(clientIP, currentUsername, "Logout");
        currentUsername = "";
    } else {
         logEvent(clientIP, "", "Logout attempt when not logged in");
    }
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "Logged out. Redirecting...");
}

void handleRoot() {
    if (currentUsername.isEmpty()) {
        server.sendHeader("Location", "/login");
        server.send(302, "text/plain", "Redirecting to login...");
        return;
    }

    String clientIP = server.client().remoteIP().toString();
    logEvent(clientIP, currentUsername, "Accessed Home Page");

    String page = getHeader("Home", "home");
    page += "<h1>ESP32 Network Attached Storage</h1>";
    String userRoleStr_local = "Unknown";
    if (users.count(currentUsername)) {
       userRoleStr_local = userRoleStr(users[currentUsername].role);
    }
    page += "<h2>Welcome, " + currentUsername + " (" + userRoleStr_local + ")</h2>";

    page += "<div class='flex-container'>";

    page += "<div class='flex-item card'><h3>File Management</h3>";
    page += "<p><button onclick=\"location.href='/list'\" class='success'>Manage Files</button></p>";
    page += "<p><button onclick=\"location.href='/storage'\" class='secondary'>View Storage Info</button></p>";

    if (users.count(currentUsername) && users[currentUsername].role != VIEWER) {
        page += "<h4 style='margin-top: 20px;'>Upload File</h4>";
        page += "<form method='post' action='/upload' enctype='multipart/form-data' id='uploadForm'>";
        page += "<input type='file' name='file' id='fileUpload' required style='margin-bottom: 10px; display: block;'>";
        page += "<button type='submit' class='success'>Upload</button>";
        page += "</form>";
        page += "<div class='progress-container' style='display:none; margin-top: 15px;' id='progressDiv'>";
        page += "  <progress id='progressBar' value='0' max='100'></progress>";
        page += "  <span id='progressText' style='display: block; text-align: center; font-size: 12px; margin-top: 5px;'></span>";
        page += "</div>";
        page += "<script>";
        page += "const form = document.getElementById('uploadForm');";
        page += "const progressBar = document.getElementById('progressBar');";
        page += "const progressText = document.getElementById('progressText');";
        page += "const progressDiv = document.getElementById('progressDiv');";
        page += "form.addEventListener('submit', (e) => { ";
        page += "  const fileInput = document.getElementById('fileUpload'); ";
        page += "  if (!fileInput.files || fileInput.files.length === 0) { alert('Please select a file to upload.'); e.preventDefault(); return; } ";
        page += "  progressDiv.style.display = 'block'; ";
        page += "  progressBar.removeAttribute('value'); ";
        page += "  progressText.textContent = 'Uploading file: ' + fileInput.files[0].name + '... Please wait.'; ";
        page += "}); ";
        page += "</script>";

    } else {
        page += "<p><i>File upload is disabled for the Viewer role.</i></p>";
    }
    page += "</div>";

    page += "<div class='flex-item card'><h3>System Status</h3>";
    page += "<div class='stats-grid'>";
    page += "<div class='stat-card'><div class='stat-label'>Uptime</div><div class='stat-value' id='uptime'>Loading...</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Free Memory</div><div class='stat-value' id='memory'>Loading...</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Connected Clients</div><div class='stat-value' id='clients'>Loading...</div></div>";
    page += "</div>";

    if (users.count(currentUsername) && users[currentUsername].role == ADMIN) {
        page += "<div class='action-buttons'>";
        page += "<button onclick=\"location.href='/system'\" class='secondary'>Detailed System Info</button>";
        page += "<button onclick=\"location.href='/console'\" class='secondary'>Admin Console</button>";
        page += "</div>";
    }

    page += "<script>";
    page += "function updateStatus() {";
    page += " fetch('/system?json=1')";
    page += "  .then(response => { if (!response.ok) { throw new Error('Network response was not ok'); } return response.json(); })";
    page += "  .then(data => {";
    page += "   if(data) {";
    page += "     document.getElementById('uptime').textContent = data.uptime || 'N/A';";
    page += "     document.getElementById('memory').textContent = data.freeMemory || 'N/A';";
    page += "     document.getElementById('clients').textContent = data.clients || 'N/A';";
    page += "   }";
    page += "  })";
    page += "  .catch(error => {";
    page += "   console.error('Error fetching system status:', error);";
    page += "   document.getElementById('uptime').textContent = 'Error';";
    page += "   document.getElementById('memory').textContent = 'Error';";
    page += "   document.getElementById('clients').textContent = 'Error';";
    page += "  });";
    page += "}";
    page += "updateStatus();";
    page += "setInterval(updateStatus, 10000);";
    page += "</script>";
    page += "</div>";

    page += "</div>";

    page += getFooter();
    server.send(200, "text/html", page);
}


void listFiles() {
    if (currentUsername.isEmpty()) {
        server.sendHeader("Location", "/login");
        server.send(302, "text/plain", "Redirecting to login...");
        return;
    }
    String clientIP = server.client().remoteIP().toString();
    logEvent(clientIP, currentUsername, "Accessed File List Page");

    File root = SD.open("/");
    if(!root){
        logEvent(clientIP, currentUsername, "Error accessing SD card root directory");
        server.send(500, "text/plain", "Server Error: Could not access SD card.");
        return;
    }
    if(!root.isDirectory()){
        logEvent(clientIP, currentUsername, "Error: SD card root is not a directory!");
        server.send(500, "text/plain", "Server Error: SD card root is not a directory.");
        root.close();
        return;
    }

    String page = getHeader("Files", "list");
    page += "<h1>File List</h1>";
    if (server.hasArg("deleted")) {
        page += "<div class='card success' style='background-color: #dff0d8; color: #3c763d; padding: 15px;'>File '" + server.arg("deleted") + "' deleted successfully.</div>";
    }
     if (server.hasArg("upload_success")) {
        page += "<div class='card success' style='background-color: #dff0d8; color: #3c763d; padding: 15px;'>File '" + server.arg("upload_success") + "' uploaded successfully.</div>";
    }


    page += "<div class='card'>";
    page += "<table id='fileTable'>";
    page += "<thead><tr><th>File Name</th><th>Size</th><th>Actions</th></tr></thead>";
    page += "<tbody>";

    File file = root.openNextFile();
    bool filesFound = false;
    while (file) {
        if (file.isDirectory() || String(file.name()).startsWith("/.")) {
             file.close();
             file = root.openNextFile();
             continue;
        }

        filesFound = true;
        String filename = String(file.name());
        if (filename.startsWith("/")) filename = filename.substring(1);

        if (filename.isEmpty() || filename.indexOf("..") != -1) {
            file.close();
            file = root.openNextFile();
            continue;
        }

        page += "<tr>";
        page += "<td>" + filename + "</td>";
        page += "<td>" + formatBytes(file.size()) + "</td>";
        page += "<td>";
        page += "<a href='/download?file=" + filename + "' target='_blank'><button class='success' title='Download " + filename + "'>Download</button></a> ";

        if (users.count(currentUsername) && users[currentUsername].role != VIEWER) {
             page += "<a href='/delete?file=" + filename + "' onclick='return confirmDelete(\"" + filename + "\");'><button class='danger' title='Delete " + filename + "'>Delete</button></a>";
        }
        page += "</td>";
        page += "</tr>";

        file.close();
        file = root.openNextFile();
    }
    root.close();

    if (!filesFound) {
        page += "<tr><td colspan='3' style='text-align:center; padding: 20px;'>No files found in the root directory.</td></tr>";
    }

    page += "</tbody></table></div>";

    page += "<script>function confirmDelete(filename){ ";
    page += " return confirm('Are you sure you want to permanently delete the file \"' + filename + '\"? This action cannot be undone.'); ";
    page += "}</script>";

    page += getFooter();
    server.send(200, "text/html", page);
}

void handleFileDelete() {
    String clientIP = server.client().remoteIP().toString();
    if (currentUsername.isEmpty()) {
        server.send(401, "text/plain", "Unauthorized: Login required.");
        logEvent(clientIP, "", "Delete Attempt - Unauthorized");
        return;
    }
    if (!users.count(currentUsername) || users[currentUsername].role == VIEWER) {
        server.send(403, "text/plain", "Forbidden: Viewers cannot delete files.");
         logEvent(clientIP, currentUsername, String("Delete Attempt - Forbidden (") + (users.count(currentUsername) ? userRoleStr(users[currentUsername].role) : "Unknown User") + ")");
        return;
    }

    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Bad Request: 'file' parameter missing.");
         logEvent(clientIP, currentUsername, "Delete Attempt - Missing 'file' parameter");
        return;
    }

    String filename = server.arg("file");
     if (filename.isEmpty() || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1 || filename.indexOf("..") != -1) {
        server.send(400, "text/plain", "Bad Request: Invalid filename format.");
        logEvent(clientIP, currentUsername, "Delete Attempt - Invalid filename: " + filename);
        return;
    }

    String filepath = "/" + filename;
    logEvent(clientIP, currentUsername, "Attempting to delete file: " + filename);

    if (SD.exists(filepath)) {
        if (SD.remove(filepath)) {
            Serial.println("File deleted successfully: " + filepath);
            logEvent(clientIP, currentUsername, "Delete Success: " + filename);
            server.sendHeader("Location", "/list?deleted=" + filename);
            server.send(302, "text/plain", "File deleted. Redirecting...");
        } else {
            Serial.println("Failed to delete file: " + filepath);
            logEvent(clientIP, currentUsername, "Delete Failure: " + filename);
             String page = getHeader("Error Deleting File", "list");
             page += "<h1>Deletion Failed</h1><div class='card error-msg'>Could not delete the file '" + filename + "'. It might be in use or there could be an SD card issue.</div>";
             page += "<p style='text-align:center;'><a href='/list'><button>Back to Files</button></a></p>";
             page += getFooter();
             server.send(500, "text/html", page);
        }
    } else {
         logEvent(clientIP, currentUsername, "Delete Attempt - File not found: " + filename);
         String page = getHeader("File Not Found", "list");
         page += "<h1>File Not Found</h1><div class='card error-msg'>The file '" + filename + "' does not exist and cannot be deleted.</div>";
         page += "<p style='text-align:center;'><a href='/list'><button>Back to Files</button></a></p>";
         page += getFooter();
         server.send(404, "text/html", page);
    }
}

void handleFileDownload() {
    String clientIP = server.client().remoteIP().toString();
    if (currentUsername.isEmpty()) {
        server.send(401, "text/plain", "Unauthorized: Login required.");
        logEvent(clientIP, "", "Download Attempt - Unauthorized");
        return;
    }

    if (!server.hasArg("file")) {
        server.send(400, "text/plain", "Bad Request: 'file' parameter missing.");
         logEvent(clientIP, currentUsername, "Download Attempt - Missing 'file' parameter");
        return;
    }
    String filename = server.arg("file");
    if (filename.isEmpty() || filename.indexOf('/') != -1 || filename.indexOf('\\') != -1 || filename.indexOf("..") != -1) {
        server.send(400, "text/plain", "Bad Request: Invalid filename format.");
         logEvent(clientIP, currentUsername, "Download Attempt - Invalid filename: " + filename);
        return;
    }

    String filepath = "/" + filename;
    logEvent(clientIP, currentUsername, "Download Attempt: " + filename);

    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        logEvent(clientIP, currentUsername, "Download Error: File not found - " + filename);
        server.send(404, "text/plain", "File Not Found: The requested file does not exist.");
        return;
    }
     if (file.isDirectory()) {
        logEvent(clientIP, currentUsername, "Download Error: Attempted to download a directory - " + filename);
        server.send(400, "text/plain", "Bad Request: Cannot download a directory.");
        file.close();
        return;
    }

    server.sendHeader("Content-Type", "application/octet-stream");
    String headerFilename = filename;
    headerFilename.replace("\"", "'");
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + headerFilename + "\"");
    server.sendHeader("Content-Length", String(file.size()));
    server.sendHeader("Connection", "close");

    size_t bytesSent = server.streamFile(file, "application/octet-stream");

    file.close();

    if (bytesSent == file.size()) {
      logEvent(clientIP, currentUsername, "Download Success: " + filename + " (" + formatBytes(bytesSent) + ")");
    } else {
      logEvent(clientIP, currentUsername, "Download Incomplete: " + filename + " (Sent " + formatBytes(bytesSent) + "/" + formatBytes(file.size()) + ")");
    }
}

void handleStorageInfo() {
    if (currentUsername.isEmpty()) {
        server.sendHeader("Location", "/login");
        server.send(302, "text/plain", "Redirecting to login...");
        return;
    }
    String clientIP = server.client().remoteIP().toString();
    logEvent(clientIP, currentUsername, "Accessed Storage Info Page");

    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();

    uint64_t freeBytes = 0;
    int usedPercent = 0;
    if (totalBytes > 0) {
        freeBytes = totalBytes - usedBytes;
        usedPercent = static_cast<int>((static_cast<double>(usedBytes) / totalBytes) * 100.0);
    } else {
        logEvent(clientIP, currentUsername, "Storage Info Warning: SD card reporting 0 total bytes.");
    }

    String page = getHeader("Storage Information", "storage");
    page += "<h1>Storage Information</h1>";

    if (totalBytes == 0) {
        page += "<div class='card error-msg'>Could not retrieve storage information. Please check the SD card connection and formatting.</div>";
    } else {
        page += "<div class='card'>";
        page += "<div class='stats-grid'>";
        page += "<div class='stat-card'><div class='stat-label'>Total Capacity</div><div class='stat-value'>" + formatBytes(totalBytes) + "</div></div>";
        page += "<div class='stat-card'><div class='stat-label'>Used Space</div><div class='stat-value'>" + formatBytes(usedBytes) + "</div></div>";
        page += "<div class='stat-card'><div class='stat-label'>Free Space</div><div class='stat-value'>" + formatBytes(freeBytes) + "</div></div>";
        page += "</div>";

        page += "<div class='progress-container'>";
        page += "<progress value='" + String(usedPercent) + "' max='100' title='" + String(usedPercent) + "% used'></progress>";
        page += "<div style='text-align:center; margin-top:5px; font-weight: bold;'>" + String(usedPercent) + "% used</div>";
        page += "<div style='text-align:center; margin-top:2px; font-size: 12px; color: #555;'>(" + formatBytes(usedBytes) + " of " + formatBytes(totalBytes) + ")</div>";
        page += "</div>";
        page += "</div>";
    }

    page += getFooter();
    server.send(200, "text/html", page);
}

void handleSystemInfo() {
    String clientIP = server.client().remoteIP().toString();
    if (currentUsername.isEmpty()) {
        server.sendHeader("Location", "/login");
        server.send(302, "text/plain", "Redirecting to login...");
        return;
    }

    if (server.hasArg("json")) {
        logEvent(clientIP, currentUsername, "Accessed System Info (JSON)");
        String json = "{";
        json += "\"uptime\":\"" + formatUptime(millis() - startTime) + "\",";
        json += "\"freeMemory\":\"" + formatBytes(ESP.getFreeHeap()) + "\",";
        json += "\"minFreeMemory\":\"" + formatBytes(ESP.getMinFreeHeap()) + "\",";
        json += "\"clients\":\"" + String(WiFi.softAPgetStationNum()) + "\",";
        json += "\"chipModel\":\"" + String(ESP.getChipModel()) + "\",";
        json += "\"chipRevision\":\"" + String(ESP.getChipRevision()) + "\",";
        json += "\"cpuFreq\":\"" + String(ESP.getCpuFreqMHz()) + " MHz\"";
        json += "}";
        server.sendHeader("Cache-Control", "no-cache");
        server.send(200, "application/json", json);
        return;
    }

    if (!users.count(currentUsername) || users[currentUsername].role != ADMIN) {
        server.send(403, "text/plain", "Forbidden: Admin access required for detailed system info.");
        logEvent(clientIP, currentUsername, "System Info Page Attempt - Forbidden (" + (users.count(currentUsername) ? userRoleStr(users[currentUsername].role) : "Unknown User") + ")");
        return;
    }

    logEvent(clientIP, currentUsername, "Accessed System Info Page (Admin)");

    String page = getHeader("System Information", "system");
    page += "<h1>System Information</h1>";

    page += "<div class='card'><h2>ESP32 System</h2>";
    page += "<div class='stats-grid'>";
    page += "<div class='stat-card'><div class='stat-label'>Chip Model</div><div class='stat-value'>" + String(ESP.getChipModel()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Chip Revision</div><div class='stat-value'>" + String(ESP.getChipRevision()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>CPU Cores</div><div class='stat-value'>" + String(ESP.getChipCores()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>CPU Frequency</div><div class='stat-value'>" + String(ESP.getCpuFreqMHz()) + " MHz</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>SDK Version</div><div class='stat-value'>" + String(ESP.getSdkVersion()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Server Uptime</div><div class='stat-value'>" + formatUptime(millis() - startTime) + "</div></div>";
    page += "</div></div>";

    page += "<div class='card'><h2>Memory</h2>";
    page += "<div class='stats-grid'>";
    page += "<div class='stat-card'><div class='stat-label'>Total Heap</div><div class='stat-value'>" + formatBytes(ESP.getHeapSize()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Free Heap</div><div class='stat-value'>" + formatBytes(ESP.getFreeHeap()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Min Free Heap</div><div class='stat-value'>" + formatBytes(ESP.getMinFreeHeap()) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Max Alloc Heap</div><div class='stat-value'>" + formatBytes(ESP.getMaxAllocHeap()) + "</div></div>";
    page += "</div></div>";

     page += "<div class='card'><h2>Flash Memory</h2>";
     page += "<div class='stats-grid'>";
     page += "<div class='stat-card'><div class='stat-label'>Flash Chip Size</div><div class='stat-value'>" + formatBytes(ESP.getFlashChipSize()) + "</div></div>";
     page += "<div class='stat-card'><div class='stat-label'>Flash Speed</div><div class='stat-value'>" + String(ESP.getFlashChipSpeed() / 1000000) + " MHz</div></div>";
      page += "<div class='stat-card'><div class='stat-label'>Sketch Size</div><div class='stat-value'>" + formatBytes(ESP.getSketchSize()) + "</div></div>";
      page += "<div class='stat-card'><div class='stat-label'>Free Sketch Space</div><div class='stat-value'>" + formatBytes(ESP.getFreeSketchSpace()) + "</div></div>";
     page += "</div></div>";

    page += "<div class='card'><h2>Network (AP Mode)</h2>";
    page += "<div class='stats-grid'>";
    page += "<div class='stat-card'><div class='stat-label'>Mode</div><div class='stat-value'>Access Point (AP)</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>SSID</div><div class='stat-value'>" + String(apSSID) + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>IP Address</div><div class='stat-value'>" + WiFi.softAPIP().toString() + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>MAC Address</div><div class='stat-value'>" + WiFi.softAPmacAddress() + "</div></div>";
    page += "<div class='stat-card'><div class='stat-label'>Connected Clients</div><div class='stat-value'>" + String(WiFi.softAPgetStationNum()) + "</div></div>";
    page += "</div></div>";

    page += getFooter();
    server.send(200, "text/html", page);
}


void handleConsole() {
    String clientIP = server.client().remoteIP().toString();
    if (currentUsername.isEmpty() || !users.count(currentUsername) || users[currentUsername].role != ADMIN) {
        logEvent(clientIP, currentUsername.isEmpty() ? "" : currentUsername, "Console Access Attempt - Forbidden");
         server.sendHeader("Location", currentUsername.isEmpty() ? "/login" : "/home");
         server.send(302, "text/plain", "Redirecting...");
        return;
    }

    logEvent(clientIP, currentUsername, "Accessed Admin Console");

    String page = getHeader("Admin Console", "console");
    page += "<h1>Admin Console</h1>";

    page += "<div class='card'><h2>Server Activity Log</h2>";
    page += "<p>Displaying the last " + String(MAX_LOG_ENTRIES) + " log entries (newest first).</p>";
    page += "<div id='logTableContainer' class='log-table-container'>";
    page += "<table id='logTable'><thead><tr><th>Timestamp</th><th>IP Address</th><th>User</th><th>Action</th></tr></thead>";
    page += "<tbody id='logTableBody'><tr><td colspan='4' style='text-align:center; padding: 20px;'>Loading logs...</td></tr></tbody></table>";
    page += "</div></div>";

    page += "<div class='card'><h2>Connected Clients</h2>";
    page += "<div id='clientTableContainer'>";
    page += "<table id='clientTable'><thead><tr><th>IP Address</th><th>MAC Address</th><th>Actions</th></tr></thead>"; // Changed header
    page += "<tbody id='clientTableBody'><tr><td colspan='3' style='text-align:center; padding: 20px;'>Loading client list...</td></tr></tbody></table>";
    page += "</div></div>";

     page += "<div class='card'><h2>IP Address Management</h2>";
    page += "<h3>Ban IP Address</h3>";
    page += "<form method='post' action='/ban' class='form-inline' onsubmit='return confirmBan(this.ip_address.value);'>";
    page += "<label for='ip_to_ban' style='margin-right: 5px;'>IP:</label>";
    page += "<input type='text' name='ip_address' id='ip_to_ban' placeholder='Enter IP to ban' required pattern='^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}$' title='Enter a valid IPv4 address (e.g., 192.168.1.100)'>";
    page += "<button type='submit' class='danger'>Ban IP</button>";
    page += "</form>";

    page += "<h3 style='margin-top: 25px;'>Manually Banned IPs</h3>";
    page += "<div id='bannedIpTableContainer'>";
    page += "<table id='bannedIpTable'><thead><tr><th>IP Address</th><th>Action</th></tr></thead>";
    page += "<tbody id='bannedIpTableBody'><tr><td colspan='2' style='text-align:center; padding: 15px;'>Loading banned list...</td></tr></tbody></table>";
    page += "</div></div>";

    page += "<script>";
    page += "function updateConsoleData() {";
    page += " fetch('/console/data')";
    page += "  .then(response => { if (!response.ok) { throw new Error('Network error fetching data'); } return response.json(); })";
    page += "  .then(data => { if(!data) return; ";
    page += "    updateLogTable(data.logs);";
    page += "    updateClientTable(data.clients);";
    page += "    updateBannedIpTable(data.banned_ips);";
    page += "  })";
    page += "  .catch(error => {";
    page += "    console.error('Error fetching console data:', error);";
    page += "    document.getElementById('logTableBody').innerHTML = '<tr><td colspan=4 style=\"color:red;text-align:center;\">Error loading logs.</td></tr>';";
    page += "    document.getElementById('clientTableBody').innerHTML = '<tr><td colspan=3 style=\"color:red;text-align:center;\">Error loading clients.</td></tr>';";
    page += "    document.getElementById('bannedIpTableBody').innerHTML = '<tr><td colspan=2 style=\"color:red;text-align:center;\">Error loading banned IPs.</td></tr>';";
    page += "  });";
    page += "}";

    page += "function updateLogTable(logs) {";
    page += " const tbody = document.getElementById('logTableBody'); tbody.innerHTML = ''; ";
    page += " if (logs && logs.length > 0) {";
    page += "  logs.forEach(log => {";
    page += "   const row = tbody.insertRow();";
    page += "   const timeCell = row.insertCell(); timeCell.textContent = new Date(log.timestamp).toLocaleString(); timeCell.style.whiteSpace = 'nowrap';";
    page += "   row.insertCell().textContent = log.ip;";
    page += "   row.insertCell().textContent = log.user;";
    page += "   const actionCell = row.insertCell(); actionCell.textContent = log.action; actionCell.style.wordBreak = 'break-all'; ";
    page += "  });";
    page += " } else { tbody.innerHTML = '<tr><td colspan=\"4\" style=\"text-align:center; padding: 15px;\">No log entries available.</td></tr>'; }";
    page += "}";

    page += "function updateClientTable(clients) {";
    page += " const tbody = document.getElementById('clientTableBody'); tbody.innerHTML = ''; ";
    page += " if (clients && clients.length > 0) {";
    page += "  clients.forEach(client => {";
    page += "   const row = tbody.insertRow();";
    page += "   row.insertCell().textContent = client.ip;"; // Will show N/A if API failed
    page += "   row.insertCell().textContent = client.mac;";
    page += "   const actionCell = row.insertCell();";
    // Ban button should still work using the IP from the log if needed, or ban MAC? Banning IP from client list is tricky now.
    // Let's make ban button use the MAC instead, or just remove ban button from client list for now?
    // Easiest: Remove ban button here, admin must use the Ban IP form below.
    // page += "   actionCell.innerHTML = `<form method='post' action='/ban' style='display:inline;' onsubmit='return confirmBan(\"${client.ip}\");'><input type='hidden' name='ip_address' value='${client.ip}'><button type='submit' class='danger' title='Ban IP ${client.ip}'>Ban</button></form>`;";
     page += "   actionCell.innerHTML = `(Use Ban Form Below)`;"; // Placeholder instead of ban button
    page += "  });";
    page += " } else { tbody.innerHTML = '<tr><td colspan=\"3\" style=\"text-align:center; padding: 15px;\">No clients currently connected.</td></tr>'; }";
    page += "}";

    page += "function updateBannedIpTable(bannedIps) {";
    page += " const tbody = document.getElementById('bannedIpTableBody'); tbody.innerHTML = ''; ";
    page += " if (bannedIps && bannedIps.length > 0) {";
    page += "  bannedIps.forEach(ip => {";
    page += "   const row = tbody.insertRow();";
    page += "   row.insertCell().textContent = ip;";
    page += "   const actionCell = row.insertCell();";
    page += "   actionCell.innerHTML = `<form method='post' action='/unban' style='display:inline;' onsubmit='return confirmUnban(\"${ip}\");'><input type='hidden' name='ip_address' value='${ip}'><button type='submit' class='secondary' title='Unban IP ${ip}'>Unban</button></form>`;";
    page += "  });";
    page += " } else { tbody.innerHTML = '<tr><td colspan=\"2\" style=\"text-align:center; padding: 15px;\">No IPs are currently manually banned.</td></tr>'; }";
    page += "}";

    page += "function confirmBan(ip) { if (!ip || ip === 'N/A') { alert('Cannot ban N/A IP.'); return false;} return confirm('Are you sure you want to manually ban the IP address: ' + ip + '?\\nThis will immediately block their access.'); }";
    page += "function confirmUnban(ip) { return confirm('Are you sure you want to unban the IP address: ' + ip + '?'); }";

    page += "document.addEventListener('DOMContentLoaded', () => {";
    page += " updateConsoleData(); ";
    page += " setInterval(updateConsoleData, 7000);";
    page += "});";
    page += "</script>";

    page += getFooter();
    server.send(200, "text/html", page);
}

void handleConsoleData() {
    String clientIP = server.client().remoteIP().toString();
     if (currentUsername.isEmpty() || !users.count(currentUsername) || users[currentUsername].role != ADMIN) {
        logEvent(clientIP, currentUsername.isEmpty() ? "" : currentUsername, "Console Data Request - Forbidden");
        server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    String json = "{";

    json += "\"logs\":[";
    bool firstLog = true;
    for (auto it = serverLogs.rbegin(); it != serverLogs.rend(); ++it) {
        if (!firstLog) json += ",";
        json += "{";
        json += "\"timestamp\":" + String(it->timestamp) + ",";
        String ip_escaped = it->ipAddress; ip_escaped.replace("\\", "\\\\"); ip_escaped.replace("\"", "\\\"");
        String user_escaped = it->username; user_escaped.replace("\\", "\\\\"); user_escaped.replace("\"", "\\\"");
        String action_escaped = it->action; action_escaped.replace("\\", "\\\\"); action_escaped.replace("\"", "\\\"");
        json += "\"ip\":\"" + ip_escaped + "\",";
        json += "\"user\":\"" + user_escaped + "\",";
        json += "\"action\":\"" + action_escaped + "\"";
        json += "}";
        firstLog = false;
    }
    json += "],";

    json += "\"clients\":[";
    wifi_sta_list_t station_list;
    memset(&station_list, 0, sizeof(station_list));
    esp_wifi_ap_get_sta_list(&station_list);

    bool firstClient = true;
    for (int i = 0; i < station_list.num; i++) {
         wifi_sta_info_t station_mac_info = station_list.sta[i];

         if (!firstClient) json += ",";
         json += "{";
         json += "\"ip\":\"N/A\","; // IP address cannot be reliably retrieved anymore

         char macStr[18];
         snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                  station_mac_info.mac[0], station_mac_info.mac[1], station_mac_info.mac[2],
                  station_mac_info.mac[3], station_mac_info.mac[4], station_mac_info.mac[5]);
         json += "\"mac\":\"" + String(macStr) + "\"";
         json += "}";
         firstClient = false;
     }
    json += "],";

    json += "\"banned_ips\":[";
    bool firstBan = true;
    for (const String& ip : bannedIPs) {
        if (!firstBan) json += ",";
        String ip_escaped = ip; ip_escaped.replace("\\", "\\\\"); ip_escaped.replace("\"", "\\\"");
        json += "\"" + ip_escaped + "\"";
        firstBan = false;
    }
    json += "]";

    json += "}";

    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
}


void handleBanIP() {
    String clientIP = server.client().remoteIP().toString();
    if (currentUsername.isEmpty() || !users.count(currentUsername) || users[currentUsername].role != ADMIN) {
        logEvent(clientIP, currentUsername.isEmpty() ? "" : currentUsername, "IP Ban Attempt - Forbidden");
        server.send(403, "text/plain", "Forbidden: Admin access required.");
        return;
    }

    if (!server.hasArg("ip_address")) {
         logEvent(clientIP, currentUsername, "IP Ban Attempt - Missing 'ip_address' parameter");
         server.send(400, "text/plain", "Bad Request: IP address parameter missing.");
         return;
    }
    String ipToBan = server.arg("ip_address");

     if (ipToBan.length() < 7 || ipToBan.length() > 15 || ipToBan.indexOf('.') == -1 || ipToBan == "N/A") { // Added check for N/A
        logEvent(clientIP, currentUsername, "IP Ban Attempt - Invalid IP format: " + ipToBan);
        server.send(400, "text/plain", "Bad Request: Invalid IP address format provided.");
        return;
     }

    if (ipToBan == clientIP) {
        logEvent(clientIP, currentUsername, "IP Ban Attempt - Prevented self-ban: " + ipToBan);
        server.sendHeader("Location", "/console?error=selfban");
        server.send(302, "text/plain", "Redirecting...");
        return;
    }

    auto result = bannedIPs.insert(ipToBan);

    if (result.second) {
        logEvent(clientIP, currentUsername, "IP Manually Banned: " + ipToBan);
        Serial.println("Admin '" + currentUsername + "' banned IP: " + ipToBan);
    } else {
        logEvent(clientIP, currentUsername, "IP Ban Attempt - Already Banned: " + ipToBan);
    }

    server.sendHeader("Location", "/console");
    server.send(302, "text/plain", "Processing ban request. Redirecting...");
}

void handleUnbanIP() {
     String clientIP = server.client().remoteIP().toString();
    if (currentUsername.isEmpty() || !users.count(currentUsername) || users[currentUsername].role != ADMIN) {
        logEvent(clientIP, currentUsername.isEmpty() ? "" : currentUsername, "IP Unban Attempt - Forbidden");
        server.send(403, "text/plain", "Forbidden: Admin access required.");
        return;
    }

     if (!server.hasArg("ip_address")) {
         logEvent(clientIP, currentUsername, "IP Unban Attempt - Missing 'ip_address' parameter");
         server.send(400, "text/plain", "Bad Request: IP address parameter missing.");
         return;
    }
    String ipToUnban = server.arg("ip_address");

    size_t removedCount = bannedIPs.erase(ipToUnban);

    if (removedCount > 0) {
        logEvent(clientIP, currentUsername, "IP Manually Unbanned: " + ipToUnban);
        Serial.println("Admin '" + currentUsername + "' unbanned IP: " + ipToUnban);
    } else {
         logEvent(clientIP, currentUsername, "IP Unban Attempt - IP Not Found in Ban List: " + ipToUnban);
    }

    server.sendHeader("Location", "/console");
    server.send(302, "text/plain", "Processing unban request. Redirecting...");
}


void handleNotFound() {
    String clientIP = server.client().remoteIP().toString();
    String requestedUrl = server.uri();

    logEvent(clientIP, currentUsername, "Page Not Found (404): " + requestedUrl);

    String page = getHeader("404 Not Found");
    page += "<h1>404 - Page Not Found</h1>";
    page += "<div class='card'>";
    page += "<p>Sorry, the page you requested (<code>" + requestedUrl + "</code>) could not be found on this server.</p>";
    page += "<p style='margin-top: 20px;'>You might want to return to the ";
    if (currentUsername.isEmpty()) {
        page += "<a href='/login'><button class='secondary'>Login Page</button></a>.</p>";
    } else {
         page += "<a href='/home'><button class='secondary'>Home Page</button></a>.</p>";
    }
    page += "</div>";

    page += getFooter();
    server.send(404, "text/html", page);
}
