#include <AMY-Arduino.h>

#define PIN_I2S_BCK  38
#define PIN_I2S_WS   39
#define PIN_I2S_DOUT 40

void setup() {
  Serial.begin(115200);
  delay(1500);

  amy_config_t cfg = amy_default_config();
  cfg.audio    = AMY_AUDIO_IS_I2S;
  cfg.i2s_bclk = PIN_I2S_BCK;
  cfg.i2s_lrc  = PIN_I2S_WS;
  cfg.i2s_dout = PIN_I2S_DOUT;
  cfg.features.default_synths = 1;
  // baseline puro: sin multicore, sin multithread, sin reverb/echo/chorus
  amy_start(cfg);

  Serial.printf("AMY_SAMPLE_RATE=%d  AMY_BLOCK_SIZE=%d\n",
                AMY_SAMPLE_RATE, AMY_BLOCK_SIZE);

  // synth 1, Juno patch 0, 4 voces
  amy_event s = amy_default_event();
  s.synth = 1; s.patch_number = 0; s.num_voices = 4;
  amy_add_event(&s); amy_update();

  // acorde sostenido C-E-G (no se apaga nunca)
  for (int i = 0; i < 3; i++) {
    amy_event n = amy_default_event();
    n.synth = 1; n.midi_note = 60 + i*4; n.velocity = 0.5f;
    amy_add_event(&n); amy_update();
  }
  Serial.println("acorde sostenido -> escuchar");
}

void loop() {
  amy_update();   // patron canonico del doc: amy_update en el loop, nada mas
}
