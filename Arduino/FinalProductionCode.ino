#include <LiquidCrystal.h>
// NOTE: This is the final production code. It uses the robust counting logic and 
// transmits data via Hardware Serial (D0/D1) to the HC-05/HC-06 Bluetooth module.

// --- CRITICAL BLUETOOTH WIRING ---
// Arduino D1 (TX) connects to HC-05 RX (Data OUT)
// Arduino D0 (RX) connects to HC-05 TX (Data IN)
// Remember to DISCONNECT the HC-05 TX wire from D0 before uploading!
// ---------------------------------

// --- HARDWARE PIN DEFINITIONS ---
LiquidCrystal lcd(2, 3, 4, 5, 6, 7); // LCD: RS, E, D4, D5, D6, D7

#define E_S1 A0 // Echo pin for Sensor 1 (A)
#define T_S1 A1 // Trigger pin for Sensor 1 (A)

#define E_S2 A2 // Echo pin for Sensor 2 (B)
#define T_S2 A3 // Trigger pin for Sensor 2 (B)

int RELAY_PIN = 8; // Digital Pin for Light Control Relay (Assumed Active-LOW)

// --- LOGIC VARIABLES ---
const int DETECTION_THRESHOLD = 30; // Distance in cm to consider blocked (SET TO 30CM)
long dis_a = 0, dis_b = 0; 
int person = 0; 

/* State Flags for Directional Logic (State Machine)
 * 0: Idle/Ready
 * 1: Sequence Started (e.g., Sensor A was blocked first)
 * 2: Passage Completed (Stops re-counting until reset)
 * 3: Intermediate Lock (New state to confirm blockage of the second sensor)
 */
int flag1 = 0; // Tracks sequence status for Sensor 1 (A)
int flag2 = 0; // Tracks sequence status for Sensor 2 (B)

// --- TIMER VARIABLES ---
unsigned long usageStartTime = 0;
unsigned long totalUsageTime = 0; // Total accumulated usage time in milliseconds
bool lightStatus = false; // Tracks the current state of the light (ON/OFF)
unsigned long sequenceStartTime = 0;
const long PASSAGE_TIMEOUT = 2000; // 2 seconds to complete the passage

//**********************ultra_read****************************
void ultra_read(int pin_t, int pin_e, long &ultra_time) {
  long time;
  pinMode(pin_t, OUTPUT);
  pinMode(pin_e, INPUT);

  // Send trigger pulse
  digitalWrite(pin_t, LOW);
  delayMicroseconds(2);
  digitalWrite(pin_t, HIGH);
  delayMicroseconds(10);
  digitalWrite(pin_t, LOW);

  // Measure echo duration
  time = pulseIn(pin_e, HIGH);

  // Convert time to distance (cm)
  ultra_time = time / 58; 
}

// Helper function to display time (MM:SS) with leading zero padding
void printTime(unsigned long seconds) {
    unsigned int minutes = seconds / 60;
    unsigned int remainingSeconds = seconds % 60;
    
    // Print Minutes (MM)
    if (minutes < 10) lcd.print("0");
    lcd.print(minutes);
    lcd.print(":");
    
    // Print Seconds (SS)
    if (remainingSeconds < 10) lcd.print("0");
    lcd.print(remainingSeconds);
}

// Function to handle Bluetooth transmission (D0/D1)
void transmitData(unsigned long seconds, int count, bool lightOn) {
    // Format data to send over Bluetooth in a clean CSV format
    // Example output: COUNT:1,USAGE_S:15,LIGHT:ON
    Serial.print("COUNT:");
    Serial.print(count);
    Serial.print(",USAGE_S:");
    Serial.print(seconds);
    Serial.print(",LIGHT:");
    Serial.println(lightOn ? "ON" : "OFF");
}


void setup() {
  Serial.begin(9600); // CRITICAL: Initializes Serial communication for HC-05 (Pins 0/1)
  
  // CRITICAL FIX: Add a startup delay to ensure LCD power stability
  delay(100); 
  
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print(" BT COUNTER READY ");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Light starts OFF (Active-LOW relay assumed)

  person = 0; 
  flag1 = 0;
  flag2 = 0;

  delay(2000);
  lcd.clear();
}


