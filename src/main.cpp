#include <Arduino.h>
#include <Notecard.h>

#define serialDebug Serial
#define productUID "com.blues.tj:kidtracker"
#define BUTTON_PIN USER_BTN
// Set this to 1 to disable debugging logs
#define RELEASE 1
#define VERSION_NUMBER "1.1.0"

#ifndef ARDUINO_SWAN_R5
#error "This program was designed to run on the Blues Swan"
#endif

Notecard notecard;
volatile bool locationRequested = false;

void ISR(void) {
  notecard.logDebug("Button pressed\n");
  locationRequested = true;
}

void setup() {
  #if !RELEASE
    static const size_t MAX_SERIAL_WAIT_MS = 5000;
    size_t begin_serial_wait_ms = ::millis();
    // Wait for the serial port to become available
    while (!serialDebug && (MAX_SERIAL_WAIT_MS > (::millis() - begin_serial_wait_ms)));
    serialDebug.begin(115200);
    notecard.setDebugOutputStream(serialDebug);
  #endif

  notecard.begin();

  {
    J *req = notecard.newRequest("hub.set");
    JAddStringToObject(req, "product", productUID);
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "inbound", 60 * 12);
    JAddNumberToObject(req, "outbound", 30);
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  {
    // Notify Notehub of the current firmware version
    J *req = notecard.newRequest("dfu.status");
    JAddStringToObject(req, "version", VERSION_NUMBER);
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  {
    // Enable Notecard Outboard Firmware Update
    J *req = notecard.newRequest("card.dfu");
    JAddBoolToObject(req, "on", true);
    JAddStringToObject(req, "name", "stm32");
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  {
    // Add temperature monitoring
    J *req = notecard.newRequest("card.temp");
    JAddNumberToObject(req, "minutes", 60);
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  {
    // Pull AUX1 low during DFU
    J *req = notecard.newRequest("card.aux");
    JAddStringToObject(req, "mode", "dfu");
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  {
    J *req = notecard.newRequest("card.location.mode");
    JAddStringToObject(req, "mode", "periodic");
    JAddNumberToObject(req, "seconds", 60 * 5);
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  {
    J *req = notecard.newRequest("card.location.track");
    JAddBoolToObject(req, "start", true);
    JAddBoolToObject(req, "heartbeat", true);
    JAddNumberToObject(req, "hours", 12);
    if (!notecard.sendRequest(req)) {
      JDelete(req);
    }
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), ISR, FALLING);

  notecard.logDebug("Setup complete\n");
}

void sendMessage(double lat, double lon) {
  notecard.logDebugf("Location: %.12lf, %.12lf\n", lat, lon);

  // http://maps.google.com/maps?q=<lat>,<lon>
  char buffer[100];
  snprintf(
    buffer,
    sizeof(buffer),
    "Your kids are requesting you. https://maps.google.com/maps?q=%.12lf,%.12lf",
    lat,
    lon
   );
  notecard.logDebug(buffer);

  J *req = notecard.newRequest("note.add");
  JAddStringToObject(req, "file", "alert.qo");
  JAddBoolToObject(req, "sync", true);
  J *body = JCreateObject();
  JAddStringToObject(body, "message", buffer);
  JAddItemToObject(req, "body", body);
  if (!notecard.sendRequest(req)) {
    JDelete(req);
  }

  notecard.logDebug("Location sent successfully.\n");
}

void loop() {
  if (!locationRequested) {
    return;
  }

  size_t gps_time_s;
  
  {
    // Save the time from the last location reading.
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
    gps_time_s = JGetInt(rsp, "time");
    NoteDeleteResponse(rsp);
  }

  {
    // Set the location mode to "continuous" mode to force the
    // Notecard to take an immediate GPS/GNSS reading.
    J *req = notecard.newRequest("card.location.mode");
    JAddStringToObject(req, "mode", "continuous");
    notecard.sendRequest(req);
  }

  // How many seconds to wait for a location before you stop looking
  size_t timeout_s = 600;

  // Block while resolving GPS/GNSS location
  for (const size_t start_ms = ::millis();;) {
    // Check for a timeout, and if enough time has passed, break out of the loop
    // to avoid looping forever
    if (::millis() >= (start_ms + (timeout_s * 1000))) {
      notecard.logDebug("Timed out looking for a location\n");
      locationRequested = false;
      break;
    }
  
    // Check if GPS/GNSS has acquired location information
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.location"));
    if (JGetInt(rsp, "time") != gps_time_s) {
      double lat = JGetNumber(rsp, "lat");
      double lon = JGetNumber(rsp, "lon");
      sendMessage(lat, lon);
      NoteDeleteResponse(rsp);

      // Restore previous configuration
      {
        J *req = notecard.newRequest("card.location.mode");
        JAddStringToObject(req, "mode", "periodic");
        notecard.sendRequest(req);
      }

      locationRequested = false;
      break;
    }
  
    // If a "stop" field is on the card.location response, it means the Notecard
    // cannot locate a GPS/GNSS signal, so we break out of the loop to avoid looping
    // endlessly
    if (JGetObjectItem(rsp, "stop")) {
      notecard.logDebug("Found a stop flag, cannot find location\n");
      locationRequested = false;
      break;
    }
  
    NoteDeleteResponse(rsp);
    // Wait 2 seconds before trying again
    delay(2000);
  }
}
