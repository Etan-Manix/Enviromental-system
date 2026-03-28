#include <LiquidCrystal_I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <EEPROM.h>
#include <math.h>
#include <Wire.h>

// PANTALLAS
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

// PINES
#define MQ_PIN    A0
#define KY_PIN    A1
#define DHTPIN     2
#define DHTTYPE    DHT11
#define BTN_UNICO  3
#define BUZZER    13

DHT dht(DHTPIN, DHTTYPE);

// VARIABLES GASES (x10)
int co = 0, ch4 = 0;
int coMax = 0, ch4Max = 0;
int coProm = 0, ch4Prom = 0;
long coSum = 0, ch4Sum = 0;
int contadorProm = 0;
float Ro = 10.0;

// VARIABLES AMBIENTE (x10)
int temp = 220, hum = 500;
int tempMax = 0, humMin = 1000;
int idxAmb = 0, idxProm = 0;
long idxSum = 0;

// VARIABLES SONIDO
int sonido = 30, sonidoMax = 30;
unsigned long tiempoSonido = 0;

// OFFSETS DHT11 (x10)
#define TEMP_OFFSET 247
#define HUM_OFFSET  187

// FILTRO MQ
#define MUESTRAS 5
int bufferCO[MUESTRAS];
int indiceCO = 0;

// TIEMPOS
unsigned long tiempoInicio = 0;
bool enRiesgo = false;

// HISTORIAL
#define HIST 10
int histCO[HIST];

// CARACTERES LCD
byte niveles[8][8] = {
  {0,0,0,0,0,0,0,31}, {0,0,0,0,0,0,31,31},
  {0,0,0,0,0,31,31,31}, {0,0,0,0,31,31,31,31},
  {0,0,0,31,31,31,31,31}, {0,0,31,31,31,31,31,31},
  {0,31,31,31,31,31,31,31}, {31,31,31,31,31,31,31,31}
};

// CONTROL DE PANTALLAS
#define LCD_VIEWS  7
int lcdView = 0;
int lastLCD = -1;
bool modoAuto = true;
unsigned long lastButton = 0;
unsigned long lastLCDchange = 0;
unsigned long lastUpdate = 0;
#define LCD_INTERVAL    8000
#define UPDATE_INTERVAL  500

// BOTÓN ÚNICO
bool lastBtnState = HIGH;
unsigned long debounceTime = 0;
#define DEBOUNCE_DELAY 50

// PULSACIÓN LARGA
unsigned long btnPressStart = 0;
bool btnLargo = false;
#define LONG_PRESS 5000

// CALIBRACIÓN
bool calibrando = false;
bool calibrado = false;
unsigned long calInicio = 0;
long calSuma = 0;
int calLecturas = 0;
#define CAL_DURATION 8000

// BUZZER
unsigned long buzzerTime = 0;
bool buzzerState = false;
bool alertaActiva = false;
#define BEEP_SLOW 2000
#define BEEP_FAST 300
#define BEEP_DURATION 150

// OLED
bool oledOK = false;
unsigned long lastOLEDupdate = 0;
#define OLED_UPDATE_INTERVAL 1000

// ========== PROTOTIPOS ==========
void leerBoton();
void leerSensores();
void calcularIndices();
void actualizarHistorial();
void controlBuzzer();
void mostrarLCD();
void mostrarOLED();
void lcdPantalla1();
void lcdPantalla2();
void lcdPantalla3();
void lcdPantalla4();
void lcdPantalla5();
void lcdPantalla6();
void lcdPantalla7();
void iniciarCalibracion();
void mostrarCalibracion();
String formatoTiempo();
String clasificacionAire();

// ========== SETUP ==========
void setup() {
  Serial.begin(9600);

  Wire.begin();
  Wire.setClock(25000); // Lento para cables largos

  lcd.init();
  lcd.backlight();

  delay(500); // Espera que LCD estabilice

  // createChar SIEMPRE antes de cualquier print
  for (int i = 0; i < 8; i++) {
    lcd.createChar(i, niveles[i]);
  }

  delay(100);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  ESTACION");
  lcd.setCursor(0, 1);
  lcd.print("   AMBIENTAL");
  delay(1000);

  dht.begin();
  pinMode(BTN_UNICO, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);

  oledOK = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (!oledOK) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("OLED NO DETECT.");
    delay(1500);
  } else {
    oled.clearDisplay();
    oled.setTextColor(WHITE);
    oled.setTextSize(2);
    oled.setCursor(20, 20);
    oled.print("OLED OK");
    oled.display();
    delay(1500);
    oled.clearDisplay();
    oled.display();
  }

  EEPROM.get(0, Ro);
  if (isnan(Ro) || Ro < 1.0 || Ro > 50.0) Ro = 10.0;

  lcd.clear();
  tiempoInicio   = millis();
  lastButton     = millis();
  lastLCDchange  = millis();
  lastOLEDupdate = millis();
}

