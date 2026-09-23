// Funciones_CYD.cpp
#include "Configuracion_CYD.h"
#include <SPI.h> // Necesario para TFT_eSPI
#include <string.h> // Para funciones de string
#include <stdlib.h> // Para atoi

// -------------------------------------------------------------------
// 1. DEFINICIÓN DE INSTANCIAS Y VARIABLES GLOBALES
// -------------------------------------------------------------------

// Instancias requeridas por el usuario
TFT_eSPI tft = TFT_eSPI();
XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);
ESP32Time rtc;

// Variables globales de estado
Pantalla currentScreen = SCREEN_MAIN_MENU;
bool screenNeedsUpdate = true;

// Inicialización de los bloques de riego a valores por defecto (ej. todo inactivo/cero)
const BloqueRiego bloqueDefault = {false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Inicialización de la configuración de las 4 válvulas (ACTUALIZADA con la nueva estructura)
ConfigValvula configuraciones[4] = {
    {1, false, {bloqueDefault, bloqueDefault, bloqueDefault, bloqueDefault}, 0.0, 0.0, 0.0, "Desconectado"},
    {2, false, {bloqueDefault, bloqueDefault, bloqueDefault, bloqueDefault}, 0.0, 0.0, 0.0, "Desconectado"},
    {3, false, {bloqueDefault, bloqueDefault, bloqueDefault, bloqueDefault}, 0.0, 0.0, 0.0, "Desconectado"},
    {4, false, {bloqueDefault, bloqueDefault, bloqueDefault, bloqueDefault}, 0.0, 0.0, 0.0, "Desconectado"}
};

uint8_t valvulaSeleccionadaIdx = 0; // Por defecto: Válvula 2
uint8_t bloqueSeleccionadoIdx = 0;  // NUEVA: Por defecto: Bloque 1
uint8_t tiempoSeleccionadoIdx = 0;  // Por defecto: 0



bool configuracionGuardada = false;

// *** NUEVAS DEFINICIONES PARA CONFIGURAR HORA/FECHA ***
SetTimeField setTimeFieldSelected = FIELD_NONE;
// Inicializar con valores de Placeholder (DD/MM/AAAA HH:MM)
char dayStr[3] = "15";
char monthStr[3] = "11";
char yearStr[5] = "2025";
char hourStr[3] = "10";
char minuteStr[3] = "30";

// --- Al inicio, definir las nuevas variables globales ---
InputTarget currentTarget = TARGET_NONE;
bool keypadActive = false;
char inputBuffer[5] = "";
uint8_t inputIndex = 0;

const char* esp32_ips[4] = {
    "192.168.4.2", 
    "192.168.4.3", 
    "192.168.4.4", 
    "192.168.4.5"
};


// -------------------------------------------------------------------
// 2. IMPLEMENTACIÓN DE FUNCIONES AUXILIARES (HELPERS)
// -------------------------------------------------------------------

bool enviarConfiguracionHaciaESP32(uint8_t idx) {
    if (!configuraciones[idx].operativa) return false;

    HTTPClient http;
    char url[60];
    sprintf(url, "http://%s/update_config", esp32_ips[idx]);
    
    StaticJsonDocument<1024> doc;
    // Datos del modulo RTC 
    DateTime now = rtc_ext.now();
    doc["hh"] = now.hour();
    doc["mm"] = now.minute();
    doc["ss"] = now.second();
    doc["dd"] = now.day();
    doc["mo"] = now.month();
    doc["yy"] = now.year();

    // Bloques de riego
    JsonArray blqs = doc.createNestedArray("bloques");
    for(int i=0; i<4; i++){
        JsonObject b = blqs.createNestedObject();
        b["a"] = configuraciones[idx].bloques[i].activo;
        b["sh"] = configuraciones[idx].bloques[i].startHour;
        b["sm"] = configuraciones[idx].bloques[i].startMinute;
        b["eh"] = configuraciones[idx].bloques[i].endHour;
        b["em"] = configuraciones[idx].bloques[i].endMinute;
        b["on_h"] = configuraciones[idx].bloques[i].onHour;
        b["on_m"] = configuraciones[idx].bloques[i].onMinute;
        b["on_s"] = configuraciones[idx].bloques[i].onSecond;
        b["off_h"] = configuraciones[idx].bloques[i].offHour;
        b["off_m"] = configuraciones[idx].bloques[i].offMinute;
        b["off_s"] = configuraciones[idx].bloques[i].offSecond;
        b["h_min"] = configuraciones[idx].bloques[i].h_min;
        b["h_max"] = configuraciones[idx].bloques[i].h_max;
        b["t_min"] = configuraciones[idx].bloques[i].t_min;
        b["t_max"] = configuraciones[idx].bloques[i].t_max;
    }


    String requestBody;
    serializeJson(doc, requestBody);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(requestBody);
    http.end();
    
    return (httpResponseCode == 200);
}

void solicitarDatosESP32(uint8_t idx) {
    if (!configuraciones[idx].operativa) {
        strcpy(configuraciones[idx].estado, "Inactiva");
        return;
    }

    HTTPClient http;
    char url[60];
    sprintf(url, "http://%s/get_sensors", esp32_ips[idx]);
    
    http.begin(url);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode == 200) {
        String payload = http.getString();
        StaticJsonDocument<200> doc;
        deserializeJson(doc, payload);
        configuraciones[idx].temp_aire = doc["t"];
        configuraciones[idx].humedad_relativa = doc["h"];
        configuraciones[idx].presion = doc["p"];
        strcpy(configuraciones[idx].estado, "Conectado");
    } else {
        strcpy(configuraciones[idx].estado, "Error Link");
    }
    http.end();
}

// Muestra un mensaje temporal en la parte inferior de la pantalla
void showMessage(const char* message, uint16_t color) {
    // Definición del área del mensaje
    const int16_t messageY = TFT_HEIGHT - MARGIN - 20;
    const int16_t messageH = 20;
    const int16_t messageW = TFT_WIDTH - 2 * MARGIN;

    // Borrar el área anterior del mensaje y dibujar el fondo
    tft.fillRect(MARGIN, messageY, messageW, messageH, TFT_KEYPAD_BG);
    tft.setTextColor(color, TFT_KEYPAD_BG);
    tft.setTextSize(FONT_SIZE);
    
    // Centrar el texto
    int16_t xCenter = MARGIN + messageW / 2;
    int16_t yCenter = messageY + (messageH / 2) - (4 * FONT_SIZE); // Ajuste vertical
    tft.drawCentreString(message, xCenter, yCenter, 2);
}

// Dibuja un botón en la pantalla
void dibujarBoton(const Boton& boton, bool seleccionado) {
uint16_t btnColor = seleccionado ? TFT_ORANGE : boton.color;
    uint16_t txtColor = seleccionado ? TFT_BLACK : TFT_WHITE;

    tft.fillRoundRect(boton.x, boton.y, boton.w, boton.h, 5, btnColor);
    tft.drawRoundRect(boton.x, boton.y, boton.w, boton.h, 5, TFT_WHITE);
    tft.setTextColor(txtColor);
    tft.setTextDatum(MC_DATUM); // Centro-Medio
    tft.drawString(boton.label, boton.x + boton.w / 2, boton.y + boton.h / 2, boton.fontSize);
}

