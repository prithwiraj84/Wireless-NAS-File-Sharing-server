#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>

const char* apSSID = "ESP32-AP";
const char* apPassword = "password";

WebServer server(80);
#define CS_PIN 5
File uploadFile;

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    Serial.print("Uploading: "); Serial.println(filename);
    uploadFile = SD.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("Upload Complete.");
    } else {
      Serial.println("Upload Failed.");
    }
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.softAP(apSSID, apPassword);
  Serial.println("WiFi AP mode started.");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  if (!SD.begin(CS_PIN)) {
    Serial.println("Card Mount Failed");
    delay(1000);
    ESP.restart();
  }
  Serial.println("SD Card initialized.");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/list", HTTP_GET, listFiles);
  server.on("/upload", HTTP_POST, []() { server.send(200); }, handleFileUpload);
  server.on("/download", HTTP_GET, handleFileDownload);
  server.on("/storage", HTTP_GET, handleStorageInfo);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();
  delay(1);
}

void handleRoot() {
  String page = "<!DOCTYPE html><html><head><style>";
  page += "body{font-family:Arial;margin:20px;text-align:center;}";
  page += "h2{color:#333;}button{padding:10px 20px;margin:10px;background:#007bff;color:#fff;cursor:pointer;}";
  page += "button:hover{background:#0056b3;}table{width:80%;margin:auto;border-collapse:collapse;}th,td{border:1px solid #ddd;padding:8px;text-align:left;}th{background:#007bff;color:#fff;}progress{width:80%;height:20px;}";
  page += "</style></head><body><h2>ESP32 NAS Server</h2>";
  page += "<p><button onclick=\"location.href='/list'\">List Files</button></p>";
  page += "<p><button onclick=\"location.href='/storage'\">Storage Info</button></p>";
  page += "<p><input type='file' id='fileUpload'><button onclick='uploadFile()'>Upload File</button></p>";
  page += "<progress id='progressBar' value='0' max='100'></progress>";
  page += "<script>function uploadFile(){var file=document.getElementById('fileUpload').files[0];if(!file){alert('Select a file first!');return;}";
  page += "var formData=new FormData();formData.append('file',file);";
  page += "var xhr=new XMLHttpRequest();xhr.open('POST','/upload?name='+encodeURIComponent(file.name),true);xhr.upload.onprogress=function(e){if(e.lengthComputable){var percent=(e.loaded/e.total)*100;document.getElementById('progressBar').value=percent;}};";
  page += "xhr.onload=function(){if(xhr.status==200){alert('File uploaded!');location.reload();}else{alert('Upload failed!');}};xhr.send(formData);}</script></body></html>";
  server.send(200, "text/html", page);
}

void listFiles() {
  File root = SD.open("/");
  String page = "<h2>File List</h2><table><tr><th>File Name</th><th>Actions</th></tr>";
  File file = root.openNextFile();
  while (file) {
    String filename = String(file.name());
    page += "<tr><td>" + filename + "</td>";
    page += "<td><a href='/download?file=" + filename + "'><button>Download</button></a></td></tr>";
    file.close();
    file = root.openNextFile();
  }
  page += "</table><br><button onclick=\"location.href='/'\">Back</button>";
  server.send(200, "text/html", page);
}

void handleFileDownload() {
  String filename = server.arg("file");
  if (filename.isEmpty()) {
    server.send(400, "text/plain", "File not specified");
    return;
  }
  File file = SD.open("/" + filename);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  server.sendHeader("Content-Type", "application/octet-stream");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Connection", "close");
  server.streamFile(file, "application/octet-stream");
  file.close();
}

void handleStorageInfo() {
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;
  
  String response = "<h2>Storage Info</h2>";
  response += "<p>Total: " + String(totalBytes / (1024 * 1024)) + " MB</p>";
  response += "<p>Used: " + String(usedBytes / (1024 * 1024)) + " MB</p>";
  response += "<p>Free: " + String(freeBytes / (1024 * 1024)) + " MB</p>";
  response += "<br><button onclick=\"location.href='/'\">Back</button>";
  server.send(200, "text/html", response);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}
