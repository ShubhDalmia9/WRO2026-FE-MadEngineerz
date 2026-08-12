#include <Arduino.h>

// 1. Global Variables to store parsed command parameters
int servoAngle = 90; // Default center position
int motorSpeed = 0;  // Default stopped position

// 2. State management variables for reading the stream
String inputBuffer = "";
unsigned long lastPacketTime = 0;
const unsigned long TIMEOUT_MS = 200; // 200ms Failsafe threshold

void setup() {
  // Initialize USB Serial interface at the identical baud rate set on the Pi
  Serial.begin(115200); 
  
  // Set lower execution allowance for stream strings to prevent loop drag
  Serial.setTimeout(2); 

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  // 3. Asynchronous Cache Reading Loop
  while (Serial.available() > 0) {
    char incomingChar = (char)Serial.read();
    
    // Check if character marks the end of a transmission payload
    if (incomingChar == '\n') {
      parseControlString(inputBuffer);
      inputBuffer = ""; // Reset cache immediately for next string package
    } else {
      inputBuffer += incomingChar; // Cache character pieces dynamically
    }
  }

  // 4. Critical Hardware Safety Watchdog Checks
  if (millis() - lastPacketTime > TIMEOUT_MS) {
    motorSpeed = 0; // Cut drive motor throttle instantly
    digitalWrite(LED_BUILTIN, HIGH); // Visual warning indicator active
  } else {
    digitalWrite(LED_BUILTIN, LOW);
  }

  // --- ACTUATOR RUN ROUTINE GOES HERE ---
  // analogWrite(motorPin, abs(motorSpeed));
  // steeringServo.write(servoAngle);
}

// 5. String Processing and Unpacking Engine
void parseControlString(String data) {
  // Validate presence of starting packet header index marker '$'
  if (data.startsWith("$")) {
    data.remove(0, 1); // Strip header '$' away cleanly
    
    int commaIndex = data.indexOf(',');
    if (commaIndex != -1) {
      // Isolate components relative to delimiter positioning
      String steerString = data.substring(0, commaIndex);
      String speedString = data.substring(commaIndex + 1);
      
      // Update running variable values natively
      servoAngle = steerString.toInt();
      motorSpeed = speedString.toInt();
      
      lastPacketTime = millis(); // Refresh active loop timeline state check
      Serial.println("OK"); 
    }
  }
}