// Dibuja el teclado virtual numérico
void drawVirtualKeypad2() {
    // Dibujar el fondo del teclado
    tft.fillRect(KEYPAD_X_START - KEYPAD_GAP, KEYPAD_Y_START - KEYPAD_GAP,
                 3 * KEYPAD_BTN_W + 4 * KEYPAD_GAP, 4 * KEYPAD_BTN_H + 5 * KEYPAD_GAP, TFT_KEYPAD_BG);

    // Definición de botones del teclado
    Boton keypadButtons[] = {
        // Fila 1 (7, 8, 9)
        {KEYPAD_X_START + 0 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 0 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "7", TFT_BUTTON_OFF, '7', 1},
        {KEYPAD_X_START + 1 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 0 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "8", TFT_BUTTON_OFF, '8', 1},
        {KEYPAD_X_START + 2 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 0 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "9", TFT_BUTTON_OFF, '9', 1},

        // Fila 2 (4, 5, 6)
        {KEYPAD_X_START + 0 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 1 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "4", TFT_BUTTON_OFF, '4', 1},
        {KEYPAD_X_START + 1 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 1 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "5", TFT_BUTTON_OFF, '5', 1},
        {KEYPAD_X_START + 2 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 1 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "6", TFT_BUTTON_OFF, '6', 1},

        // Fila 3 (1, 2, 3)
        {KEYPAD_X_START + 0 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 2 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "1", TFT_BUTTON_OFF, '1', 1},
        {KEYPAD_X_START + 1 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 2 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "2", TFT_BUTTON_OFF, '2', 1},
        {KEYPAD_X_START + 2 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 2 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "3", TFT_BUTTON_OFF, '3', 1},

        // Fila 4 (<- , 0, OK)
        {KEYPAD_X_START + 0 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 3 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "<-", TFT_NAVY, 10, 1},
        {KEYPAD_X_START + 1 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 3 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "0", TFT_BUTTON_OFF, '0', 1},
        {KEYPAD_X_START + 2 * (KEYPAD_BTN_W + KEYPAD_GAP), KEYPAD_Y_START + 3 * (KEYPAD_BTN_H + KEYPAD_GAP), KEYPAD_BTN_W, KEYPAD_BTN_H, "OK", TFT_GREEN, 11, 1}
    };

    for (const auto& btn : keypadButtons) {
        // Resaltar OK si hay algo en el buffer
        bool highlightOK = (btn.id == 11 && setTimeFieldSelected != FIELD_NONE && inputIndex > 0);
        dibujarBoton(btn, highlightOK);
    }
}



// --- Implementación del Teclado Virtual ---
void drawVirtualKeypad() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("INGRESE VALOR", TFT_WIDTH / 2, 10, 2);

    // Recuadro del valor actual
    tft.drawRect(TFT_WIDTH / 2 - 40, 35, 80, 30, TFT_WHITE);
    tft.drawCentreString(inputBuffer, TFT_WIDTH / 2, 40, 4);

    // Dibujar botones 1-9
    int btnW = 60;
    int btnH = 35;
    int startX = 60;
    int startY = 75;

    for (int i = 0; i < 9; i++) {
        int row = i / 3;
        int col = i % 3;
        char label[2] = {(char)('1' + i), '\0'};
        Boton b = {startX + (col * (btnW + 5)), startY + (row * (btnH + 5)), btnW, btnH, "", TFT_BLUE, (uint8_t)(1 + i), 2};
        dibujarBoton(b);
        tft.drawCentreString(label, b.x + btnW / 2, b.y + 10, 2);
    }

    // Botones especiales: 0, DEL, OK
    Boton b0 = {startX + (1 * (btnW + 5)), startY + (3 * (btnH + 5)), btnW, btnH, "0", TFT_BLUE, 0, 2};
    dibujarBoton(b0);
    
    Boton bDel = {startX, startY + (3 * (btnH + 5)), btnW, btnH, "DEL", TFT_RED, 10, 2};
    dibujarBoton(bDel);

    Boton bOk = {startX + (2 * (btnW + 5)), startY + (3 * (btnH + 5)), btnW, btnH, "OK", TFT_GREEN, 11, 2};
    dibujarBoton(bOk);
}

void handleKeypadTouch(int16_t x, int16_t y) {
    int btnW = 60;
    int btnH = 35;
    int startX = 60;
    int startY = 75;

    for (int i = 0; i < 12; i++) {
        int row = i / 3;
        int col = i % 3;
        int bx = startX + (col * (btnW + 5));
        int by = startY + (row * (btnH + 5));

        if (x > bx && x < bx + btnW && y > by && y < by + btnH) {
            if (i < 9 || i == 10) { // Números 1-9 y 0
                char key = (i < 9) ? (char)('1' + i) : '0';
                if (inputIndex < 4) {
                    inputBuffer[inputIndex++] = key;
                    inputBuffer[inputIndex] = '\0';
                }
            } else if (i == 9) { // Botón DEL
                if (inputIndex > 0) {
                    inputBuffer[--inputIndex] = '\0';
                }
            } else if (i == 11) { // Botón OK
                int val = atoi(inputBuffer);
                auto& bloque = configuraciones[valvulaSeleccionadaIdx].bloques[bloqueSeleccionadoIdx];
                
                // Asignar valor según el objetivo
                switch (currentTarget) {
                    case TARGET_VALVE_H_INI: bloque.startHour = val; break;
                    case TARGET_VALVE_M_INI: bloque.startMinute = val; break;
                    case TARGET_VALVE_H_FIN:  bloque.endHour = val; break;
                    case TARGET_VALVE_M_FIN:  bloque.endMinute = val; break;
                    case TARGET_VALVE_H_ON: bloque.onHour = val; break;
                    case TARGET_VALVE_M_ON: bloque.onMinute = val; break;
                    case TARGET_VALVE_S_ON:  bloque.onSecond = val; break;
                    case TARGET_VALVE_H_OFF:  bloque.offHour = val; break;
                    case TARGET_VALVE_M_OFF:  bloque.offMinute = val; break;
                    case TARGET_VALVE_S_OFF:  bloque.offSecond = val; break;
                    
                    // --- NUEVOS CASOS PARA HUMEDAD Y TEMPERATURA ---
                    case TARGET_H_MIN: bloque.h_min = (float)val; break;
                    case TARGET_H_MAX: bloque.h_max = (float)val; break;
                    case TARGET_T_MIN: bloque.t_min = (float)val; break;
                    case TARGET_T_MAX: bloque.t_max = (float)val; break;

                    case TARGET_RTC_DD: strncpy(dayStr, inputBuffer, 2); dayStr[2] = '\0'; break;
                    case TARGET_RTC_MO: strncpy(monthStr, inputBuffer, 2); monthStr[2] = '\0'; break;
                    case TARGET_RTC_YY: strncpy(yearStr, inputBuffer, 4); yearStr[4] = '\0'; break;
                    case TARGET_RTC_HH: strncpy(hourStr, inputBuffer, 2); hourStr[2] = '\0'; break;
                    case TARGET_RTC_MM: strncpy(minuteStr, inputBuffer, 2); minuteStr[2] = '\0'; break;
                }
                keypadActive = false;
                screenNeedsUpdate = true;
                inputIndex = 0;   // Resetear el índice del buffer para la próxima vez
                inputBuffer[0] = '\0';
                return;
            }
            drawVirtualKeypad(); 
            delay(150);
        }
    }
}

