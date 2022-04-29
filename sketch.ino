#include <Notecard.h>

#define serialDebug Serial
#define productUID "YOUR VALUE HERE"
#define BUTTON_PIN USER_BTN

Notecard notecard;
bool locationRequested = false;

void ISR(void) {
  serialDebug.println("Button pressed");
  locationRequested = true;
}

void setup() {
  serialDebug.begin(115200);

  notecard.begin();
  notecard.setDebugOutputStream(serialDebug);
 
  J *req1 = notecard.newRequest("hub.set");
  JAddStringToObject(req1, "product", productUID);
  JAddStringToObject(req1, "mode", "periodic");
  JAddNumberToObject(req1, "outbound", 5);
  notecard.sendRequest(req1);

  J *req2 = notecard.newRequest("card.location.mode");
  JAddStringToObject(req2, "mode", "periodic");
  JAddNumberToObject(req2, "seconds", 300);
  notecard.sendRequest(req2);

  J *req3 = notecard.newRequest("card.location.track");
  JAddBoolToObject(req3, "start", true);
  JAddBoolToObject(req3, "heartbeat", true);
  JAddNumberToObject(req3, "hours", 12);
  notecard.sendRequest(req3);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), ISR, FALLING);

  serialDebug.println("Setup complete");
}

void loop() {
  if (!locationRequested) {
    return;
  }

  J *req = notecard.newRequest("card.location");
  J *resp = notecard.requestAndResponse(req);
  serialDebug.println("Location:");
  double lat = JGetNumber(resp, "lat");
  double lon = JGetNumber(resp, "lon");
  serialDebug.println(lat, 10);
  serialDebug.println(lon, 10);

  if (lat == 0) {
    serialDebug.println("The Notecard does not yet have a location");
    // Wait a minute before trying again.
    delay(1000 * 60);
    return;
  }

  // http://maps.google.com/maps?q=<lat>,<lon>
  char buffer[100];
  snprintf(
    buffer,
    sizeof(buffer),
    "Your kids are requesting you. https://maps.google.com/maps?q=%.8lf,%.8lf",
    lat,
    lon
   );
  serialDebug.println(buffer);

  J *req2 = notecard.newRequest("note.add");
  JAddStringToObject(req2, "file", "twilio.qo");
  JAddBoolToObject(req2, "sync", true);
  J *body = JCreateObject();
  JAddStringToObject(body, "message", buffer);
  JAddItemToObject(req2, "body", body);
  notecard.sendRequest(req2);

  locationRequested = false;
  serialDebug.println("Location sent successfully.");
}
