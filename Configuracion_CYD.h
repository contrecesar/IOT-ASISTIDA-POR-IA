// Configuracion_CYD.h
#ifndef CONFIGURACION_CYD_H
#define CONFIGURACION_CYD_H

// Inclusiones de librerías esenciales
#include <TFT_eSPI.h>       // Para el manejo de la pantalla TFT
#include <XPT2046_Bitbang.h>// Para el manejo del controlador táctil XPT2046
#include <ESP32Time.h>      // Para el reloj de tiempo real (RTC)
#include <WiFi.h>           // Funcionalidad WiFi básica
#include <AsyncTCP.h>       // Requerido por el servidor web asíncrono
#include <ESPAsyncWebSrv.h> // Para el servidor web de configuración
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <RTClib.h> // NUEVA: Librería para el RTC DS1307 
#include <Wire.h>   // Para comunicación I2C 
// -------------------------------------------------------------------
// 1. PINES DE LA PANTALLA TÁCTIL (¡IMPORTANTE! Respetar las definiciones del usuario)
// -------------------------------------------------------------------
// Estos pines se usan para la comunicación bitbang SPI con el XPT2046
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// -------------------------------------------------------------------
// 2. CONSTANTES DE PANTALLA Y COLORES
// -------------------------------------------------------------------
#define TFT_WIDTH 320
#define TFT_HEIGHT 240
#define MARGIN 10
#define FONT_SIZE 1 // Tamaño de fuente para la mayoría de los textos

// Colores personalizados
#define TFT_NAVY 0x000F      // Azul marino oscuro
#define TFT_DARK_GRAY 0x7BEF // Gris oscuro
#define TFT_ORANGE 0xFD20    // Naranja brillante
#define TFT_GREEN_LIGHT 0x47E0 // Verde claro (para estados ON)
#define TFT_KEYPAD_BG 0xC618 // NUEVO: Fondo de teclado/mensajes (Gris claro)

// --- COLORES DE BOTONES (NUEVAS DECLARACIONES) ---
#define TFT_BUTTON_OFF 0xAD75 // Color por defecto del botón (Gris azulado claro)
#define TFT_BUTTON_ON  0x5AEB // Color cuando el botón está presionado o seleccionado (Azul medio)

// --- CONSTANTES DE GEOMETRÍA DEL TECLADO VIRTUAL (NUEVAS DECLARACIONES) ---
#define KEYPAD_BTN_W 45 // Ancho del botón del teclado
#define KEYPAD_BTN_H 30 // Alto del botón del teclado
#define KEYPAD_GAP   6  // Espacio entre botones
// Posición inicial del teclado para centrarlo horizontalmente: 
// (320 - (3 * 55 + 4 * 8)) / 2 = (320 - 197) / 2 = 61.5 -> 62
#define KEYPAD_X_START 160 
// Posición inicial del teclado para dejar espacio para el título y el campo de entrada
#define KEYPAD_Y_START 140 

// -------------------------------------------------------------------
// 3. / NUEVAS DEFINICIONES PARA COMUNICACIÓN WIFI ---
// -------------------------------------------------------------------

#define WIFI_SSID "INVERNADERO_CYD"
#define WIFI_PASS "SISTEMA_RIEGO_2025"

// IPs fijas para los 4 ESP32 (el CYD es 192.168.4.1)
extern const char* esp32_ips[4];

// -------------------------------------------------------------------
// 4. / NUEVAS DEFINICIONES I2C PARA RT---
// -------------------------------------------------------------------

#define I2C_SDA 27 // Según requerimiento y valvula01_2.ino 
#define I2C_SCL 22 // Según requerimiento y valvula01_2.ino 

extern RTC_DS1307 rtc_ext; // Declaración del objeto RTC externo


// -------------------------------------------------------------------
// 5. ESTRUCTURAS DE DATOS PARA LA CONFIGURACIÓN
// -------------------------------------------------------------------

// Define los posibles estados (pantallas) de la interfaz de usuario
enum Pantalla {
    SCREEN_MAIN_MENU,
    SCREEN_SET_TIME,
    SCREEN_CONFIG_VALVE_SELECT, // Pantalla de selección Válvula/Bloque
    SCREEN_CONFIG_VALVE_EDIT,   // Pantalla de edición de tiempos
    SCREEN_MONITOR_VALVE,
    SCREEN_CONFIG_HUM_TEMP
};

// Define los campos seleccionables en la pantalla de configuración de hora y fecha
enum SetTimeField {
    FIELD_NONE,
    FIELD_DAY,
    FIELD_MONTH,
    FIELD_YEAR, 
    FIELD_HOUR,
    FIELD_MINUTE
};

// Estructura para definir un botón en la pantalla
struct Boton {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    const char* label;
    uint16_t color;
    uint8_t id;
    uint8_t fontSize;
};

// Estructura para definir el horario de inicio y fin y los ciclos de riego/descanso

// NUEVA: Estructura para definir un bloque de riego
struct BloqueRiego {
    bool activo;            // true si el bloque está activo, false si no
    uint8_t startHour;      // Hora de inicio del bloque (0-23)
    uint8_t startMinute;    // Minuto de inicio del bloque (0-59)
    uint8_t endHour;        // Hora de fin del bloque (0-23)
    uint8_t endMinute;      // Minuto de fin del bloque (0-59)
    // Tiempos de Ciclo de Riego (HH:MM:SS)
    uint8_t onHour;         // Tiempo encendido - Horas (0-23)
    uint8_t onMinute;       // Tiempo encendido - Minutos (0-59)
    uint8_t onSecond;       // Tiempo encendido - Segundos (0-59)
    uint8_t offHour;        // Tiempo apagado - Horas (0-23)
    uint8_t offMinute;      // Tiempo apagado - Minutos (0-59)
    uint8_t offSecond;      // Tiempo apagado - Segundos (0-59)
    float h_min, h_max;     // Permisivo humdad
    float t_min, t_max;     // Permisivo tiempo
};