// Actualiza el texto de la fecha y hora en la pantalla
void updateDateTimeDisplay() {
    tft.setTextSize(1); // Fuente grande para la fecha/hora

    // Coordenadas iniciales para la Fecha
    int16_t xDate = 30;
    int16_t yDate = 40;

    // Borrar el área de la fecha y hora para evitar artefactos
   // tft.fillRect(xDate - 5, yDate - 5, 170, 50, TFT_BLACK);
    
    // DD (Día)
    uint16_t colorDay = (setTimeFieldSelected == FIELD_DAY) ? TFT_ORANGE : TFT_WHITE;
    tft.setTextColor(colorDay, TFT_BLACK);
    tft.drawString(dayStr, xDate, yDate, 2);

    // Separador /
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("/", xDate + 15, yDate, 2);

    // MM (Mes)
    uint16_t colorMonth = (setTimeFieldSelected == FIELD_MONTH) ? TFT_ORANGE : TFT_WHITE;
    tft.setTextColor(colorMonth, TFT_BLACK);
    tft.drawString(monthStr, xDate + 35, yDate, 2);

    // Separador /
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("/", xDate + 55, yDate, 2);

    // AAAA (Año)
    uint16_t colorYear = (setTimeFieldSelected == FIELD_YEAR) ? TFT_ORANGE : TFT_WHITE;
    tft.setTextColor(colorYear, TFT_BLACK);
    tft.drawString(yearStr, xDate + 90, yDate, 2);


    // Coordenadas iniciales para la Hora
    int16_t xTime = xDate + 30;
    int16_t yTime = 60;

    // HH (Hora)
    uint16_t colorHour = (setTimeFieldSelected == FIELD_HOUR) ? TFT_ORANGE : TFT_WHITE;
    tft.setTextColor(colorHour, TFT_BLACK);
    tft.drawString(hourStr, xTime, yTime, 2);

    // Separador :
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(":", xTime + 20, yTime, 2);

    // MM (Minuto)
    uint16_t colorMinute = (setTimeFieldSelected == FIELD_MINUTE) ? TFT_ORANGE : TFT_WHITE;
    tft.setTextColor(colorMinute, TFT_BLACK);
    tft.drawString(minuteStr, xTime + 30, yTime, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK); // Restaurar color por defecto

    // Mostrar el buffer de entrada temporalmente debajo de la hora/fecha si hay entrada
    int16_t feedbackY = yDate + 50;
   // tft.fillRect(xDate, feedbackY - 5, 50, 10, TFT_BLACK);
    if (inputIndex > 0) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawCentreString(inputBuffer, xDate + 25, feedbackY+50, 1);
    }
}


// Procesa la entrada de un dígito o comandos del teclado
void processKeypadInput(char key) {
    if (key >= '0' && key <= '9') {
        uint8_t requiredLength = 0;
        if (setTimeFieldSelected == FIELD_YEAR) {
            requiredLength = 4;
        } else if (setTimeFieldSelected != FIELD_NONE) {
            requiredLength = 2;
        }

        if (requiredLength > 0 && inputIndex < requiredLength) {
            inputBuffer[inputIndex++] = key;
            inputBuffer[inputIndex] = '\0'; // Terminador nulo
            showMessage("Ingresando...", TFT_YELLOW);
        } else if (requiredLength > 0 && inputIndex == requiredLength) {
             showMessage("Presione OK para confirmar.", TFT_BLUE);
        } else {
             showMessage("Seleccione un campo (Dia/Mes/...).", TFT_RED);
        }
    } else if (key == '<') { // Botón de retroceso
        if (inputIndex > 0) {
            inputBuffer[--inputIndex] = '\0';
            showMessage("Retroceso.", TFT_YELLOW);
        } else {
            showMessage("Buffer de entrada vacio.", TFT_RED);
        }
    } else if (key == 'O') { // Botón OK
        if (setTimeFieldSelected != FIELD_NONE) {
            uint8_t requiredLength = (setTimeFieldSelected == FIELD_YEAR) ? 4 : 2;

            if (inputIndex == requiredLength) {
                // Se ha completado la entrada, mover al string de destino
                if (setTimeFieldSelected == FIELD_DAY) {
                    strncpy(dayStr, inputBuffer, 2);
                    dayStr[2] = '\0';
                } else if (setTimeFieldSelected == FIELD_MONTH) {
                    strncpy(monthStr, inputBuffer, 2);
                    monthStr[2] = '\0';
                } else if (setTimeFieldSelected == FIELD_YEAR) {
                    strncpy(yearStr, inputBuffer, 4);
                    yearStr[4] = '\0';
                } else if (setTimeFieldSelected == FIELD_HOUR) {
                    strncpy(hourStr, inputBuffer, 2);
                    hourStr[2] = '\0';
                } else if (setTimeFieldSelected == FIELD_MINUTE) {
                    strncpy(minuteStr, inputBuffer, 2);
                    minuteStr[2] = '\0';
                }
                
                showMessage("Valor guardado. Presione SET para confirmar.", TFT_GREEN);
                // Limpiar buffer
                inputIndex = 0;
                inputBuffer[0] = '\0';
            } else {
                char msg[50];
                sprintf(msg, "Error: Necesita %d digitos. Solo tiene %d.", requiredLength, inputIndex);
                showMessage(msg, TFT_RED);
            }
        } else {
            showMessage("Seleccione un campo primero.", TFT_RED);
        }
    }
    screenNeedsUpdate = true; // Forzar redibujado para mostrar el cambio
}

// Funciones_CYD.cpp

// ... [El resto de tus funciones implementadas (handle/draw)] ...

// -------------------------------------------------------------------
// 7. FUNCIONES DE INTERFAZ (TECLADO VIRTUAL y Helpers)
// -------------------------------------------------------------------

/**
 * @brief Muestra el teclado virtual para ingresar un valor numérico.
 * * Esta es una versión simplificada. La implementación completa
 * debe manejar la visualización del teclado, el manejo de toques
 * en los números, el botón 'Enter'/'Guardar' y la actualización
 * de la variable global inputBuffer.
 *
 * @param maxVal Valor máximo permitido para la entrada (ej. 60 para minutos/segundos, 24 para horas)
 */
 
void showVirtualKeypad(int16_t maxVal) {
    // NOTE: Esta función toma el control de la pantalla para dibujar el teclado.
    // El 'loop' principal debe ser modificado para manejar los toques del teclado
    // y la actualización de los campos.

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Ingreso de Valor", TFT_WIDTH / 2, MARGIN, 2);
    tft.drawFastHLine(MARGIN, 30, TFT_WIDTH - 2 * MARGIN, TFT_WHITE);
    
    // Indica el límite del valor
    char limit[30];
    sprintf(limit, "Max: %d", maxVal - 1);
    tft.drawCentreString(limit, TFT_WIDTH / 2, 40, 2);

    // Dibuja el campo de entrada (usando el buffer global)
    char bufferDisplay[6];
    strncpy(bufferDisplay, inputBuffer, inputIndex);
    bufferDisplay[inputIndex] = '_'; // Cursor
    bufferDisplay[inputIndex + 1] = '\0';
    tft.drawCentreString(bufferDisplay, TFT_WIDTH / 2, 70, 4);

    // Lógica para dibujar los botones del teclado virtual (1-9, 0, <-, OK) iría aquí.

    // Poner un flag de estado si la aplicación usa un estado de pantalla específico para el teclado.
    // Por simplicidad, asumimos que el handleTouch se adaptará temporalmente.
    
    // No se pone 'screenNeedsUpdate = false' porque esta pantalla debe ser interactiva.
}




// -------------------------------------------------------------------
// 3. PANTALLAS (DRAW FUNCTIONS)
// -------------------------------------------------------------------