// ========== LOOP ==========
void loop() {
  unsigned long ahora = millis();

  leerBoton();

  static unsigned long lastSensor = 0;
  if (ahora - lastSensor > 500) {
    lastSensor = ahora;
    leerSensores();
    calcularIndices();
    actualizarHistorial();
    controlBuzzer();
  }

  mostrarLCD();

  if (oledOK && ahora - lastOLEDupdate >= OLED_UPDATE_INTERVAL) {
    lastOLEDupdate = ahora;
    mostrarOLED();
  }

  delay(5);
}

// ========== BOTÓN ==========
void leerBoton() {
  unsigned long ahora = millis();
  bool btnState = digitalRead(BTN_UNICO);

  if (btnState == LOW && lastBtnState == HIGH) {
    btnPressStart = ahora;
    btnLargo = false;
    debounceTime = ahora;
  }

  if (btnState == LOW && !btnLargo && ahora - btnPressStart >= LONG_PRESS) {
    btnLargo = true;
    iniciarCalibracion();
  }

  if (btnState == HIGH && lastBtnState == LOW) {
    if (!btnLargo && ahora - debounceTime > DEBOUNCE_DELAY) {
      lastButton = ahora;
      modoAuto = false;
      lcdView = (lcdView + 1) % LCD_VIEWS;
      lastLCD = -1;
      lcd.clear();
    }
  }

  if (!modoAuto && ahora - lastButton > 30000) {
    modoAuto = true;
    lastLCDchange = ahora;
  }

  lastBtnState = btnState;
}

// ========== SENSORES ==========
void leerSensores() {
  if (calibrando) return;

  int adc = analogRead(MQ_PIN);
  float Vout = max(adc * (5.0 / 1023.0), 0.01f);
  float resistenciaSensor = ((5.0 - Vout) / Vout) * 10.0;
  float relacion = resistenciaSensor / Ro;

  float coCalc  = pow(10, (-0.77 * log10(relacion) + 1.7)) * 10;
  float ch4Calc = pow(10, (-0.38 * log10(relacion) + 1.4)) * 10;

  co  = constrain((int)coCalc,  0, 2000);
  ch4 = constrain((int)ch4Calc, 0, 10000);

  bufferCO[indiceCO] = co;
  indiceCO = (indiceCO + 1) % MUESTRAS;
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) suma += bufferCO[i];
  co = suma / MUESTRAS;

  if (co  > coMax)  coMax  = co;
  if (ch4 > ch4Max) ch4Max = ch4;

  contadorProm++;
  coSum  += co;
  ch4Sum += ch4;
  coProm  = coSum  / contadorProm;
  ch4Prom = ch4Sum / contadorProm;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) {
    temp = (int)((t * 10) + TEMP_OFFSET);
    if (temp > tempMax) tempMax = temp;
  }
  if (!isnan(h)) {
    hum = (int)((h * 10) + HUM_OFFSET);
    if (hum < humMin) humMin = hum;
  }

  int valor = analogRead(KY_PIN);
  static int pico = 0;
  if (valor > pico) pico = valor;

  if (millis() - tiempoSonido > 300) {
    if (pico > 550) {
      sonido = 30 + (pico - 512) / 8;
      if (sonido > 80) sonido = 80;
    } else {
      sonido = sonido * 0.9;
      if (sonido < 30) sonido = 30;
    }
    if (sonido > sonidoMax) sonidoMax = sonido;
    pico = 0;
    tiempoSonido = millis();
  }
}

