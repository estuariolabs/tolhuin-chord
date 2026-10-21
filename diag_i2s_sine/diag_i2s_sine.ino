/* ============================================================================
   DIAG_I2S_SINE  -  Prueba de AISLAMIENTO de audio (SIN AMY)
   ----------------------------------------------------------------------------
   Objetivo: confirmar si la cadena ESP32-S3 -> I2S -> PCM5102 produce sonido,
   independientemente de AMY y de cualquier modulo del proyecto Tolhuin.

   Usa SOLO la libreria I2S nativa del core arduino-esp32 3.x (ESP_I2S),
   sin AudioTools, sin Maximilian, sin OLED, sin botones, sin ADS, sin tareas.

   HARDWARE (PCM5102):
     GPIO38 -> BCK
     GPIO39 -> WS / LRCK
     GPIO40 -> DIN
     SCK    -> GND   (puente del modulo / cable)

   Genera una senoide pura de 440 Hz continua.

   Compilar/subir (no requiere PSRAM para esta prueba):
   & ".\arduino-cli.exe" compile --upload -p COM7 ^
     --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc" diag_i2s_sine

   INTERPRETACION:
     SUENA      -> hardware PCM5102 + I2S OK. El problema esta en AMY/su integracion.
     NO SUENA   -> el problema esta debajo de AMY: core ESP32, pines I2S,
                   cableado o jumpers de configuracion del modulo PCM5102.
   ============================================================================ */
#include <ESP_I2S.h>

// ---- Pines (idénticos a los del proyecto Tolhuin) ----
const int8_t PIN_BCK  = 38;   // BCK
const int8_t PIN_WS   = 39;   // WS / LRCK
const int8_t PIN_DOUT = 40;   // DIN del PCM5102
// SCK del PCM5102 -> GND (no se controla por software)

// ---- Parámetros de audio ----
const uint32_t SAMPLE_RATE = 44100;
const float    TONE_HZ     = 440.0f;
const int16_t  AMP         = 10000;   // ~1/3 del fondo de escala (evita saturar)

I2SClass i2s;

float phase = 0.0f;
float phaseInc;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("[diag] === PRUEBA I2S SIN AMY ===");
  Serial.printf("[diag] BCK=%d  WS=%d  DOUT=%d  rate=%u Hz\n",
                PIN_BCK, PIN_WS, PIN_DOUT, SAMPLE_RATE);

  phaseInc = 2.0f * PI * TONE_HZ / (float)SAMPLE_RATE;

  // din = -1 (no hay entrada), mclk = -1 (el PCM5102 no necesita MCLK)
  i2s.setPins(PIN_BCK, PIN_WS, PIN_DOUT, -1, -1);

  bool ok = i2s.begin(I2S_MODE_STD, SAMPLE_RATE,
                      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (!ok) {
    Serial.println("[diag] ERROR: i2s.begin() devolvio FALSE.");
    Serial.println("[diag] -> Revisar version del core esp32 (necesita 3.x) o pines.");
    while (true) delay(1000);
  }

  Serial.println("[diag] I2S iniciado OK. Generando senoide 440 Hz continua...");
  Serial.println("[diag] Si NO se escucha: cableado/jumpers del DAC, pines I2S o core.");
}

void loop() {
  const int N = 256;                 // muestras por bloque
  static int16_t buf[N * 2];         // estereo intercalado: L, R, L, R, ...

  for (int i = 0; i < N; i++) {
    int16_t s = (int16_t)(sinf(phase) * AMP);
    phase += phaseInc;
    if (phase >= 2.0f * PI) phase -= 2.0f * PI;
    buf[2 * i]     = s;   // canal L
    buf[2 * i + 1] = s;   // canal R
  }

  // write() bloquea hasta que el DMA acepta el bloque -> salida continua
  i2s.write((const uint8_t *)buf, sizeof(buf));

  // latido por Serial para confirmar que el loop corre
  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.println("[diag] sonando (loop activo)...");
  }
}