// Dibuja la pantalla de configuración de Hora/Fecha
void drawSetTimeScreen() {

// Al entrar, si no estamos editando, cargamos la hora del RTC externo 
    if (setTimeFieldSelected == FIELD_NONE) {
        DateTime now = rtc_ext.now();
        sprintf(dayStr, "%02d", now.day());
        sprintf(monthStr, "%02d", now.month());
        sprintf(yearStr, "%04d", now.year());
        sprintf(hourStr, "%02d", now.hour());
        sprintf(minuteStr, "%02d", now.minute());
    }

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("1. Configurar Hora/Fecha", MARGIN, 5, 2);

    // Botón de Menú
    Boton menuBtn = {TFT_WIDTH - 60, MARGIN, 50, 30, "Menu", TFT_DARK_GRAY, 99, 1};
    dibujarBoton(menuBtn);

    // 1. Botones de selección de campo (Día, Mes, Año, Hora, Minuto)
    int16_t btnY = 90;
    int16_t btnW = 55;
    int16_t btnH = 35;
    int16_t btnSpacing = 20;
    int16_t startX = 100;

    // Botón Día
    Boton btnDay = {startX, btnY, btnW, btnH, "Dia", TFT_BUTTON_OFF, 1, 1};
    dibujarBoton(btnDay, setTimeFieldSelected == FIELD_DAY);

    // Botón Mes
    Boton btnMonth = {startX, btnY + btnH + btnSpacing, btnW, btnH, "Mes", TFT_BUTTON_OFF, 2, 1};
    dibujarBoton(btnMonth, setTimeFieldSelected == FIELD_MONTH);

    // Botón Año
    Boton btnYear = {startX , btnY + 2 * (btnH + btnSpacing), btnW + 10, btnH, "Anio", TFT_BUTTON_OFF, 3, 1};
    dibujarBoton(btnYear, setTimeFieldSelected == FIELD_YEAR);

    // Botón Hora
    Boton btnHour = {startX + btnW + 15, btnY , btnW, btnH, "Hora", TFT_BUTTON_OFF, 4, 1};
    dibujarBoton(btnHour, setTimeFieldSelected == FIELD_HOUR);

    // Botón Minuto
    Boton btnMinute = {startX + btnW + 15, btnY + btnH + btnSpacing, btnW, btnH, "Min", TFT_BUTTON_OFF, 5, 1};
    dibujarBoton(btnMinute, setTimeFieldSelected == FIELD_MINUTE);

    // 2. Display de la Fecha y Hora y buffer de entrada
    updateDateTimeDisplay();

    // 3. Botón SET
    Boton btnSET = {startX + btnW + 15, btnY + 2 * (btnH + btnSpacing), btnW + 10, btnH, "SET", TFT_GREEN, 6, 1};
    dibujarBoton(btnSET);

    screenNeedsUpdate = false;
}

// -------------------------------------------------------------------
// 4. MANEJO DE TOQUES (TOUCH HANDLERS)
// -------------------------------------------------------------------

// Maneja los toques en la pantalla de Configurar Hora/Fecha
void handleSetTimeTouch(int16_t x, int16_t y) {

    // 1. Botón de Menú
    Boton menuBtn = {TFT_WIDTH - 60, MARGIN, 50, 30, "Menu", TFT_DARK_GRAY, 99, 2};
    if (x > menuBtn.x && x < menuBtn.x + menuBtn.w && y > menuBtn.y && y < menuBtn.y + menuBtn.h) {
        currentScreen = SCREEN_MAIN_MENU;
        setTimeFieldSelected = FIELD_NONE; // Reset state
        inputIndex = 0; // Limpiar buffer
        inputBuffer[0] = '\0';
        screenNeedsUpdate = true;
        return;
    }

    // 2. Botones de selección de campo y SET
    int16_t btnY = 90;
    int16_t btnW = 55;
    int16_t btnH = 35;
    int16_t btnSpacing = 20;
    int16_t startX = 100;

 

    // Usar la misma lista de botones que en drawSetTimeScreen
    Boton buttons[] = {
        {startX, btnY, btnW, btnH, "Dia", TFT_BUTTON_OFF, 1, 1}, // FIELD_DAY
        {startX, btnY + btnH + btnSpacing, btnW, btnH, "Mes", TFT_BUTTON_OFF, 2, 1}, // FIELD_MONTH
        {startX , btnY + 2 * (btnH + btnSpacing), btnW + 10, btnH, "Anio", TFT_BUTTON_OFF, 3, 1}, // FIELD_YEAR
        {startX + btnW + 15, btnY , btnW, btnH, "Hora", TFT_BUTTON_OFF, 4, 1}, // FIELD_HOUR
        {startX + btnW + 15, btnY + btnH + btnSpacing, btnW, btnH, "Min", TFT_BUTTON_OFF, 5, 1}, // FIELD_MINUTE
        {startX + btnW + 15, btnY + 2 * (btnH + btnSpacing), btnW + 10, btnH, "SET", TFT_GREEN, 6, 1} // SET
    };

    for (int i = 0; i < 6; i++) {
        Boton btn = buttons[i];
        if (x > btn.x && x < btn.x + btn.w && y > btn.y && y < btn.y + btn.h) {
          if (i < 5) { // Botones de selección de campo (Día a Minuto)
                setTimeFieldSelected = (SetTimeField)(i + 1);
                switch (i) {
                case 0: currentTarget = TARGET_RTC_DD; break;
                case 1: currentTarget = TARGET_RTC_MO; break;
                case 2: currentTarget = TARGET_RTC_YY; break;
                case 3: currentTarget = TARGET_RTC_HH; break;
                case 4: currentTarget = TARGET_RTC_MM; break;
                }
                keypadActive = true;
                inputIndex = 0;
                inputBuffer[0] = '\0';
                char msg[50];
                uint8_t requiredLength = (setTimeFieldSelected == FIELD_YEAR) ? 4 : 2;
                sprintf(msg, "Ingrese %d digitos para %s. OK para guardar.", requiredLength, btn.label);
                showMessage(msg, TFT_BLUE);
            } else { // Botón SET (i=5)
                // Acción de SET: Guardar la hora/fecha en el RTC
                uint8_t day = (uint8_t)atoi(dayStr);
                uint8_t month = (uint8_t)atoi(monthStr);
                uint16_t year = (uint16_t)atoi(yearStr);
                uint8_t hour = (uint8_t)atoi(hourStr);
                uint8_t minute = (uint8_t)atoi(minuteStr);
                if (day > 0 && day <= 31 && month > 0 && month <= 12 && year >= 2000 && hour < 24 && minute < 60) {
                    rtc_ext.adjust(DateTime(atoi(yearStr), atoi(monthStr), atoi(dayStr), atoi(hourStr), atoi(minuteStr), 0));
                    rtc.setTime(0, atoi(minuteStr), atoi(hourStr), atoi(dayStr), atoi(monthStr), atoi(yearStr));
                    showMessage("RTC Actualizado", TFT_GREEN);
                } else {
                    showMessage("Error: Datos de fecha/hora invalidos. Revise sus entradas.", TFT_RED);
                }
                setTimeFieldSelected = FIELD_NONE; // Desactivar selección
                inputIndex = 0; // Limpiar buffer
                inputBuffer[0] = '\0';
            }
            screenNeedsUpdate = true;
            return;
       }       
   }
}


// -------------------------------------------------------------------
// 5. OTRAS FUNCIONES REQUERIDAS
// -------------------------------------------------------------------

// Función de inicialización del sistema y variables
void inicializarSistema() {
    // Aquí se inicializarían los valores de los strings de fecha/hora si se leyeran
    // de una memoria persistente o del RTC por primera vez.
    // Como las variables globales ya tienen valores por defecto, no es necesario más.
    
    // Ejemplo de lectura inicial del RTC si ya estuviera configurado
    // rtc.getTimeStruct();
    // sprintf(dayStr, "%02d", rtc.getDay());
    // ...
}

// -------------------------------------------------------------------
// 6. FUNCIÓN GENERAL DE MANEJO DE TOQUE (DISPATCHER)
// -------------------------------------------------------------------

