#include <Arduino.h>

// Assign the digital output hardware pin, this is an ex
const int MOTOR_PIN = 9;

const float TimeOff = 0.15; // Example: 0.15 seconds (15% Duty Cycle / 150ms HIGH for testing)

void motorPWM(float a) {
  // 1. Calculate high and low phase durations in milliseconds
  int highTimeMs = static_cast<int>(a * 1000.0);
  int lowTimeMs  = static_cast<int>((1.0 - a) * 1000.0);

  // 2. Execute HIGH state phase
  digitalWrite(MOTOR_PIN, HIGH);
  Serial.println("HIGH");
  delay(highTimeMs);

  // 3. Execute LOW state phase
  digitalWrite(MOTOR_PIN, LOW);
  Serial.println("LOW");
  delay(lowTimeMs);
}

void setup() {
  // Initialize standard hardware Serial output at 9600 baud rate
  Serial.begin(9600);
  
  // Set the digital pin mode to function as an active hardware output
  pinMode(MOTOR_PIN, OUTPUT);
  
  Serial.println("Arduino Software PWM loop started...");
}

void loop() {
  // Continuously cycle your PWM function using your input value
  motorPWM(TimeOff);
}
