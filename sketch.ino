#include <Notecard.h>

#define serialDebug Serial
#define productUID "com.blues.tj:kidtracker"
#define BUTTON_PIN USER_BTN
// Set this to 1 to disable debugging logs
#define NDEBUG 0

#ifndef ARDUINO_SWAN_R5
#error "This program was designed to run on the Blues Wireless Swan"
#endif

Notecard notecard;
volatile bool locationRequested = false;

void ISR(void) {
  notecard.logDebug("Button pressed\n");
  locationRequested = true;
}

void setup() {  
  serialDebug.begin(115200);
  notecard.begin();

  #if !NDEBUG
    notecard.setDebugOutputStream(serialDebug);
  #endif
 
  J *req1 = notecard.newRequest("hub.set");
  JAddStringToObject(req1, "product", productUID);
  JAddStringToObject(req1, "mode", "periodic");
  JAddNumberToObject(req1, "outbound", 5);
  if (!notecard.sendRequest(req1)) {
    JDelete(req1);
  }

  J *req2 = notecard.newRequest("card.location.mode");
  JAddStringToObject(req2, "mode", "periodic");
  JAddNumberToObject(req2, "seconds", 180);
  if (!notecard.sendRequest(req2)) {
    JDelete(req2);
  }

  J *req3 = notecard.newRequest("card.location.track");
  JAddBoolToObject(req3, "start", true);
  JAddBoolToObject(req3, "heartbeat", true);
  JAddNumberToObject(req3, "hours", 12);
  if (!notecard.sendRequest(req3)) {
    JDelete(req3);
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), ISR, FALLING);

  notecard.logDebug("Setup complete\n");
}

void loop() {
  if (!locationRequested) {
    return;
  }

  J *req = notecard.newRequest("card.location");
  J *resp = notecard.requestAndResponse(req);
  double lat = JGetNumber(resp, "lat");
  double lon = JGetNumber(resp, "lon");
  notecard.logDebugf("Location: %.12lf, %.12lf\n", lat, lon);

  if (lat == 0) {
    notecard.logDebug("The Notecard does not yet have a location\n");
    // Wait a minute before trying again.
    delay(1000 * 60);
    return;
  }

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

  J *req2 = notecard.newRequest("note.add");
  JAddStringToObject(req2, "file", "twilio.qo");
  JAddBoolToObject(req2, "sync", true);
  J *body = JCreateObject();
  JAddStringToObject(body, "message", buffer);
  JAddItemToObject(req2, "body", body);
  if (!notecard.sendRequest(req2)) {
    JDelete(req2);
  }

  locationRequested = false;
  notecard.logDebug("Location sent successfully.\n");
}