// Función principal para manejar el toque y redirigir
void handleTouch(int16_t x, int16_t y) {
    switch (currentScreen) {
        case SCREEN_MAIN_MENU:
            handleMainMenuTouch(x, y);
            break;
        case SCREEN_SET_TIME:
            handleSetTimeTouch(x, y);
            break;
        case SCREEN_CONFIG_VALVE_SELECT: 
            handleConfigValveSelectTouch(x, y);
            break;
        case SCREEN_CONFIG_VALVE_EDIT:   
            handleConfigValveEditTouch(x, y);
            break;
        case SCREEN_MONITOR_VALVE:
            handleMonitorValveTouch(x, y);
            break;
        case SCREEN_CONFIG_HUM_TEMP:   // <--- AGREGAR ESTA LÍNEA
            handleConfigHumTempTouch(x, y); // Llama a la función que creamos anteriormente
            break;
    }
}



Boton botonesMenu[] = {
    {MARGIN, 40, 300, 50, "1. Establecer Hora/Fecha", TFT_NAVY, 1,1},
    {MARGIN, 110, 300, 50, "2. Configurar Valvulas", TFT_NAVY, 2,1},
    {MARGIN, 180, 300, 50, "3. Monitorear Valvulas", TFT_NAVY, 4,1}
};
const int numBotonesMenu = 3;



// Implementaciones Placeholder para otras pantallas (Mantenidas)
void drawMainMenu() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(FONT_SIZE);
    tft.drawString(" - - Sistema de Riego - -", 20, 2, 4);

    for (int i = 0; i < numBotonesMenu; i++) {
        dibujarBoton(botonesMenu[i]);
    }

    screenNeedsUpdate = false;
}

void handleMainMenuTouch(int16_t x, int16_t y) {
    for (int i = 0; i < numBotonesMenu; i++) {
        if (x > botonesMenu[i].x && x < botonesMenu[i].x + botonesMenu[i].w &&
            y > botonesMenu[i].y && y < botonesMenu[i].y + botonesMenu[i].h) {
            // Lógica de navegación
            currentScreen = (Pantalla)botonesMenu[i].id;
            Serial.print("currentScreen: "); Serial.print(currentScreen);
            screenNeedsUpdate = true;
            break;
        }
    }
}



// 2. CONFIGURAR VALVULAS - Pantalla de Selección
void drawConfigValveSelectScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("2. Configurar Valvulas", TFT_WIDTH / 2, MARGIN, 3);
    tft.drawFastHLine(MARGIN, 30, TFT_WIDTH - 2 * MARGIN, TFT_WHITE);

    // --- Botones de Válvula (Selección y Estado Operativo) ---
    int btnW = 45;
    int btnH = 30;
    int spacing = 15;
    int startY = 40;
    tft.drawString("Valvulas:", MARGIN+20, startY + 15, 2);
    int valveX = MARGIN + 70;
    for (int i = 0; i < 4; i++) {
        char label[10];
        sprintf(label, "V %d", i + 1);
        
        // Color de fondo indica selección, color de texto indica estado operativo
        uint16_t textColor = configuraciones[i].operativa ? TFT_WHITE : TFT_RED;
        uint16_t bgColor = (i == valvulaSeleccionadaIdx) ? TFT_BLUE : TFT_DARK_GRAY;

        Boton btn = {valveX + i * (btnW + spacing), startY, btnW, btnH, label, bgColor, 10 + i, 2};
        dibujarBoton(btn, (i == valvulaSeleccionadaIdx));
    }
    
    // Toggle de estado operativo (Activar/Desactivar)
    Boton toggleBtn = {MARGIN, startY + btnH + 10, 100, 30, configuraciones[valvulaSeleccionadaIdx].operativa ? "Desactivar" : "Activar", TFT_ORANGE, 32, 2};
    dibujarBoton(toggleBtn);
    tft.drawString(configuraciones[valvulaSeleccionadaIdx].operativa ? "Estado: ACTIVA" : "Estado: NO ACTIVA", MARGIN + 190, startY + btnH + spacing + 10, 2);

    // Separador
    tft.drawFastHLine(MARGIN, startY + btnH + 45, TFT_WIDTH - 2 * MARGIN, TFT_WHITE);
    int blockY = startY + btnH + 55;

    // --- Botones de Bloque (Selección y Estado de Bloque) ---
    tft.drawString("Bloques:", MARGIN+20, blockY + 15, 2);
    int blockX = MARGIN + 70;
    for (int i = 0; i < 4; i++) {
        char label[10];
        sprintf(label, "B %d", i + 1);
        
        // Color de fondo indica si el bloque está activo
        uint16_t bgColor = configuraciones[valvulaSeleccionadaIdx].bloques[i].activo ? TFT_GREEN : TFT_DARK_GRAY;

        Boton btn = {blockX + i * (btnW + spacing), blockY, btnW, btnH, label, bgColor, 20 + i, 2};
        dibujarBoton(btn, (i == bloqueSeleccionadoIdx));
    }

    // --- Botones de Acción ---
    int actionY = blockY + btnH + spacing * 2-15;
    int actionW = (TFT_WIDTH - 4 * MARGIN) / 2;
    int smallH = 30;

    // Botón de Configurar
    Boton confBtn = {MARGIN, actionY, actionW, smallH, "Conf.", TFT_ORANGE, 30, 2};
    dibujarBoton(confBtn);

    // Botón de Enviar Configuración
    Boton sendBtn = {MARGIN + actionW + MARGIN +20, actionY, actionW, smallH, "Enviar Conf.", TFT_RED, 31, 2};
    dibujarBoton(sendBtn);

    // Botón de Menú (Abajo a la derecha)
    Boton menuBtn = {TFT_WIDTH - 60, TFT_HEIGHT - 35, 50, 30, "Menu", TFT_DARK_GRAY, 99, 2};
    dibujarBoton(menuBtn);
    
    screenNeedsUpdate = false;
}


// Mirar despues si esta función se elimina, con el buscador
void handleConfigValveTouch(int16_t x, int16_t y) {
    // Botón de Menú (ejemplo)
    Boton menuBtn = {TFT_WIDTH - 60, MARGIN, 50, 30, "Menu", TFT_DARK_GRAY, 99, 2};
    if (x > menuBtn.x && x < menuBtn.x + menuBtn.w && y > menuBtn.y && y < menuBtn.y + menuBtn.h) {
        currentScreen = SCREEN_MAIN_MENU;
        screenNeedsUpdate = true;
        return;
    }
}