// ========== ÍNDICES ==========
void calcularIndices() {
  if (calibrando) return;

  int scoreCO   = constrain(100 - (co  * 100 / 2000),  0, 100);
  int scoreCH4  = constrain(100 - (ch4 * 100 / 10000), 0, 100);
  int scoreTemp = constrain(100 - (abs(temp - 220) * 100 / 150), 0, 100);
  int scoreHum  = constrain(100 - (abs(hum - 500)  * 100 / 400), 0, 100);
  int scoreSon  = constrain(100 - (sonido * 100 / 80), 0, 100);

  idxAmb = (scoreCO*25 + scoreCH4*25 + scoreTemp*20 + scoreHum*20 + scoreSon*10) / 100;

  if (contadorProm >= 10000) {
    idxSum = idxAmb;
    contadorProm = 1;
    coSum  = co;
    ch4Sum = ch4;
  }

  idxSum += idxAmb;
  idxProm = idxSum / contadorProm;
  enRiesgo = (idxAmb < 50);
}

// ========== HISTORIAL ==========
void actualizarHistorial() {
  if (calibrando) return;
  for (int i = 0; i < HIST - 1; i++) {
    histCO[i] = histCO[i + 1];
  }
  histCO[HIST-1] = co / 10;
}

// ========== BUZZER ==========
void controlBuzzer() {
  if (calibrando) return;
  alertaActiva = (co > 1500 || ch4 > 5000) && idxAmb < 50;
  unsigned long ahora = millis();

  if (alertaActiva) {
    if (ahora - buzzerTime >= BEEP_FAST) {
      buzzerState ? noTone(BUZZER) : tone(BUZZER, 2500);
      buzzerState = !buzzerState;
      buzzerTime = ahora;
    }
  } else if (enRiesgo) {
    if (ahora - buzzerTime >= BEEP_SLOW) {
      tone(BUZZER, 1500, BEEP_DURATION);
      buzzerTime = ahora;
    }
  } else {
    noTone(BUZZER);
    buzzerState = false;
  }
}

// ========== FORMATOS ==========
String formatoTiempo() {
  unsigned long segs = (millis() - tiempoInicio) / 1000;
  char buffer[9];
  sprintf(buffer, "%02lu:%02lu:%02lu", segs/3600, (segs%3600)/60, segs%60);
  return String(buffer);
}

String clasificacionAire() {
  if (idxAmb >= 85) return "EXCELENTE";
  if (idxAmb >= 70) return "BUENO    ";
  if (idxAmb >= 50) return "RIESGO   ";
  return "PELIGRO  ";
}

// ========== LCD ==========
void mostrarLCD() {
  unsigned long ahora = millis();

  if (calibrando) {
    mostrarCalibracion();
    return;
  }

  if (modoAuto && ahora - lastLCDchange >= LCD_INTERVAL) {
    lcdView = (lcdView + 1) % LCD_VIEWS;
    lastLCDchange = ahora;
    lastLCD = -1;
    lcd.clear();
  }

  if (lcdView == lastLCD && ahora - lastUpdate < UPDATE_INTERVAL) return;

  lastUpdate = ahora;
  lastLCD = lcdView;

  if (modoAuto) {
    lcd.setCursor(15, 0);
    lcd.print("A");
  }

  switch (lcdView) {
    case 0: lcdPantalla1(); break;
    case 1: lcdPantalla2(); break;
    case 2: lcdPantalla3(); break;
    case 3: lcdPantalla4(); break;
    case 4: lcdPantalla5(); break;
    case 5: lcdPantalla6(); break;
    case 6: lcdPantalla7(); break;
  }
}

void lcdPantalla1() {
  lcd.setCursor(0, 0);
  lcd.print("CO:");
  lcd.print(co/10); lcd.print("."); lcd.print(co%10);
  lcd.print("ppm    ");
  lcd.setCursor(0, 1);
  lcd.print("CH4:");
  lcd.print(ch4/10); lcd.print("."); lcd.print(ch4%10);
  lcd.print("ppm    ");
}

void lcdPantalla2() {
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temp/10); lcd.print("."); lcd.print(temp%10);
  lcd.print((char)223); lcd.print("C H:");
  lcd.print(hum/10); lcd.print("."); lcd.print(hum%10);
  lcd.print("%   ");
  lcd.setCursor(0, 1);
  lcd.print(formatoTiempo());
}

