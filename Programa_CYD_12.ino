// Programa_CYD.ino (Módulo Principal)
#include "Configuracion_CYD.h"
#include <SPI.h> // Se requiere SPI para TFT_eSPI/XPT2046

RTC_DS1307 rtc_ext; // Instancia del RTC externo

// -------------------------------------------------------------------
// FUNCIONES DE INICIALIZACIÓN
// -------------------------------------------------------------------
void initializeWiFi() {
    Serial.println("Configurando CYD como Punto de Acceso...");
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("IP del Punto de Acceso: ");
    Serial.println(IP);
}

void setup() {
    Serial.begin(115200);
    delay(100);

// Inicialización I2C con los pines especificados 
    Wire.begin(I2C_SDA, I2C_SCL);
    
    if (!rtc_ext.begin()) {
        Serial.println("No se encontró el RTC externo");
    }

    // 1. Inicialización de la pantalla TFT
    tft.init();
    tft.setRotation(3); // Rotación 180° (320x240)
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(FONT_SIZE);
    
    // 2. Inicialización del TouchScreen
    // Nota: XPT2046_Bitbang usa la implementación de software SPI con los pines definidos.
    ts.begin();
    // Calibración (utilizando los valores comunes para 320x240, ajusta si es necesario)
    // Estos valores son esenciales para mapear correctamente el toque.
    ts.setCalibration(3800, 300, 3700, 250);

    // 3. Inicialización del reloj RTC (Simulación/Placeholder)
    // Sincronizar el RTC con una hora inicial
    rtc.setTime(30, 0, 10, 15, 11, 2025); // (seg, min, hora, dia, mes, año)
    
    // 4. Inicialización del sistema WiFi y servidor web (Placeholder)
    initializeWiFi();
    
    // El estado inicial de la pantalla se establece en Configuracion_CYD.h
    // y la función drawMainMenu se ejecutará en el primer loop.
    currentScreen = SCREEN_MAIN_MENU;
    screenNeedsUpdate = true;
    Serial.println("Configuracion inicial completada. Listo para interactuar.");
}

// -------------------------------------------------------------------
// BUCLE PRINCIPAL
// -------------------------------------------------------------------
void loop() {
    // 1. Dibujado de pantallas
    if (screenNeedsUpdate) {
        if (keypadActive) {
            drawVirtualKeypad();
        } else {
            switch (currentScreen) {
                case SCREEN_MAIN_MENU: drawMainMenu(); break;
                case SCREEN_SET_TIME: drawSetTimeScreen(); break;
                case SCREEN_CONFIG_VALVE_SELECT: drawConfigValveSelectScreen(); break;
                case SCREEN_CONFIG_VALVE_EDIT: drawConfigValveEditScreen(); break;
                case SCREEN_MONITOR_VALVE: drawMonitorValveScreen(valvulaSeleccionadaIdx); break;
                case SCREEN_CONFIG_HUM_TEMP: drawConfigHumTempScreen(); break;
            }
        }
        screenNeedsUpdate = false;
    }

    // 2. Lectura del Touch
    TouchPoint touch = ts.getTouch();
    if (touch.zRaw > 200) {
        int16_t touchX = touch.x;
        int16_t touchY = touch.y;

        if (keypadActive) {
            handleKeypadTouch(touchX, touchY);
        } else {
            handleTouch(touchX, touchY);
        }
        
        while (ts.getTouch().zRaw > 200) delay(50);
    }
}