// 2. CONFIGURAR VALVULAS - Manejo de Toque en Pantalla de Selección
void handleConfigValveSelectTouch(int16_t x, int16_t y) {
    int btnW = 45;
    int btnH = 30;
    int spacing = 15;
    int startY = 40;

    
    // 1. Botones de Válvula (IDs 10-13)
    int valveX = MARGIN + 70;
    if (y >= startY && y <= startY + btnH) {
        for (int i = 0; i < 4; i++) {
            int x1 = valveX + i * (btnW + spacing);
            int x2 = x1 + btnW;
            if (x >= x1 && x < x2) {
                valvulaSeleccionadaIdx = i;
                screenNeedsUpdate = true;
                return;
            }
        }
    }

    // Toggle Activar/Desactivar Válvula (ID 32)
    Boton toggleBtn = {MARGIN, startY + btnH + 10, 100, 30, "", TFT_ORANGE, 32, 2};
    if (x > toggleBtn.x && x < toggleBtn.x + toggleBtn.w && y > toggleBtn.y && y < toggleBtn.y + toggleBtn.h) {
        configuraciones[valvulaSeleccionadaIdx].operativa = !configuraciones[valvulaSeleccionadaIdx].operativa;
        Serial.print("valvulaSeleccionadaIdx: ");  Serial.println(valvulaSeleccionadaIdx);
        screenNeedsUpdate = true;
        return;
    }

    // 2. Botones de Bloque (IDs 20-23)
    int blockY = startY + btnH + 55;
    int blockX = MARGIN + 70;
    if (y >= blockY && y <= blockY + btnH) {
        for (int i = 0; i < 4; i++) {
            int x1 = blockX + i * (btnW + spacing);
            int x2 = x1 + btnW;
            if (x >= x1 && x < x2) {
                bloqueSeleccionadoIdx = i;
                screenNeedsUpdate = true;
                return;
            }
        }
    }

    // --- Botones de Acción ---
    int actionY = blockY + btnH + spacing * 2;
    int actionW = (TFT_WIDTH - 4 * MARGIN) / 2;
    int smallH = 30;

    // Botón de Configurar (ID 30)
    if (x > MARGIN && x < MARGIN + actionW && y > actionY && y < actionY + smallH) {
        currentScreen = SCREEN_CONFIG_VALVE_EDIT; // Transición a la pantalla de edición
        screenNeedsUpdate = true;
        return;
    }

    // Botón de Enviar Configuración (ID 31)
    if (x > MARGIN + actionW + MARGIN && x < TFT_WIDTH - MARGIN && y > actionY && y < actionY + smallH) {
        enviarConfiguracionHaciaESP32(valvulaSeleccionadaIdx); 
        screenNeedsUpdate = true;
        return;
    }
    
    // Botón de Menú (ID 99)
    Boton menuBtn = {TFT_WIDTH - 60, TFT_HEIGHT - 35, 50, 30, "Menu", TFT_DARK_GRAY, 99, 2};
    if (x > menuBtn.x && x < menuBtn.x + menuBtn.w && y > menuBtn.y && y < menuBtn.y + menuBtn.h) {
        currentScreen = SCREEN_MAIN_MENU;
        screenNeedsUpdate = true;
        return;
    }
}

// 2. CONFIGURAR VALVULAS - Pantalla de Edición de Tiempos
void drawConfigValveEditScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // Título dinámico
    char title[60];
    sprintf(title, "V %d - B %d: Tiempos", valvulaSeleccionadaIdx + 1, bloqueSeleccionadoIdx + 1);
    tft.drawCentreString(title, TFT_WIDTH / 2, MARGIN, 2);
    tft.drawFastHLine(MARGIN, 30, TFT_WIDTH - 2 * MARGIN, TFT_WHITE);
    
    BloqueRiego& currentBlock = configuraciones[valvulaSeleccionadaIdx].bloques[bloqueSeleccionadoIdx];

    // --- Fila Superior: Activar Bloque y Botón Hum_Temp ---
    int row1Y = 35;
    
    // Toggle Activo
    tft.drawString("Activo:", MARGIN+30, row1Y + 15, 2);
    Boton toggleBlockBtn = {MARGIN + 85, row1Y, 45, 25, currentBlock.activo ? "SI" : "NO", currentBlock.activo ? TFT_GREEN : TFT_RED, 40, 2};
    dibujarBoton(toggleBlockBtn);

    // NUEVO: Botón Hum_Temp (ID 70)
    // Lo ubicamos a la derecha para que sea fácil de ver
    Boton humTempBtn = {TFT_WIDTH - MARGIN - 90, row1Y, 90, 25, "Hum_Temp", TFT_MAGENTA, 70, 2};
    dibujarBoton(humTempBtn);

    // --- Definición de áreas y valores de tiempo ---
    int startY = row1Y + 35;
    int fieldH = 25;
    int timeW = 35; 
    
    int xOffsetLabel = MARGIN +70;
    int xOffsetH = MARGIN + 170;
    int xOffsetM = xOffsetH + timeW + 10;
    int xOffsetS = xOffsetM + timeW + 10;
    
    int yPos[] = {startY, startY + fieldH + MARGIN, startY + 2 * (fieldH + MARGIN), startY + 3 * (fieldH + MARGIN)};
    
    uint8_t* timeFields[] = {
        &currentBlock.startHour, &currentBlock.startMinute,
        &currentBlock.endHour, &currentBlock.endMinute,
        &currentBlock.onHour, &currentBlock.onMinute, &currentBlock.onSecond,
        &currentBlock.offHour, &currentBlock.offMinute, &currentBlock.offSecond
    };
    
    tft.drawString("Inicio Bloque (HH:MM):", xOffsetLabel, yPos[0] + 10, 2);
    tft.drawString("Fin Bloque (HH:MM):", xOffsetLabel-5, yPos[1] + 10, 2);
    tft.drawString("T_On (HH:MM:SS):", xOffsetLabel-10, yPos[2] + 10, 2);
    tft.drawString("T_Off (HH:MM:SS):", xOffsetLabel-10, yPos[3] + 10, 2);

    for (int i = 0; i < 10; i++) {
        char valStr[3];
        sprintf(valStr, "%02d", *timeFields[i]);
        int xPos, yCurrent;
        
        if (i < 2) { yCurrent = yPos[0]; xPos = (i == 0) ? xOffsetH : xOffsetM; }
        else if (i < 4) { yCurrent = yPos[1]; xPos = (i == 2) ? xOffsetH : xOffsetM; }
        else if (i < 7) { yCurrent = yPos[2]; xPos = (i == 4) ? xOffsetH : (i == 5 ? xOffsetM : xOffsetS); }
        else { yCurrent = yPos[3]; xPos = (i == 7) ? xOffsetH : (i == 8 ? xOffsetM : xOffsetS); }
        
        if (i == 0 || i == 2 || i == 4 || i == 5 || i == 7 || i == 8) {
             tft.drawString(":", xPos + timeW + 3, yCurrent + 5, 2);
        }

        uint16_t bgColor = (i == tiempoSeleccionadoIdx) ? TFT_BLUE : TFT_BLACK;
        Boton btn = {xPos, yCurrent, timeW, fieldH, valStr, bgColor, 50 + i, 2};
        dibujarBoton(btn, (i == tiempoSeleccionadoIdx));
    }
    
    int actionY = TFT_HEIGHT - 35;
    int actionW = (TFT_WIDTH - 4 * MARGIN) / 2;
    int actionH = 30;
    
    Boton backBtn = {MARGIN + 15, actionY, actionW, actionH, "Regresar", TFT_DARK_GRAY, 60, 2};
    dibujarBoton(backBtn);
    
    Boton saveBtn = {MARGIN + actionW + MARGIN + 15, actionY, actionW, actionH, "Enviar Conf.", TFT_GREEN, 61, 2};
    dibujarBoton(saveBtn);
    
    screenNeedsUpdate = false;
}

