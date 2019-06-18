/*
 *
 *
 * http://minifiedjs.com/
 * http://mincss.com/
 *
 */

void handleRoot() {
  // TODO: Make this a static page that loads values through the API
  digitalWrite(LED_PIN, HIGH);
  g_bat_volt = analogRead(BAT_PIN) / BAT_SCALE;
  Serial << "Handling root request...";
  String resp = "<!DOCTYPE html><html lang='en'><head><title>EEG Trigger</title><meta charset='utf-8' />\n";
  resp += (String)"<meta name='viewport' content='width=device-width, initial-scale=1' />\n";
  resp += (String)"<link rel='stylesheet' href='min.min.css' />\n";
  resp += (String)"<link rel='shortcut icon' href='favicon.ico' type='image/x-icon' />\n";
  resp += (String)"</head>\n<body>\n";
  resp += (String)"<h2>EEG Trigger</h2>\n";
  resp += (String)"<table class='table table-borderless'>\n";
  resp += (String)"<tbody>\n";
  resp += (String)"<tr><td>Wifi connected to SSID:</td><td>" + WiFi.SSID() + "</td></tr>\n";
  resp += (String)"<tr><td>Wifi signal strength:</td><td>" + WiFi.RSSI() + " dBm</td></tr>\n";
  resp += (String)"<tr><td>Battery voltage:</td><td>" + g_bat_volt + " V</td></tr>\n";
  char buf[20];
  g_unix_epoch_ms = timeClient.getEpochTimeMillisUTC();
  sprintf(buf, "%d-%02d-%02d %02d:%02d:%02d", year(), month(), day(), dst_hour(year(), month(), day(), hour()), minute(), second());
  resp += (String)"<tr><td>Current time:</td><td>" + buf + " (epoch: " + (uint32_t)(g_unix_epoch_ms/1000L) + "." + (uint32_t)(g_unix_epoch_ms%1000L) + ")</td></tr>\n";
  resp += (String)"</tbody>\n";
  resp += (String)"</table>\n";
  resp += (String)"<div class='row'>";
  resp += (String)"<form action='/' id='config'>\n";
  resp += (String)"<span class='addon'>SSID:</span><input type='text' id='ssid' class='smooth' value='" + g_ssid + "'>\n";
  resp += (String)"<span class='addon'>Password:</span><input type='text' id='passwd' class='smooth' value='" + g_passwd + "'>\n";
  resp += (String)"<span class='addon'>Pulse Duration (usec):</span><input type='text' id='pulseMicros' class='smooth' value='" + g_pulseMicros + "'>\n";
  resp += (String)"<span class='addon'>Logical on:</span><input type='text' id='logicOn' class='smooth' value='" + g_logicOn + "'>\n";
  resp += (String)"<span class='addon'>Analog out flag:</span><input type='text' id='analogOut' class='smooth' value='" + g_analogOut + "'>\n";
  resp += (String)"<span class='addon'>Touch threshold:</span><input type='text' id='touchThresh' class='smooth' value='" + g_touchThresh + "'>\n";
  resp += (String)"</form>\n";
  resp += (String)"</div>";
  resp += (String)"<script src='minified-web.js.gz'></script>\n";
  resp += (String)"<script>\nvar MINI=require('minified');\nvar $=MINI.$,$$=MINI.$$,EE=MINI.EE;\n";
  resp += (String)"$('#config').submit(function(e){e.preventDefault();\n$.request('post','/config',$.toJSON($('#config').values()))}).then(function(r){});\n";
  resp += (String)"</script>\n";
  resp += (String)"</body>\n</html>\n";
  server->send(200, "text/html", resp);
  Serial << "Finished." << endl;
  digitalWrite(LED_PIN, LOW);
}

void handleUri() {
  digitalWrite(LED_PIN, HIGH);
  if(loadFromSpiffs(server->uri())){
    Serial << "Serving up " + server->uri() << endl;
  } else {
    String resp = "File Not Found\n\n";
    String meth = (server->method() == HTTP_GET)?"GET":"POST";
    resp += (String)"URI: " + server->uri() + "<br>Method: " + meth + "<br>Arguments: ";
    resp += server->args();
    resp += "<br>";
    for (uint8_t i=0; i<server->args(); i++){
      resp += (String)" " + server->argName(i) + ": " + server->arg(i) + "<br>";
    }
    server->send(404, "text/html", resp);
  }
  digitalWrite(LED_PIN, LOW);
}

void handleGetTimestamp() {
  g_unix_epoch_ms = timeClient.getEpochTimeMillisUTC();
  // TODO: return timestamp in milliseconds (uint64)
  //String resp = (String)"{\"timestamp\":" + g_unix_epoch_ms + "}";
  String resp = (String)"{\"timestamp\":" + (uint32_t)(g_unix_epoch_ms/1000L) + "." + (uint32_t)(g_unix_epoch_ms%1000L) + "}";
  server->send(100, "application/json", resp);
  Serial << "Getting timestamp" << endl;
}