// Estructura para la configuración completa de una válvula
// MODIFICACIÓN: Estructura para la configuración completa de una válvula
struct ConfigValvula {
    uint8_t id;             // ID de la válvula (1 a 4)
    bool operativa;         // true si la válvula está activa/operativa
    BloqueRiego bloques[4]; // Hasta 4 bloques de riego
    float temp_aire;        // Temperatura actual del aire
    float humedad_relativa; // Humedad relativa actual
    float presion;          // Presión barométrica actual
    char estado[15];        // Estado de conexión (ej. "Conectado", "Desconectado")
};


// -------------------------------------------------------------------
// 6. DECLARACIÓN DE INSTANCIAS GLOBALES (Definidas en Funciones_CYD.cpp)
// -------------------------------------------------------------------
extern TFT_eSPI tft;
extern XPT2046_Bitbang ts;
extern ESP32Time rtc; // Reloj de tiempo real

// -------------------------------------------------------------------
// 5. DECLARACIÓN DE VARIABLES DE ESTADO (Definidas en Funciones_CYD.cpp)
// -------------------------------------------------------------------
extern Pantalla currentScreen;
extern bool screenNeedsUpdate;

extern ConfigValvula configuraciones[4]; // Array con la configuración de las 4 válvulas
extern uint8_t valvulaSeleccionadaIdx;  // Índice de la válvula seleccionada (0 a 3)
extern uint8_t bloqueSeleccionadoIdx;   // NUEVA: Índice del bloque seleccionado (0 a 3)

// Índices para los 10 campos de tiempo en la pantalla de edición de válvulas:
// 0=InicioBloque H, 1=InicioBloque M, 2=FinBloque H, 3=FinBloque M,
// 4=T_On H, 5=T_On M, 6=T_On S, 7=T_Off H, 8=T_Off M, 9=T_Off S
extern uint8_t tiempoSeleccionadoIdx;


// NUEVA: Para manejar el campo seleccionado en la configuración de hora/fecha
extern SetTimeField setTimeFieldSelected; 

// Para la pantalla de teclado: valor actual de entrada
extern char inputBuffer[5];
extern uint8_t inputIndex;
extern bool configuracionGuardada; // Flag para indicar que se guardó la config

// --- Añadir en la Sección 5. VARIABLES DE ESTADO (Globales) ---
enum InputTarget {
    TARGET_NONE,
    TARGET_RTC_HH, TARGET_RTC_MM, TARGET_RTC_DD, TARGET_RTC_MO, TARGET_RTC_YY,
    TARGET_VALVE_H_INI, TARGET_VALVE_M_INI, TARGET_VALVE_H_FIN, TARGET_VALVE_M_FIN, 
    TARGET_VALVE_H_ON, TARGET_VALVE_M_ON, TARGET_VALVE_S_ON,TARGET_VALVE_H_OFF, 
    TARGET_VALVE_M_OFF,TARGET_VALVE_S_OFF, 
    TARGET_H_MIN, TARGET_H_MAX,
    TARGET_T_MIN, TARGET_T_MAX
};

extern InputTarget currentTarget; // Qué estamos editando
extern bool keypadActive;         // ¿Está el teclado visible?

// -------------------------------------------------------------------
// 7. PROTOTIPOS DE FUNCIONES (Funciones del módulo)
// -------------------------------------------------------------------

// Inicialización
void initializeWiFi(); // En Programa_CYD.ino

// Pantallas (Dibujo)
void drawMainMenu();
void drawSetTimeScreen();
void drawConfigValveSelectScreen(); // MODIFICADO (Nueva pantalla de selección)
void drawConfigValveEditScreen();   // NUEVO (Pantalla de edición)
void drawMonitorValveScreen(uint8_t valveIdx);
void drawVirtualKeypad();
void drawVirtualKeypad2();

// Manejo de Interacciones (Toques)
void handleTouch(int16_t x, int16_t y);
void handleMainMenuTouch(int16_t x, int16_t y);
void handleSetTimeTouch(int16_t x, int16_t y);
void handleConfigValveSelectTouch(int16_t x, int16_t y); // MODIFICADO
void handleConfigValveEditTouch(int16_t x, int16_t y);   // NUEVO
void handleMonitorValveTouch(int16_t x, int16_t y);
void handleKeypadTouch(int16_t x, int16_t y);

// Helpers
void dibujarBoton(const Boton& boton, bool seleccionado = false);
void showVirtualKeypad(int16_t maxVal);
void updateSelectedTimeValue();
void dibujarTextoCentrado(int16_t x, int16_t y, int16_t w, const char* text, uint16_t fgColor, uint16_t bgColor);
// Nuevos Prototipos
void solicitarDatosESP32(uint8_t idx);
bool enviarConfiguracionHaciaESP32(uint8_t idx);
void drawConfigHumTempScreen();
void handleConfigHumTempTouch(int16_t x, int16_t y);


#endif // CONFIGURACION_CYD_H