void loop() {
  // 1. Read distances from both sensors
  ultra_read(T_S1, E_S1, dis_a);
  ultra_read(T_S2, E_S2, dis_b);

  int oldPersonCount = person; // Capture count BEFORE change

  // --- No DEBUG OUTPUT: Only the final data packet will be sent to D1 ---
  
  // 2. DIRECTIONAL COUNTING LOGIC (Using the robust state machine)

  // --- 2a. Detect Initial Direction (Start Sequence) ---
  if (flag1 == 0 && flag2 == 0) {
      if (dis_a < DETECTION_THRESHOLD && dis_b > DETECTION_THRESHOLD) {
        flag1 = 1; // Start Entry A -> B
        sequenceStartTime = millis(); // START TIMEOUT TIMER
      }
      else if (dis_b < DETECTION_THRESHOLD && dis_a > DETECTION_THRESHOLD) {
        flag2 = 1; // Start Exit B -> A
        sequenceStartTime = millis(); // START TIMEOUT TIMER
      }
  }

  // --- 2b. Intermediate Lock (Confirming Passage) ---
  // Entry Path Confirmation (A -> B): A is clear, B is blocked, Flag 1 started
  if (flag1 == 1 && dis_a > DETECTION_THRESHOLD && dis_b < DETECTION_THRESHOLD && flag2 != 2) {
    flag1 = 3; // Commit to the count 
  }
  
  // Exit Path Confirmation (B -> A): B is clear, A is blocked, Flag 2 started
  if (flag2 == 1 && dis_b > DETECTION_THRESHOLD && dis_a < DETECTION_THRESHOLD && flag1 != 2) {
    flag2 = 3; // Commit to the count 
  }

  // --- 2c. Final Count and Mark Completion ---
  
  // Entry Count 
  if (flag1 == 3 && dis_b < DETECTION_THRESHOLD) {
    person++; // INCREMENT count
    flag1 = 2; // Mark state as 'completed'
    flag2 = 2; 
  }
  
  // Exit Count
  else if (flag2 == 3 && dis_a < DETECTION_THRESHOLD) {
    person--; // DECREMENT count
    flag1 = 2; // Mark state as 'completed'
    flag2 = 2; 
  }
  
  // --- BLUETOOTH TRANSMISSION ON COUNT CHANGE ---
  if (person != oldPersonCount) {
    
    // 5. POWER USAGE TIMER LOGIC (Embedded here to update usage before transmitting)
    if (person > oldPersonCount) { // Count increased
        if (oldPersonCount == 0) {
            // Transition 0 -> 1: Light turns ON
            usageStartTime = millis();
            lightStatus = true; 
        }
    } else { // Count decreased
        if (person == 0) {
            // Transition 1 -> 0: Light turns OFF
            unsigned long duration = millis() - usageStartTime;
            totalUsageTime += duration;
            usageStartTime = 0;
            lightStatus = false; 
        }
    }
    
    // Transmit data when count changes
    unsigned long currentTotalTimeMs = totalUsageTime;
    if (lightStatus) {
        currentTotalTimeMs += (millis() - usageStartTime);
    }
    // Send the clean, final data packet over Bluetooth
    transmitData(currentTotalTimeMs / 1000, person, (person > 0)); 
  }


  // 3. RESET State (Passage Clearance)
  // Only reset if BOTH sensors are clear.
  if (dis_a > DETECTION_THRESHOLD && dis_b > DETECTION_THRESHOLD) {
    flag1 = 0;
    flag2 = 0;
    delay(50);
  }
  
  // --- PASSAGE TIMEOUT CHECK ---
  if (flag1 != 0 || flag2 != 0) {
      if (flag1 != 2 && flag2 != 2) { // Only check timeout if not in a completed state
          if (millis() - sequenceStartTime > PASSAGE_TIMEOUT) {
              flag1 = 0;
              flag2 = 0;
              sequenceStartTime = 0;
          }
      }
  }

  // 4. Boundary Check: Ensure count never goes below zero
  if (person < 0) {
    person = 0;
  }
  
  // 7. DISPLAY AND RELAY CONTROL 
  
  unsigned long currentTotalTimeMsDisplay = totalUsageTime;
  unsigned long currentSessionTimeMs = 0;
  
  // RELAY CONTROL: 
  if (person > 0) {
      currentSessionTimeMs = millis() - usageStartTime;
      digitalWrite(RELAY_PIN, LOW); // *INVERTED* - LOW turns ON an Active-LOW relay
  } else {
      digitalWrite(RELAY_PIN, HIGH); // *INVERTED* - HIGH turns OFF an Active-LOW relay
  }

  bool displayLightOn = (person > 0); 
  unsigned long usageSeconds = currentTotalTimeMsDisplay / 1000;
  
  // --- LINE 1 (Row 0): People Count and Light Status ---
  lcd.setCursor(0, 0);
  lcd.print("People:");
  lcd.print(person);
  
  lcd.setCursor(9, 0); 
  lcd.print("Light:");
  if (displayLightOn) { 
      lcd.print("ON "); 
  } 
  else { 
      lcd.print("OFF"); 
  } 
  
  // --- LINE 2 (Row 1): TIME DISPLAY (Switches between Session and Total) ---
  lcd.setCursor(0, 1);

  if (displayLightOn) { 
      // Light is ON: Show running session time
      lcd.print("Session: ");
      printTime(currentSessionTimeMs / 1000);
      lcd.print("      "); // Clear remaining space
  } else {
      // Light is OFF: Show total accumulated time
      lcd.print("Total Used: ");
      printTime(usageSeconds);
      lcd.print("  "); // Clear remaining space
  }
}
