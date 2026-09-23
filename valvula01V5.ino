#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <RTClib.h>

// ... (Inclusiones existentes)
  #define EEPROM_I2C_ADDR 0x50  // Dirección I2C estándar del AT24C32
  #define CONFIG_MAGIC 0xA5     // Identificador para validar datos en EEPROM
  #define EEPROM_START_ADDR 0   // Dirección de inicio en la EEPROM

  #define I2C_SDA 27
  #define I2C_SCL 22
  #define SEALEVELPRESSURE_HPA (1013.25)

// Objetos para los sensores
RTC_DS1307 rtc;
Adafruit_BME280 bme;

// --- CONFIGURACIÓN HARDWARE ---
const int PIN_RELAY = 26; // Pin conectado al relay HL-51
const int ip_final = 2;   // <--- CAMBIAR ESTO para cada ESP32 (2, 3, 4 o 5)

// --- ESTRUCTURAS ---
struct BloqueRiego {
    bool activo;
    uint8_t sh, sm, eh, em;
    uint32_t on_ms, off_ms;
    float h_min, h_max; // Agregar esto
    float t_min, t_max; // Agregar esto
};


BloqueRiego bloques[4];
//ESP32Time rtc;
WebServer server(80);

// Función para escribir un byte en la EEPROM AT24C32
void writeEEPROM(uint16_t address, byte data) {
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write((int)(address >> 8));   // MSB (Byte alto de la dirección)
    Wire.write((int)(address & 0xFF)); // LSB (Byte bajo de la dirección)
    Wire.write(data);
    Wire.endTransmission();
    delay(5); // Tiempo necesario para el ciclo de escritura física
}