// 2. CONFIGURAR VALVULAS - Manejo de Toque en Pantalla de Edición
void handleConfigValveEditTouch(int16_t x, int16_t y) {
    BloqueRiego& currentBlock = configuraciones[valvulaSeleccionadaIdx].bloques[bloqueSeleccionadoIdx];
    
    int row1Y = 35;

    // 1. Botón Toggle Activo (ID 40)
    if (x > MARGIN + 85 && x < MARGIN + 85 + 45 && y > row1Y && y < row1Y + 25) {
        currentBlock.activo = !currentBlock.activo;
        screenNeedsUpdate = true;
        return;
    }

    // 2. NUEVO: Botón Hum_Temp (ID 70)
    if (x > (TFT_WIDTH - MARGIN - 90) && x < (TFT_WIDTH - MARGIN) && y > row1Y && y < row1Y + 25) {
        currentScreen = SCREEN_CONFIG_HUM_TEMP;
        screenNeedsUpdate = true;
        Serial.println("Abriendo Configuracion Humedad/Temperatura");
        return;
    }

    // --- Lógica de campos de tiempo ---
    int fieldH = 25;
    int startY = row1Y + 35;
    int timeW = 35;
    int xOffsetH = MARGIN + 170;
    int xOffsetM = xOffsetH + timeW + 10;
    int xOffsetS = xOffsetM + timeW + 10;

    struct FieldArea { int x, y; };
    FieldArea fields[10] = {
        {xOffsetH, startY}, {xOffsetM, startY},
        {xOffsetH, startY + fieldH + MARGIN}, {xOffsetM, startY + fieldH + MARGIN},
        {xOffsetH, startY + 2 * (fieldH + MARGIN)}, {xOffsetM, startY + 2 * (fieldH + MARGIN)}, {xOffsetS, startY + 2 * (fieldH + MARGIN)},
        {xOffsetH, startY + 3 * (fieldH + MARGIN)}, {xOffsetM, startY + 3 * (fieldH + MARGIN)}, {xOffsetS, startY + 3 * (fieldH + MARGIN)}
    };
    
    for (int i = 0; i < 10; i++) {
        if (x > fields[i].x && x < fields[i].x + timeW && y > fields[i].y && y < fields[i].y + fieldH) {
            tiempoSeleccionadoIdx = i; // Para resaltar el botón seleccionado
            switch (i) {
                case 0: currentTarget = TARGET_VALVE_H_INI; break;
                case 1: currentTarget = TARGET_VALVE_M_INI; break;
                case 2: currentTarget = TARGET_VALVE_H_FIN; break;
                case 3: currentTarget = TARGET_VALVE_M_FIN; break;
                case 4: currentTarget = TARGET_VALVE_H_ON; break;
                case 5: currentTarget = TARGET_VALVE_M_ON; break;
                case 6: currentTarget = TARGET_VALVE_S_ON; break;
                case 7: currentTarget = TARGET_VALVE_H_OFF; break;
                case 8: currentTarget = TARGET_VALVE_M_OFF; break;
                case 9: currentTarget = TARGET_VALVE_S_OFF; break;
            }
            keypadActive = true;
            inputIndex = 0;
            inputBuffer[0] = '\0';
            screenNeedsUpdate = true;
            return;
        }
    }

    // --- Botones de Acción ---
    int actionY = TFT_HEIGHT - 35;
    int actionW = (TFT_WIDTH - 4 * MARGIN) / 2;
    int actionH = 30;

    // Botón REGRESAR
    if (x > MARGIN + 15 && x < MARGIN + 15 + actionW && y > actionY && y < actionY + actionH) {
        currentScreen = SCREEN_CONFIG_VALVE_SELECT;
        screenNeedsUpdate = true;
        return;
    }

    // Botón ENVIAR CONF (Anterior GUARDAR)
    if (x > MARGIN + actionW + MARGIN + 15 && x < TFT_WIDTH - MARGIN && y > actionY && y < actionY + actionH) {
        // Solo enviamos si la válvula está activa (según tu requerimiento)
        if (currentBlock.activo) {
            tft.fillRect(0, actionY - 25, TFT_WIDTH, 20, TFT_BLACK);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawCentreString("Enviando a ESP32...", TFT_WIDTH / 2, actionY - 20, 2);
            
            if (enviarConfiguracionHaciaESP32(valvulaSeleccionadaIdx)) {
                tft.fillRect(0, actionY - 25, TFT_WIDTH, 20, TFT_BLACK);
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.drawCentreString("Configuracion Enviada!", TFT_WIDTH / 2, actionY - 20, 2);
            } else {
                tft.fillRect(0, actionY - 25, TFT_WIDTH, 20, TFT_BLACK);
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.drawCentreString("Error de Envio", TFT_WIDTH / 2, actionY - 20, 2);
            }
        } else {
            tft.drawCentreString("Bloque Inactivo - No enviado", TFT_WIDTH / 2, actionY - 20, 2);
        }
        
        delay(1200);
        currentScreen = SCREEN_CONFIG_VALVE_SELECT;
        screenNeedsUpdate = true;
        return;
    }
}

void drawMonitorValveScreen(uint8_t valveIdx) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // 1. Título y Botón de Menú
    char title[30];
    sprintf(title, "MONITOREO: VALVULA %d", valveIdx + 1);
    tft.drawCentreString(title, TFT_WIDTH / 2, 5, 2);
    
    Boton menuBtn = {TFT_WIDTH - 65, 5, 60, 25, "MENU", TFT_DARK_GRAY, 99, 1};
    dibujarBoton(menuBtn);

    // 2. Solicitar configuración real al ESP32 (Incluyendo Hum/Temp)
    char url[64];
    sprintf(url, "http://192.168.4.%d/getConfig", valveIdx + 2);
    
    HTTPClient http;
    http.begin(url);
    http.setTimeout(2000); 
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        StaticJsonDocument<1536> doc; // Aumentado para los nuevos datos
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            JsonArray arr = doc["bloques"];
            for (int i = 0; i < 4; i++) {
                configuraciones[valveIdx].bloques[i].activo = arr[i]["a"];
                configuraciones[valveIdx].bloques[i].startHour = arr[i]["sh"];
                configuraciones[valveIdx].bloques[i].startMinute = arr[i]["sm"];
                configuraciones[valveIdx].bloques[i].endHour = arr[i]["eh"];
                configuraciones[valveIdx].bloques[i].endMinute = arr[i]["em"];
                
                // NUEVO: Captura de Humedad y Temperatura desde el JSON del ESP32
                configuraciones[valveIdx].bloques[i].h_min = arr[i]["h_min"];
                configuraciones[valveIdx].bloques[i].h_max = arr[i]["h_max"];
                configuraciones[valveIdx].bloques[i].t_min = arr[i]["t_min"];
                configuraciones[valveIdx].bloques[i].t_max = arr[i]["t_max"];
                
                uint32_t on_s = arr[i]["on"].as<uint32_t>() / 1000;
                configuraciones[valveIdx].bloques[i].onHour = on_s / 3600;
                configuraciones[valveIdx].bloques[i].onMinute = (on_s % 3600) / 60;
                configuraciones[valveIdx].bloques[i].onSecond = on_s % 60;
                
                uint32_t off_s = arr[i]["off"].as<uint32_t>() / 1000;
                configuraciones[valveIdx].bloques[i].offHour = off_s / 3600;
                configuraciones[valveIdx].bloques[i].offMinute = (off_s % 3600) / 60;
                configuraciones[valveIdx].bloques[i].offSecond = off_s % 60;
            }
            strncpy(configuraciones[valveIdx].estado, "Sincronizado", 15);
        }
    } else {
        strncpy(configuraciones[valveIdx].estado, "Desconectado", 15);
    }
    http.end();

    // 3. Solicitar sensores actuales (BME280)
    sprintf(url, "http://192.168.4.%d/get_sensors", valveIdx + 2);
    http.begin(url);
    if (http.GET() == HTTP_CODE_OK) {
        String payload = http.getString();
        StaticJsonDocument<200> doc;
        deserializeJson(doc, payload);
        configuraciones[valveIdx].temp_aire = doc["t"];
        configuraciones[valveIdx].humedad_relativa = doc["h"];
        configuraciones[valveIdx].presion = doc["p"];
    }
    http.end();

    // 4. Dibujar Botones de Selección V1-V4
    for (int i = 0; i < 4; i++) {
        const char* labels[] = {"V1", "V2", "V3", "V4"};
        Boton bV_final = { (int16_t)(MARGIN + (i * 75)), 40, 70, 30, labels[i], TFT_BLUE, (uint8_t)i, 1};
        dibujarBoton(bV_final, (valveIdx == i));
        
        uint16_t colorEstado = (strcmp(configuraciones[i].estado, "Desconectado") != 0) ? TFT_GREEN : TFT_RED;
        tft.fillCircle(MARGIN + (i * 75) + 60, 45, 4, colorEstado);
    }

    // 5. Mostrar Bloques con Datos Agrometeorológicos
    for (int i = 0; i < 4; i++) {
        int currentY = 82 + (i * 33); // Ajuste leve de espaciado
        BloqueRiego bR = configuraciones[valveIdx].bloques[i];
        
        tft.setTextColor(bR.activo ? TFT_GREEN : TFT_DARKGREY);
        
        // Fila 1: Tiempos y Permisivos
        
        char buf[128];
        sprintf(buf, "B%d: %02d:%02d-%02d:%02d | H:%.0f-%.0f%% T:%.0f-%.0fC", 
                i+1, bR.startHour, bR.startMinute, bR.endHour, bR.endMinute,
                bR.h_min, bR.h_max, bR.t_min, bR.t_max);
        tft.drawString(buf, MARGIN+150, currentY, 2);
        
        // Fila 2: Ciclos On/Off
        char freq[64];
        sprintf(freq, "On: %02d:%02d:%02d | Off: %02d:%02d:%02d", 
                bR.onHour, bR.onMinute, bR.onSecond, bR.offHour, bR.offMinute, bR.offSecond);
        tft.setTextColor(TFT_WHITE);
        tft.drawString(freq, MARGIN + 150, currentY + 14, 2);
    }

    // 6. Barra Inferior de Sensores en Tiempo Real
    tft.drawFastHLine(MARGIN, 215, TFT_WIDTH - 2 * MARGIN, TFT_WHITE);
    tft.setTextColor(TFT_CYAN);
    char sensorStr[100];
    sprintf(sensorStr, "T: %.1f C | H: %.1f %% | P: %.1f hPa", 
            configuraciones[valveIdx].temp_aire, 
            configuraciones[valveIdx].humedad_relativa, 
            configuraciones[valveIdx].presion);
    tft.drawCentreString(sensorStr, TFT_WIDTH / 2, 222, 2);

    screenNeedsUpdate = false;
}

