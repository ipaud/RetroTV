#include <Arduino.h>

#include "app/App.h"
#include "config.h"

static App app;

void setup() {
  Serial.setTxBufferSize(SERIAL_TX_BUFFER);  // before begin(); the default 256 B cuts long stats lines
  Serial.begin(115200);
  // Never stall the TV when no USB host reads the log. Not 0: in core 2.0.17, HWCDC::write counts
  // its retries down from the timeout, so 0 wraps around and, with a computer plugged in but the
  // port closed, the loop waited forever once the log ring was full (the web remote's commands
  // piled up unanswered). With 1 ms the core gives up once and drops the log until someone reads.
  Serial.setTxTimeoutMs(1);
  app.begin();
}

void loop() { app.loop(); }