void handlePostTimestamp() {
  // Logic:
  // Get the current unix time for the trigger device (me)
  // Send the pulse out asap
  // Write the log with the two timestamps
  server->sendHeader("Access-Control-Allow-Origin","*");
  g_unix_epoch_ms = timeClient.getEpochTimeMillisUTC();
  String buffer = server->arg("plain");
  DynamicJsonDocument doc(128);
  DeserializationError error = deserializeJson(doc, buffer);
  if (error) {
    Serial << "POST timestamp: error parsing json body. Error code: " << error.c_str() << endl;
    server->send(400);
    return;
  }

  // TODO: time the lag here. Parsing JSON might take some time!
  uint64_t ts = doc["timestamp"];
  String tstring = String((uint32_t)(ts/1000)) + "." + String((uint32_t)(ts%1000)) + "," + String((uint32_t)(g_unix_epoch_ms/1000)) + "." + String((uint32_t)(g_unix_epoch_ms%1000)) + "\n";
  appendToLog(tstring);
  Serial << "Posting timestamp" << endl;
  pulseOut((byte)(ts % 255 + 1));
  //server->sendHeader("Location", (String)"/v1/timestamp/");
  //resp = send_timestamp((int)jsonBody["timestamp"])
  server->send(201);
}

void handleGetLog() {
  // WORK HERE
  g_unix_epoch_ms = timeClient.getEpochTimeMillisUTC();
  String resp = (String)"{\"timestamp\":" + (uint32_t)(g_unix_epoch_ms/1000L) + "." + (uint32_t)(g_unix_epoch_ms%1000L) + "}";
  server->send(100, "application/json", resp);
}

void handleDeleteLog() {
  // WORK HERE
  // Rotate the logfile. I.e., delete it and start a new one.
  SPIFFS.remove("/log.csv");
  server->send(201);
}

void handlePostEvent() {
  // WORK HERE
  // Ingest an event from the phone
  g_unix_epoch_ms = timeClient.getEpochTimeMillisUTC();
  String buffer = server->arg("plain");
  DynamicJsonDocument doc(buffer.length()+1);
  DeserializationError error = deserializeJson(doc, buffer);
  if (error) {
    Serial << "POST event: error parsing json body. Error code: " << error.c_str() << endl;
    server->send(400);
    return;
  }
  uint64_t upload_ts = doc["timestamp"];
  uint64_t event_ts;
  String event_str = doc["event"];
  int cnt, idx=-1;
  while(cnt<6) {
    idx = event_str.indexOf("\t", idx+1);
    if(idx > -1)
      cnt++;
    else
      break;
  }
  if(cnt==6) {
    idx++; // Skip the \t
    int tokend = event_str.indexOf("\t", idx); // Find the next \t (the end of the timestamp token)
    if(tokend > -1) { // If tokend is a valid index, then we've found an intact timestamp!
      // String.toInt can only convert long, not int64 (long long). So we need to use atoll.
      event_ts = atoll(event_str.substring(idx, tokend).c_str());
    } else {
      Serial << "POST event: error parsing event_ts from '" << event_str << "'" << endl;
      server->send(400);
      return;
    }
  }
  pulseOut((byte)(event_ts % 255 + 1));
  //server->sendHeader("Location", (String)"/v1/timestamp/");
  //resp = send_timestamp((int)jsonBody["timestamp"])
  server->send(201);
}

void handleGetConfig() {
  if (SPIFFS.exists("/config.json")) {
    File file = SPIFFS.open("/config.json", "r");
    size_t sent = server->streamFile(file, "application/json");
    file.close();
  } else {
    Serial.println("Config file not found");
    server->send(404);
  }
  //server->send(200, "application/json", resp);
}

void handlePostConfig() {
  if(setConfigFromJson(server->arg("plain"))) {
    server->sendHeader("Location", (String)"/config/");
    if(saveConfig()) {
      Serial << "Config saved." << endl;
      server->send(201);
    } else {
      Serial << "FAILED to save config!" << endl;
      server->send(200);
    }
  } else {
    Serial << "error parsing json" << endl;
    server->send(400);
  }
}

// Configuration utils
bool setConfigFromJson(String buffer) {
  //DynamicJsonBuffer jsonBuffer(buffer.length()+1);
  //JsonObject jsonBody = jsonBuffer.parseObject(buffer);
  //DynamicJsonDocument doc(buffer.length()+2);
  // With typical values, the config json doc is about 120 bytes, so 256 should be plenty until the config grows.
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, buffer);
  if(error){
    Serial << "setConfig: error parsing json body. Error code: " << error.c_str() << endl;
    return false;
  }
  Serial << "setConfigFromJson: json doc used " << doc.memoryUsage() << " bytes." << endl;
  g_ssid = doc["ssid"] | g_ssid;
  g_passwd = doc["passwd"] | g_passwd;
  g_pulseMicros = doc["pulseMicros"] | g_pulseMicros;
  g_logicOn = doc["logicOn"] | g_logicOn;
  g_analogOut = doc["analogOut"] | g_analogOut;
  g_touchThresh = doc["touchThresh"] | g_touchThresh;
  return true;
}

bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial << "Failed to open config file" << endl;
    return false;
  }

  size_t size = configFile.size();
  if (size > 2048) {
    Serial << "Config file size is too large" << endl;
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  configFile.readBytes(buf.get(), size);

  if(!setConfigFromJson(String(buf.get()))) {
    Serial << "Failed to parse config file" << endl;
    return false;
  }
  return true;
}

bool saveConfig() {
  //DynamicJsonBuffer jsonBuffer(512);
  //JsonObject json = jsonBuffer.createObject();
  DynamicJsonDocument doc(512);
  doc["ssid"] = g_ssid;
  doc["passwd"] = g_passwd;
  doc["pulseMicros"] = g_pulseMicros;
  doc["logicOn"] = g_logicOn;
  doc["analogOut"] = g_analogOut;
  doc["touchThresh"] = g_touchThresh;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  serializeJson(doc, configFile);
  serializeJsonPretty(doc, Serial);
  Serial << endl;
  return true;
}

bool appendToLog(String msg){
  File logFile = SPIFFS.open("/log.csv", "a");
  if (!logFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  logFile.print(msg);
  return true;
}

bool loadFromSpiffs(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";

  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".html")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";

  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz)) path = pathWithGz;

  File dataFile = SPIFFS.open(path.c_str(), "r");
  if (server->hasArg("download")) dataType = "application/octet-stream";
  server->sendHeader("Cache-Control", "public, max-age=31536000");
  if (server->streamFile(dataFile, dataType) != dataFile.size()) {
  }

  dataFile.close();
  return true;
}

bool configMode(){
  bool try_connect = false;
  WiFi.mode(WIFI_AP_STA);
  std::unique_ptr<DNSServer> dnsServer;
  dnsServer.reset(new DNSServer());
  server.reset(new WebServer(80));
  // NOTE: this will launch the AP in open (unsecured) mode. Add a second argument to softAP to set a WPA passphrase.
  WiFi.softAP("ephys_trigger");
  delay(500);
  Serial << "AP mode enabled on IP: " << WiFi.softAPIP() << endl;
  /* Setup the DNS server on port 53, redirecting all the domains to the apIP */
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(53, "*", WiFi.softAPIP());
  server->on("/", HTTP_GET, handleGetConfig);
  server->on("/config", HTTP_POST, [&try_connect](){
    handlePostConfig();
    // if we got a new config, try connecting...
    try_connect = true;
  });
  server->onNotFound(handleUri);
  server->begin();
  Serial << "Config server started..." << endl;
  while(1){
    // check if timeout
    // if(configPortalHasTimeout()) break;
    dnsServer->processNextRequest();
    server->handleClient();
    if (try_connect) {
      try_connect = false;
      delay(2000);
      Serial << "Connecting to new AP" << endl;
      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      if (WiFi.begin(g_ssid.c_str(), g_passwd.c_str()) != WL_CONNECTED) {
        Serial << "Failed to connect." << endl;
      } else {
        //connected
        WiFi.mode(WIFI_STA);
        break;
      }
    }
    yield();
  }
  server.reset(new WebServer(80));
  dnsServer.reset();
  return WiFi.status() == WL_CONNECTED;
}

bool startServer(){
  server.reset(new WebServer(80));
  server->on("/timestamp", HTTP_OPTIONS, []() {
    server->sendHeader("Access-Control-Max-Age", "10000");
    server->sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
    server->send(200, "application/json", "" );
  });
  server->on("/", HTTP_GET, handleRoot);
  server->on("/config", HTTP_GET, handleGetConfig);
  server->on("/config", HTTP_POST, handlePostConfig);
  server->on("/timestamp", HTTP_GET, handleGetTimestamp);
  server->on("/timestamp", HTTP_POST, handlePostTimestamp);
  server->on("/log", HTTP_GET, handleGetLog);
  server->on("/log", HTTP_DELETE, handleDeleteLog);
  server->on("/event", HTTP_POST, handlePostEvent);
  server->on("/inline", HTTP_GET, [](){
    server->send(200, "text/html", "inline example");
  });

  // Any other route will try to serve up a file from SPFFS
  server->onNotFound(handleUri);
  //server.serveStatic("/", SPIFFS, "/","max-age=31536000");

  server->begin();
  Serial << "Server started..." << endl;
}