void lcdPantalla3() {
  lcd.setCursor(0, 0);
  lcd.print("IDX:"); lcd.print(idxAmb); lcd.print("% ");
  if      (idxAmb >= 85) lcd.print("EXC");
  else if (idxAmb >= 70) lcd.print("BUE");
  else if (idxAmb >= 50) lcd.print("RIE");
  else                   lcd.print("PEL");
  lcd.setCursor(0, 1);
  lcd.print("PROM:"); lcd.print(idxProm); lcd.print("% ");
  lcd.print(idxAmb >= idxProm ? "+" : "-");
}

void lcdPantalla4() {
  lcd.setCursor(0, 0);
  lcd.print("SONIDO:"); lcd.print(sonido); lcd.print("dB");
  lcd.setCursor(0, 1);
  if      (sonido < 45) lcd.print("SILENCIO        ");
  else if (sonido < 60) lcd.print("NORMAL          ");
  else if (sonido < 70) lcd.print("ALTO            ");
  else                  lcd.print("RUIDOSO         ");
}

void lcdPantalla5() {
  lcd.setCursor(0, 0);
  lcd.print("MAX CO:"); lcd.print(coMax/10);
  lcd.print(" CH4:"); lcd.print(ch4Max/10);
  lcd.setCursor(0, 1);
  lcd.print("T MAX:"); lcd.print(tempMax/10);
  lcd.print("C H:"); lcd.print(humMin/10); lcd.print("%");
}

void lcdPantalla6() {
  lcd.setCursor(0, 0);
  lcd.print("CO "); lcd.print(co/10); lcd.print("ppm     ");
  lcd.setCursor(0, 1);
  for (int i = 0; i < 8; i++) {
    int valor = constrain(histCO[i*2] / 25, 0, 7);
    lcd.write(byte(valor));
  }
}

void lcdPantalla7() {
  lcd.setCursor(0, 0);
  lcd.print("Ro:"); lcd.print(Ro, 1); lcd.print("k       ");
  lcd.setCursor(0, 1);
  lcd.print("BTN 5s=CALIBRAR ");
}

// ========== CALIBRACIÓN ==========
void iniciarCalibracion() {
  calibrando  = true;
  calibrado   = false;
  calInicio   = millis();
  calSuma     = 0;
  calLecturas = 0;
  lcd.clear();
}

void mostrarCalibracion() {
  unsigned long ahora = millis();
  if (calibrado) return;

  if (ahora - calInicio < CAL_DURATION) {
    int adc = analogRead(MQ_PIN);
    float Vout = max(adc * (5.0 / 1023.0), 0.01f);
    calSuma += (long)(((5.0 - Vout) / Vout) * 100);
    calLecturas++;

    int segundos = (CAL_DURATION - (ahora - calInicio)) / 1000 + 1;
    int progreso = map(ahora - calInicio, 0, CAL_DURATION, 0, 16);

    lcd.setCursor(0, 0);
    lcd.print("CALIBRANDO ");
    lcd.print(segundos); lcd.print("s  ");
    lcd.setCursor(0, 1);
    for (int i = 0; i < 16; i++)
      lcd.print(i < progreso ? (char)255 : ' ');

  } else {
    Ro = constrain(((float)calSuma / calLecturas) / (9.8 * 100.0), 1.0, 50.0);
    EEPROM.put(0, Ro);
    calibrando = false;
    calibrado  = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CALIB OK! Ro:");
    lcd.setCursor(0, 1);
    lcd.print(Ro, 2); lcd.print("k       ");
    delay(2000);
    lcd.clear();
  }
}

// ========== OLED ==========
void mostrarOLED() {
  oled.clearDisplay();
  oled.setTextColor(WHITE);
  oled.setTextSize(2);

  oled.setCursor(0, 0);
  oled.print("CO:");
  oled.print(co/10); oled.print(".");
  oled.print(co%10);

  oled.setCursor(0, 20);
  oled.print("CH4:");
  oled.print(ch4/10); oled.print(".");
  oled.print(ch4%10);

  oled.setCursor(0, 40);
  oled.print("T:");
  oled.print(temp/10); oled.print(".");
  oled.print(temp%10);
  oled.print("C");

  if ((millis() / 500) % 2 == 0) {
    oled.fillRect(120, 55, 5, 5, WHITE);
  }

  oled.display();
}