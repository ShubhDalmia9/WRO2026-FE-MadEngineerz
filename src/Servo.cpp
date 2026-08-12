#include <Servo.h>

// Instantiate the Servo object (similar to Python's class instantiation)
Servo myServo;

// Define the hardware control pin
const int SERVO_PIN = 9;

void setup() {
  // Initialize serial communication for debugging at 9600 baud
  Serial.begin(9600);
  
  // Attach the servo and unlock the extended 600µs to 2400µs pulse limits
  // This maps 0 degrees to 600µs and 180 degrees to 2400µs
  myServo.attach(SERVO_PIN, 600, 2400);
  
  Serial.println("Servo initialized with extended range (600us - 2400us).");
}

void loop() {
  // Sweep to 0 degrees (Generates a 600µs pulse width)
  Serial.println("Moving to 0 degrees...");
  myServo.write(0);
  delay(2000); // Wait 2 seconds for mechanical travel
  // Sweep to 90 degrees (Generates a 1500µs pulse width - Center)
  Serial.println("Moving to 90 degrees...");
  myServo.write(90);
  delay(2000);

  // Sweep to 180 degrees (Generates a 2400µs pulse width)
  Serial.println("Moving to 180 degrees...");
  myServo.write(180);
  delay(2000);
}