// Función para leer un byte de la EEPROM AT24C32
byte readEEPROM(uint16_t address) {
    byte rdata = 0xFF;
    Wire.beginTransmission(EEPROM_I2C_ADDR);
    Wire.write((int)(address >> 8));
    Wire.write((int)(address & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom(EEPROM_I2C_ADDR, 1);
    if (Wire.available()) rdata = Wire.read();
    return rdata;
}

void guardarConfigEEPROM() {
    uint16_t addr = EEPROM_START_ADDR;
    writeEEPROM(addr++, CONFIG_MAGIC); // Escribir marca de validación
    
    byte* p = (byte*)(void*)&bloques; // Puntero al array de estructuras
    for (int i = 0; i < sizeof(bloques); i++) {
        writeEEPROM(addr++, *p++);
    }
    Serial.println("Configuración guardada en EEPROM AT24C32.");
}

void cargarConfigEEPROM() {
    uint16_t addr = EEPROM_START_ADDR;
    if (readEEPROM(addr++) != CONFIG_MAGIC) {
        Serial.println("EEPROM vacía o inválida. Usando valores por defecto.");
        return;
    }

    byte* p = (byte*)(void*)&bloques;
    for (int i = 0; i < sizeof(bloques); i++) {
        *p++ = readEEPROM(addr++);
    }
    Serial.println("Configuración cargada desde EEPROM AT24C32.");
}


void handleUpdateConfig() {
    StaticJsonDocument<1536> doc; // Aumentamos un poco el tamaño por las nuevas variables
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
        Serial.print(F("Error en JSON: "));
        Serial.println(error.f_str());
        server.send(400, "text/plain", "JSON Error");
        return;
    }

    // Actualizar RTC Tiny
    rtc.adjust(DateTime(doc["yy"], doc["mo"], doc["dd"], doc["hh"], doc["mm"], doc["ss"]));

    // Cargar bloques
    JsonArray blqs = doc["bloques"];
    for(int i = 0; i < 4; i++) {
        bloques[i].activo = blqs[i]["a"];
        
        bloques[i].sh = blqs[i]["sh"];
        bloques[i].sm = blqs[i]["sm"];
        bloques[i].eh = blqs[i]["eh"];
        bloques[i].em = blqs[i]["em"];

        // --- NUEVAS VARIABLES: Captura de permisivos ---
        bloques[i].h_min = blqs[i]["h_min"];
        bloques[i].h_max = blqs[i]["h_max"];
        bloques[i].t_min = blqs[i]["t_min"];
        bloques[i].t_max = blqs[i]["t_max"];

        // Debug de las nuevas variables
        Serial.printf("Bloque %d -> H_Min: %.1f, H_Max: %.1f, T_Min: %.1f, T_Max: %.1f\n", 
                      i, bloques[i].h_min, bloques[i].h_max, bloques[i].t_min, bloques[i].t_max);

        // Convertir tiempos a ms (Tu lógica existente)
        bloques[i].on_ms = (blqs[i]["on_h"].as<uint32_t>() * 3600 + 
                    blqs[i]["on_m"].as<uint32_t>() * 60 + 
                    blqs[i]["on_s"].as<uint32_t>()) * 1000;

        bloques[i].off_ms = (blqs[i]["off_h"].as<uint32_t>() * 3600 + 
                     blqs[i]["off_m"].as<uint32_t>() * 60 + 
                     blqs[i]["off_s"].as<uint32_t>()) * 1000;
    }
    
    // Guardar en EEPROM tras actualizar variables
    guardarConfigEEPROM();
    server.send(200, "text/plain", "OK");
}

void handleGetSensors() {
    // Aquí leerías los sensores reales (BME280/DHT22)
    StaticJsonDocument<200> doc;
    doc["t"] = bme.readTemperature(); // Placeholder
    doc["h"] = bme.readHumidity();
    doc["p"] = bme.readPressure() / 100.0F;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleGetConfig() {
    StaticJsonDocument<1024> doc; // Buffer para los 4 bloques
    JsonArray arr = doc.createNestedArray("bloques");
    
    for(int i = 0; i < 4; i++) {
        JsonObject b = arr.createNestedObject();
        b["a"] = bloques[i].activo;
        b["sh"] = bloques[i].sh;
        b["sm"] = bloques[i].sm;
        b["eh"] = bloques[i].eh;
        b["em"] = bloques[i].em;
        b["on"] = bloques[i].on_ms;
        b["off"] = bloques[i].off_ms;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void verificarRiego() {
    DateTime now = rtc.now();
    int currentH = now.hour();
    int currentM = now.minute();
    bool valvulaEncendida = false;

    // 1. Lectura de sensores BME280 para los permisivos
    float tempActual = bme.readTemperature();
    float humActual = bme.readHumidity();

    for(int i=0; i<4; i++) {
        if(!bloques[i].activo) continue;
        
        // 2. Verificar Permisivos Agrometeorológicos
        // Solo permite el riego si la humedad es menor al máximo configurado
        // Y si la temperatura es mayor al mínimo configurado
        bool permisivosOK = (humActual < bloques[i].h_max) && (tempActual > bloques[i].t_min);

        if (permisivosOK) {
            // 3. Verificar si estamos dentro del rango horario
            long currentTotalMin = currentH * 60 + currentM;
            long startTotalMin = bloques[i].sh * 60 + bloques[i].sm;
            long endTotalMin = bloques[i].eh * 60 + bloques[i].em;

            if(currentTotalMin >= startTotalMin && currentTotalMin < endTotalMin) {
                // 4. Lógica de ciclo T_On / T_Off usando millis()
                unsigned long cicloTotal = bloques[i].on_ms + bloques[i].off_ms;
                
                // Evitar división por cero si no se han configurado tiempos
                if (cicloTotal > 0) {
                    if (millis() % cicloTotal < bloques[i].on_ms) {
                        valvulaEncendida = true;
                    }
                }
            }
        }
    }

    // 5. Accionar el relé (Lógica inversa: LOW enciende, HIGH apaga)
    digitalWrite(PIN_RELAY, valvulaEncendida ? LOW : HIGH); 
}


void setup() {
    Serial.begin(115200);
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH); // Relay usualmente OFF (si es nivel bajo)

    // Conexión a CYD
    WiFi.mode(WIFI_STA);
    WiFi.begin("INVERNADERO_CYD", "SISTEMA_RIEGO_2025");
    
    // IP Estática
    IPAddress local_IP(192, 168, 4, ip_final);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.config(local_IP, gateway, subnet);

    while (WiFi.status() != WL_CONNECTED) { delay(500); }

    // Endpoints
    server.on("/update_config", HTTP_POST, handleUpdateConfig);
    server.on("/get_sensors", HTTP_GET, handleGetSensors);
    server.on("/getConfig", HTTP_GET, handleGetConfig);
    server.begin();

    // Inicializa el bus I2C
    Wire.begin(I2C_SDA, I2C_SCL); // ESP32 usa pines específicos

    // Inicializa el RTC DS1307
    if (!rtc.begin()) {
    Serial.println("No se encontró el RTC DS1307!");
    while (1); // Detener si no se encuentra
    }

    // Cargar configuración guardada inmediatamente después de iniciar I2C
    cargarConfigEEPROM();

    // Inicializa el sensor BME280
    if (!bme.begin(0x76)) { // Dirección I2C común, cambia si usas 0x77
    Serial.println("No se encontró el BME280!");
    while (1);
    }

    Serial.println("Sensores I2C inicializados correctamente.");

}

void loop() {
    server.handleClient();
    verificarRiego();
}