void handleMonitorValveTouch(int16_t x, int16_t y) {
    // 1. Botón Menú
    Boton menuBtn = {TFT_WIDTH - 65, 5, 60, 25, "MENU", TFT_DARK_GRAY, 99, 1};
    if (x > menuBtn.x && x < menuBtn.x + menuBtn.w && y > menuBtn.y && y < menuBtn.y + menuBtn.h) {
        currentScreen = SCREEN_MAIN_MENU;
        screenNeedsUpdate = true;
        return;
    }

    // 2. Detección de botones de Válvula (V1 a V4)
    int btnW = 50;
    int btnH = 30;
    int spacing = 10;
    int startX = (TFT_WIDTH - (4 * btnW + 3 * spacing)) / 2;
    int startY = 35;

    for (int i = 0; i < 4; i++) {
        int bx = startX + i * (btnW + spacing);
        if (x > bx && x < bx + btnW && y > startY && y < startY + btnH) {
            if (valvulaSeleccionadaIdx != i) {
                valvulaSeleccionadaIdx = i;
                screenNeedsUpdate = true;
                Serial.printf("Monitoreando Valvula %d\n", i + 1);
            }
            return;
        }
    }
}

// -------------------------------------------------------------------
// N. PANTALLA Permisivo Humedad-Temperatura
// -------------------------------------------------------------------
void drawConfigHumTempScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("CONFIG. PERMISIVOS H/T", TFT_WIDTH / 2, 10, 2);

    BloqueRiego &b = configuraciones[valvulaSeleccionadaIdx].bloques[bloqueSeleccionadoIdx];
    
    // Dibujar etiquetas y botones para H_min, H_max, T_min, T_max
    // Usar el estilo de dibujo de botones existente (dibujarBoton)
    // Ejemplo para H_Min:
    tft.drawString("H. Min:", 50, 60, 2);
    char valStr[10];
    sprintf(valStr, "%.1f %%", b.h_min);
    Boton btnHmin = {140, 45, 80, 30, valStr, TFT_BLUE, 1, 2};
    dibujarBoton(btnHmin, currentTarget == TARGET_H_MIN);

    tft.drawString("H. Max:", 50, 100, 2);
    sprintf(valStr, "%.1f %%", b.h_max);
    Boton btnHmax = {140, 85, 80, 30, valStr, TFT_BLUE, 1, 2};
    dibujarBoton(btnHmax, currentTarget == TARGET_H_MAX);

    tft.drawString("T. Min:", 50, 140, 2);
    sprintf(valStr, "%.1f %C", b.t_min);
    Boton btnTmin = {140, 125, 80, 30, valStr, TFT_BLUE, 1, 2};
    dibujarBoton(btnTmin, currentTarget == TARGET_T_MIN);

    tft.drawString("T. Max:", 50, 180, 2);
    sprintf(valStr, "%.1f %C", b.t_max);
    Boton btnTmax = {140, 165, 80, 30, valStr, TFT_BLUE, 1, 2};
    dibujarBoton(btnTmax, currentTarget == TARGET_T_MAX);

    // Botón VOLVER
    Boton btnVolver = {200, 200, 100, 30, "VOLVER", TFT_DARK_GRAY, 99, 2};
    dibujarBoton(btnVolver);
}

// -------------------------------------------------------------------
// N. Manejo de PANTALLA Permisivo Humedad-Temperatura
// -------------------------------------------------------------------

void handleConfigHumTempTouch(int16_t x, int16_t y) {
    // Referencia al bloque actual para facilitar la lectura/escritura
    BloqueRiego &b = configuraciones[valvulaSeleccionadaIdx].bloques[bloqueSeleccionadoIdx];

    // 1. Botón VOLVER (Ubicación: x=200, y=200, w=100, h=30)
    if (x > 200 && x < 300 && y > 200 && y < 230) {
        currentScreen = SCREEN_CONFIG_VALVE_EDIT;
        screenNeedsUpdate = true;
        return;
    }

    // Definición de áreas de toque para los valores (basado en el dibujo previo)
    // H_MIN (Humedad Mínima)
    if (x > 140 && x < 220 && y > 45 && y < 75) {
        currentTarget = TARGET_H_MIN;
        keypadActive = true;
        inputIndex = 0;
        inputBuffer[0] = '\0';
        screenNeedsUpdate = true;
        return;
    }

    // H_MAX (Humedad Máxima)
    if (x > 140 && x < 220 && y > 85 && y < 115) {
        currentTarget = TARGET_H_MAX;
        keypadActive = true;
        inputIndex = 0;
        inputBuffer[0] = '\0';
        screenNeedsUpdate = true;
        return;
    }

    // T_MIN (Temperatura Mínima)
    if (x > 140 && x < 220 && y > 125 && y < 155) {
        currentTarget = TARGET_T_MIN;
        keypadActive = true;
        inputIndex = 0;
        inputBuffer[0] = '\0';
        return;
    }

    // T_MAX (Temperatura Máxima)
    if (x > 140 && x < 220 && y > 165 && y < 195) {
        currentTarget = TARGET_T_MAX;
        keypadActive = true;
        inputIndex = 0;
        inputBuffer[0] = '\0';
        return;
    }
}


