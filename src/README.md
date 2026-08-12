# Software Documentation

Software for the MadEngineerz WRO 2026 Future Engineers robot. The system is split across two computing platforms: a Raspberry Pi running Python for perception and decision making, and an Arduino Nano running C++ for actuator control. This folder contains the source files and the research documents behind them.

This page documents both. Where the research describes something the code does not yet implement, it is labelled as planned rather than presented as working.

## Contents

* Software Architecture
* Programming Environment and Tools
* Development and Deployment
* Core Software Components
* Raspberry Pi and Arduino Communication
* Computer Vision System
* YUV422 Colour Detection
* Open Challenge Strategy
* Obstacle Challenge Strategy
* PID Steering Control
* Odometry
* ToF-Based Safety and Positioning
* Sensor Fusion
* Motor Control
* Steering Servo Control
* Software Development Philosophy
* Engineering Challenges and Solutions
* Implementation Status
* Known Issues, Gaps and TODOs
* Software Dependencies
* File Structure
* Technical Documentation

## Software Architecture

Two processors, with a hard split in responsibility.

```
+---------------------------------------------------------------+
|                        RASPBERRY PI                           |
|                          (Python 3)                           |
|                                                               |
|   Camera  ->  Vision      Time-of-Flight array                |
|               pipeline                                        |
|                                                               |
|   Rotary encoder odometry     High level navigation           |
|                               and state management            |
|                                                               |
|                    Steering PID calculation                   |
+-------------------------------+-------------------------------+
                                |
                                |  Serial, ASCII string messages
                                |  115200 baud
                                v
+---------------------------------------------------------------+
|                        ARDUINO NANO                           |
|                       (Embedded C++)                          |
|                                                               |
|   Servo steering angle execution                              |
|   DC motor direction and PWM speed control                    |
+-------------------------------+-------------------------------+
                                |
                       +--------+--------+
                       |                 |
                       v                 v
                 Steering servo     Drive motor
```

**Why the split.** The Pi is the only platform on the robot capable of running a camera pipeline, and it is a poor platform for deterministic timing because it runs a general purpose operating system. The Nano is the reverse. Putting perception and decision making on the Pi and actuator execution on the Nano gives each task the platform suited to it: the vision loop can take as long as it needs without stalling the motor, and the servo pulse train and PWM stay deterministic regardless of what the Pi is doing.

The split also gives a clean failure boundary. Because the Nano holds the last commanded values and runs its own watchdog, a stall or crash on the Pi results in the motor being stopped by the Nano rather than the robot continuing at its last commanded speed. That behaviour is implemented, not planned; see [Raspberry Pi and Arduino Communication](#raspberry-pi-and-arduino-communication).

## Programming Environment and Tools

| Platform | Language | Toolchain | Responsibilities |
|---|---|---|---|
| Raspberry Pi | Python 3 | Edited and run over SSH | Image processing, sensor integration, state management, steering calculation |
| Arduino Nano | C++, embedded | Arduino IDE | Deterministic real time control of motor PWM and servo steering commands |

## Development and Deployment

**Raspberry Pi.** Code is deployed over SSH, either over a direct Ethernet link or over a dedicated Wi-Fi hotspot. This gives low latency remote terminal management and fast code iteration during pit debugging, with no external monitor required.

**Arduino Nano.** Firmware is compiled and flashed with the standard Arduino IDE over a direct USB serial link.

**Startup indication.** Competition rules restrict external discrete LEDs, so boot and script execution are indicated using the Raspberry Pi's onboard green ACT LED, mapped through GPIO triggers. This confirms that the system has started. It does not convey system status beyond that.

A separate indicator exists on the Nano: `LED_BUILTIN` is driven HIGH by the serial watchdog when the link has timed out, and returns LOW when packets resume.

## Core Software Components

| File | Platform | Language | Purpose | Status |
|---|---|---|---|---|
| [`CameraProccesingUnit..py`](./CameraProccesingUnit..py) | Raspberry Pi | Python | Camera configuration, raw YUV422 capture, U and V channel colour masking, centroid calculation | Working pipeline with test threshold values |
| [`Transmissioncode.py`](./Transmissioncode.py) | Raspberry Pi | Python | Builds and writes the serial command string to the Nano, reads the reply | Working, with test scaffolding still present |
| [`Nanotransmissionreceiver.cpp`](./Nanotransmissionreceiver.cpp) | Arduino Nano | C++ | Serial receive, packet parse, command state, watchdog failsafe | Parsing and failsafe implemented; actuator calls commented out |
| [`Servo.cpp`](./Servo.cpp) | Arduino Nano | C++ | Servo library setup and pulse range configuration | **Test routine.** Sweeps fixed angles; not the final steering control |
| [`MotorPWM.cpp`](./MotorPWM.cpp) | Arduino Nano | C++ | Software generated PWM on a digital pin | **Test implementation.** Not the final motor control |

Two of the five files are test routines rather than control software, and the receiver's actuator calls are not yet wired. The perception and communication layers are the parts that currently run.

## Raspberry Pi and Arduino Communication

The link uses **human readable ASCII strings rather than binary packets**. Strings are easier to read on a terminal, easier to debug when a value looks wrong, and easier to modify when a field changes. The cost is a small reduction in efficiency and speed, accepted deliberately in exchange for development simplicity.

### Packet structure

```
$<servoAngle>,<motorSpeed>\n
```

| Element | Value | Purpose |
|---|---|---|
| Start marker | `$` | Identifies the beginning of a valid payload |
| Field 1 | `servoAngle` | Target steering angle |
| Delimiter | `,` | Separates the two fields |
| Field 2 | `motorSpeed` | Target motor speed |
| Terminator | `\n` | Marks the end of the payload |

### Raspberry Pi transmission

From [`Transmissioncode.py`](./Transmissioncode.py), using `pyserial` on `/dev/ttyUSB0` at 115200 baud with `timeout=0`:

```python
command_string = f"${ServoAngle},{MotorPWM}\n"   # string formation
ser.write(command_string.encode('utf-8'))

if ser.in_waiting > 0:
    response = ser.readline().decode('utf-8').strip()
```

A `time.sleep(0.1)` loop delay is present, noted in the source as accounting for frame rate and still to be tuned.

### Arduino parsing

From [`Nanotransmissionreceiver.cpp`](./Nanotransmissionreceiver.cpp). `Serial.begin(115200)` matches the Pi, and `Serial.setTimeout(2)` keeps stream reads from dragging the main loop. Characters are accumulated one at a time until the newline arrives:

```cpp
while (Serial.available() > 0) {
    char incomingChar = (char)Serial.read();
    if (incomingChar == '\n') {
        parseControlString(inputBuffer);
        inputBuffer = "";
    } else {
        inputBuffer += incomingChar;
    }
}
```

The parser validates the header, splits on the first comma and converts both fields:

```cpp
if (data.startsWith("$")) {
    data.remove(0, 1);
    int commaIndex = data.indexOf(',');
    if (commaIndex != -1) {
        servoAngle = data.substring(0, commaIndex).toInt();
        motorSpeed = data.substring(commaIndex + 1).toInt();
        lastPacketTime = millis();
        Serial.println("OK");
    }
}
```

Startup defaults are `servoAngle = 90`, the centre position, and `motorSpeed = 0`, stopped. A successful parse returns `OK` to the Pi.

### Watchdog failsafe

```cpp
if (millis() - lastPacketTime > TIMEOUT_MS) {   // TIMEOUT_MS = 200
    motorSpeed = 0;
    digitalWrite(LED_BUILTIN, HIGH);
} else {
    digitalWrite(LED_BUILTIN, LOW);
}
```

If no valid packet arrives within 200 ms, the drive throttle is cut to zero and the built in LED lights as a visual warning. `lastPacketTime` only refreshes on a successful parse, so a corrupted or malformed stream triggers the failsafe just as a disconnected cable would. This is the mechanism that makes the Pi's non deterministic timing safe to build on.

## Computer Vision System

The vision system trades resolution for processing frequency, so that the control loop stays responsive at speed.

```
        [ Raw camera feed ]
                 |
                 v
     [ YUV422 colour space capture ]
                 |
                 v
    [ Discard Y channel (luminance) ]
                 |
                 v
 [ Apply threshold masks on U and V channels ]
                 |
                 v
  [ NumPy vectorised pixel mean (centroid X, Y) ]
                 |
                 v
   [ Calculate relative angle and distance scale ]
```

The first four stages are implemented in [`CameraProccesingUnit..py`](./CameraProccesingUnit..py). The final stage, relative angle and distance scale, is designed but not yet written.

**Framing.** The pipeline document specifies QQVGA at 70 FPS. The current source configures:

```python
config = picam2.create_video_configuration(
    main={"size": (160, 140),
          "format": "YUV422" }  # QQVGA at raw yuv422 due to the weak single core processor of the Pi Zero W
)
```

The in-code comment gives the reason for the low resolution and the raw format directly: the single core processor of the Pi Zero W. No frame rate is set explicitly in the current source.

**Why not BGR or HSV.** Converting every frame to another colour space costs processing time the Pi Zero W does not have. Capturing raw YUV422 skips the conversion entirely, and because U and V are already the chrominance channels, a colour mask can be built by indexing into the array rather than transforming it first.

**Uses.** The camera provides block detection, corner line detection, turning decisions and obstacle detection.

**OpenCV.** OpenCV appears in the dependency research as intended for colour masking in the main code, and is listed among the Raspberry Pi libraries. It is **not imported by any current source file**. All processing implemented today is NumPy only.

## YUV422 Colour Detection

Implemented in [`CameraProccesingUnit..py`](./CameraProccesingUnit..py).

**1. Capture and channel extraction.** The frame arrives as raw YUV422 with channel indices `y = 0`, `u = 1`, `v = 2`. The Y channel is not read at all for this step:

```python
frame_yuv  = picam2.capture_array("main")
u_channel  = frame_yuv[:, :, 1]   # Slicing via numpy (Index: y-0, u - 1, v - 2)
v_channel  = frame_yuv[:, :, 2]
```

**2. Boolean mask.** Both chroma channels are tested against minimum and maximum bounds in a single vectorised expression:

```python
matching_indices = (u_channel >= u_min) & (u_channel <= u_max) & \
                   (v_channel >= v_min) & (v_channel <= v_max)
```

**3. Matching pixel positions.**

```python
y_positions, x_positions = np.where(matching_indices)
```

**4. Noise gate.** A region is only accepted above a pixel count. The source notes that both arrays are the same length, so only one is tested:

```python
if len(y_positions) > 30:
```

**5. Centroid.** The centre of the detected region is the arithmetic mean of the matching pixel coordinates:

```python
center_x = int(np.mean(x_positions))
center_y = int(np.mean(y_positions))
```

This returns the centroid without contour fitting or blob analysis, which is what keeps the pipeline fast enough to run on this hardware.

**Threshold values are test values.** The current bounds are `u_min, u_max = 90, 110` and `v_min, v_max = 140, 160`, marked in the source as being purely for testing purposes. They are **not finalised competition values**, and the file carries a single range pair rather than separate calibrated red and green sets. Dedicated red and green calibration sets are an open development item.

**Camera alignment.** The camera module is fixed along the chassis centreline, so the centre of a pillar aligns with the camera's central reference and the frame centreline can act as the 0° reference for relative angle.

## Open Challenge Strategy

```
Lane centering between walls, biased to the inner wall
                      |
                      v
          Corner line detection (camera)
                      |
                      v
             Corner line counting
                      |
                      v
              11 lines counted
                      |
                      v
   Camera navigation yields to encoder odometry
                      |
                      v
    Drive a pre-calculated distance, then full stop
```

**Lane centering.** The control algorithm holds the vehicle centred between the inner and outer track walls, with a slight algorithmic bias towards the inner boundary wall to optimise cornering radii.

**Lap counting and stop condition.** Lap progression is monitored by counting corner marker lines through the camera. Once 11 corner lines have been detected, the system changes state: camera navigation yields to encoder odometry, which drives the vehicle a pre-calculated physical distance before bringing it to a full stop.

**Status: planned.** The lane centering logic, corner line detection, the lap counter and the odometry handover are described in the navigation research and are not present in the current source files. The camera colour detection that they will build on is implemented.

## Obstacle Challenge Strategy

**Detection.** Red and green pillars are identified by the U and V channel mask described above, and their centroid is the mean position of the matching pixels.

**Relative angle.** The camera is fixed along the chassis centreline. The pixel distance between a detected pillar's centroid and the frame centreline represents the relative angular offset of that obstacle, with θ = 0° at true centre.

**Colour dependent avoidance.** The obstacle's colour and its relative angle together determine which side the robot passes on.

**Dynamic steering angle reduction.** The target avoidance angle between the chassis and the pillar decreases linearly as the pillar's pixel footprint increases in the frame, which indicates closer proximity. The effect is a smooth sweeping curve around a pillar rather than an abrupt late swerve. No equation for this relationship exists in the source or the research documents.

**Steering response.** Corrections are applied through the PID controlled servo.

**Status.** Detection and centroid calculation are implemented. Relative angle calculation, the size to angle reduction and the avoidance behaviour are planned and not present in the current source.

## PID Steering Control

A PID controller governs the steering servo. Its purpose is to reduce steering error and produce controlled corrections: enough response to reach the required steering position, without under correcting and drifting wide or over correcting and oscillating. The input is the obstacle centroid produced by the colour detection stage.

**Status: not implemented.** The PID loop and the reference angle calculation are listed as modules still to be written. The camera source refers to them explicitly, noting that the centre point exists for the PID avoidance algorithm and that the PID and reference angle systems belong in separate files. No gain values exist in any source or research file, and none are stated here.

## Odometry

The rotary encoder tracks physical movement. Its documented roles are:

* **Parking**, where positioning accuracy matters more than visual reference
* **Movement and distance estimation** during navigation
* **Final stopping behaviour** in the Open Challenge, taking over from camera navigation after the eleventh corner line and driving a pre-calculated distance to the stop point

Odometry is handled on the Raspberry Pi.

**Status: not implemented.** No encoder reading or odometry code exists in the current source files. The encoder hardware and its wiring are documented in the [Schemes](../schemes/README.md) documentation. No distance conversion or encoder calculation is stated here, because none exists yet.

## ToF-Based Safety and Positioning

The Time-of-Flight array provides spatial safety. It continuously monitors close proximity and acts as a fail-safe override: if the vehicle approaches a wall, the ToF reading forces immediate corrective steering, **overriding camera input**. This inversion of the normal priority is the point of the sensor. The camera drives the robot in open running, but a wall at close range is a situation where the camera's judgement is not allowed to win.

Documented roles:

* Parking
* Open Challenge navigation
* Wall distance estimation
* Collision protection
* Supporting or overriding camera based decisions

ToF sensors are read by the Raspberry Pi.

**Status: not implemented.** No ToF reading or override logic exists in the current source files.

## Sensor Fusion

Sensor inputs are merged dynamically according to environmental context and priority level rather than blended at fixed weights.

| Sensor | Primary role | Fusion and priority |
|---|---|---|
| **Camera** | Primary navigation | Calculates relative target angles for cornering, pillar detection and track colour lines. Holds primary control during open track driving. |
| **Rotary encoder** | Odometry and distance | Tracks precise physical movement for accurate parking manoeuvres and to determine final lap completion. |
| **Time-of-Flight** | Spatial safety net | Continuously monitors close proximity. Acts as a fail-safe override forcing immediate corrective steering when the vehicle approaches a wall, overriding camera input. |

The hierarchy is contextual: the camera leads on open track, the encoder leads where position matters more than vision, and the ToF array can pre-empt both when a collision is imminent.

**Status: planned.** This is the designed architecture. None of the three fusion paths are implemented in the current source, and no arbitration logic exists yet. A BNO085 IMU is present in the robot hardware, but it is not assigned a role in the sensor fusion architecture research and no IMU code exists in this folder.

## Motor Control

From [`MotorPWM.cpp`](./MotorPWM.cpp). **This is a test implementation, not the final motor control.**

PWM is generated in software by driving the pin directly, rather than using `analogWrite`:

```cpp
void motorPWM(float a) {
  int highTimeMs = static_cast<int>(a * 1000.0);
  int lowTimeMs  = static_cast<int>((1.0 - a) * 1000.0);

  digitalWrite(MOTOR_PIN, HIGH);
  delay(highTimeMs);

  digitalWrite(MOTOR_PIN, LOW);
  delay(lowTimeMs);
}
```

| Property | Value |
|---|---|
| Pin | 9 |
| Method | Software generated PWM using `digitalWrite` and `delay` |
| Input | `a`, a duty fraction between 0 and 1 |
| High period | `a × 1000` ms |
| Low period | `(1 - a) × 1000` ms |
| Resulting period | 1000 ms |
| Test value | `TimeOff = 0.15`, giving a 15% duty cycle |
| Serial | 9600 baud, for debug output |

Two limitations follow directly from the code as written. The period is one second, which is a test frequency rather than a drive frequency. And because the routine uses blocking `delay` calls, the loop cannot do anything else while a PWM phase is in progress. Both are consequences of this being a standalone test file; the motor routine that will run in the final firmware has to be called from the receiver's main loop alongside the serial handling and the watchdog.

## Steering Servo Control

From [`Servo.cpp`](./Servo.cpp). **This is a travel test routine, not the final steering control.**

```cpp
#include <Servo.h>
Servo myServo;
const int SERVO_PIN = 9;

// Attach the servo and unlock the extended 600us to 2400us pulse limits
myServo.attach(SERVO_PIN, 600, 2400);
```

| Property | Value |
|---|---|
| Library | Arduino `Servo` |
| Pin | 9 |
| Pulse range | 600 µs to 2400 µs, the extended range |
| 0° | 600 µs |
| 90° | 1500 µs, centre |
| 180° | 2400 µs |
| Serial | 9600 baud, for debug output |

The extended pulse range is unlocked deliberately: the default Arduino range is narrower, and using the full 600 to 2400 µs span gives the steering the widest travel the servo can deliver, which matters on a short wheelbase where a small angular range would limit the turning circle.

The current `loop()` sweeps to 0°, 90° and 180° with two second delays between them. That verifies mechanical travel and the pulse mapping. It is not steering control.

**Relationship to the higher level command.** In the final firmware the `servoAngle` value parsed from the serial packet is what will be written to the servo. That call currently exists as a commented placeholder in the receiver:

```cpp
// --- ACTUATOR RUN ROUTINE GOES HERE ---
// analogWrite(motorPin, abs(motorSpeed));
// steeringServo.write(servoAngle);
```

## Software Development Philosophy

The principles below are drawn from the design decisions visible in the research and the source, not stated as generic goals.

**Simple algorithms where simple is sufficient.** The vision pipeline is deliberately a NumPy channel mask, not a computer vision framework. Contour fitting and blob analysis were skipped in favour of a boolean mask and a mean, because that returns the centroid in milliseconds on hardware that could not afford the alternative.

**Match the platform to the task.** Perception on the Pi, deterministic actuator timing on the Nano. Neither platform is asked to do the thing it is bad at.

**Readability and debuggability over efficiency, where the cost is small.** The serial protocol uses ASCII strings rather than binary specifically so a human can read the traffic, at a known and accepted cost in speed.

**Practical constraints drive the design.** The camera resolution and raw format are chosen because of the Pi Zero W's single core. The framing decision is documented in the source itself, at the line where it is made.

**Iterative development, honestly tracked.** Test files, placeholder values and TODO comments are left visible in the source rather than removed, and the open items are tracked as a list rather than tidied away.

## Engineering Challenges and Solutions

### SSH access to the Raspberry Pi

**Challenge.** SSH over Wi-Fi through VS Code could not be made to work despite extended effort. Without SSH there was no way to test the code at all, because most of the libraries the code depends on only run on the Raspberry Pi itself.

**Approach.** Multiple solutions were attempted over hours before the approach was changed rather than continued.

**Solution.** Wi-Fi was dropped entirely in favour of Ethernet. It was later found that a hotspot configured to WPA2 on an older phone was the only way to get the Pi Zero W to connect to a network at all.

**Result.** Deployment now runs over SSH via direct Ethernet, with the WPA2 hotspot available for network connectivity. SSH itself still requires the direct Ethernet connection to a laptop.

### Obstacle detection performance

**Challenge.** The obstacle detection algorithm worked, but it was slow, reported a low frame rate, struggled under varying lighting, and overloaded the CPU of a test Raspberry Pi 3, which is considerably more powerful than the Zero W the robot actually carries. It also drew bounding boxes above the obstacles rather than on them.

**Approach.** The problem was traced to how the detection defined an obstacle's position.

**Solution.** The algorithm was changed to work from the centre of an obstacle rather than from its corner.

**Result.** The current pipeline is centroid based, computing the mean X and Y of the masked pixels. This is the approach implemented in [`CameraProccesingUnit..py`](./CameraProccesingUnit..py) today, and it is also what feeds the planned PID avoidance algorithm.

## Implementation Status

An explicit summary, so that nothing in this document is mistaken for more than it is.

| Capability | Status |
|---|---|
| Camera capture, YUV422, U and V masking, centroid | **Implemented**, with test threshold values |
| Serial protocol, Pi transmission side | **Implemented**, with test scaffolding present |
| Serial protocol, Nano parsing side | **Implemented** |
| Watchdog failsafe on the Nano | **Implemented** |
| Servo pulse range configuration | **Implemented**, inside a test routine |
| Software PWM generation | **Test implementation only** |
| Actuator calls driven by received commands | **Not wired**, commented placeholders |
| Relative angle calculation | **Planned** |
| Dynamic steering angle reduction | **Planned** |
| PID steering loop | **Planned**, not written |
| Reference angle module | **Planned**, not written |
| Corner line detection and counting | **Planned** |
| Lane centering | **Planned** |
| Odometry and encoder reading | **Planned** |
| ToF reading and override logic | **Planned** |
| Sensor fusion arbitration | **Planned** |
| Parking sequence | **Planned** |

## Known Issues, Gaps and TODOs

### Hardware and baud conflicts

* **Pin allocation conflict.** Both [`Servo.cpp`](./Servo.cpp) and [`MotorPWM.cpp`](./MotorPWM.cpp) currently attempt to use pin 9.
* **Baud rate mismatch.** The standalone C++ test files open serial at 9600 baud, while [`Nanotransmissionreceiver.cpp`](./Nanotransmissionreceiver.cpp) operates at 115200 baud.

### Open development items

* **Integration.** Re-enable and wire the commented out actuator hardware calls in `Nanotransmissionreceiver.cpp` to the servo and motor routines.
* **PID system.** Write the standalone PID steering loop and the reference angle calculation modules.
* **Colour bounds.** Replace the temporary inline U and V test bounds with dedicated red and green calibration sets.
* **Transmission delay.** Tune the `time.sleep(0.1)` transmission loop delay against the real time camera frame rate.
* **Code cleanup.** Remove test scaffolding: the `input()` calls in the Python files, the terminal prints in the vision module, and the test print statements.
* **Optimisation.** Evaluate dropping the `center_y` calculation if the PID avoidance algorithm does not need it.

## Software Dependencies

### Python, Raspberry Pi

| Library | Purpose | In current source |
|---|---|---|
| `numpy` | Array slicing, boolean masks, `np.where`, `np.mean` | Yes |
| `picamera2` | Camera configuration and frame acquisition | Yes |
| `libcamera` | Camera stack | Yes |
| `time` | Frame timing and delay loops | Yes |
| `serial` (pyserial) | Serial bus communication with the Arduino | Yes |
| `opencv` | Colour masking in the main code | **No.** Listed in the dependency research as intended for future use; not imported by any current source file |

### C++, Arduino Nano

| Library | Purpose | In current source |
|---|---|---|
| `Arduino.h` | Core microcontroller function definitions | Yes |
| `Servo.h` | Servo pulse generation | Yes |
| Built in `Serial` | Serial bus communication and debugging | Yes |

## File Structure

### Source files

| File | Type | Platform | Purpose |
|---|---|---|---|
| [`CameraProccesingUnit..py`](./CameraProccesingUnit..py) | Python | Raspberry Pi | Camera capture and YUV422 colour detection with centroid output |
| [`Transmissioncode.py`](./Transmissioncode.py) | Python | Raspberry Pi | Serial command transmission to the Arduino Nano |
| [`Nanotransmissionreceiver.cpp`](./Nanotransmissionreceiver.cpp) | C++ | Arduino Nano | Serial receive, parsing, command state and watchdog failsafe |
| [`Servo.cpp`](./Servo.cpp) | C++ | Arduino Nano | Servo library setup and travel test |
| [`MotorPWM.cpp`](./MotorPWM.cpp) | C++ | Arduino Nano | Software PWM test implementation |

### Research and design documents

| Document | Contents |
|---|---|
| [`Core Software Components.pdf`](./Core%20Software%20Components.pdf) | Language choices, core libraries and the functional task distribution between the two platforms |
| [`Computer Vision System & Processing Pipeline.pdf`](./Computer%20Vision%20System%20&%20Processing%20Pipeline.pdf) | Vision pipeline stages, framing decisions and the reasoning behind raw YUV422 processing |
| [`Navigation Strategies & Algorithms.pdf`](./Navigation%20Strategies%20&%20Algorithms.pdf) | Open Challenge lane centering and stop condition, Obstacle Challenge centering and dynamic steering angle reduction |
| [`Sensor Fusion Architecture.pdf`](./Sensor%20Fusion%20Architecture.pdf) | Sensor roles and the contextual priority hierarchy between camera, encoder and ToF |
| [`Build & Deployment Process.pdf`](./Build%20&%20Deployment%20Process.pdf) | SSH deployment, Arduino IDE flashing and startup indication |
| [`Software Dependencies.pdf`](./Software%20Dependencies.pdf) | Python and C++ library dependencies and their purposes |
| [`Engineering Challenges & Solutions.pdf`](./Engineering%20Challenges%20&%20Solutions.pdf) | The SSH access problem and the obstacle detection performance problem, with how each was resolved |
| [`Known Issues, Gaps & TODOs.pdf`](./Known%20Issues,%20Gaps%20&%20TODOs.pdf) | Pin and baud conflicts and the current open development items |

## Technical Documentation

| Area | Documentation |
|---|---|
| Electrical design, wiring and power | [Schemes](../schemes/README.md) |
| Mechanical design and manufacturing files | [Models](../models/README.md) |
| Component references and research | [Other](../other/README.md) |
| Vehicle photographs | [Vehicle photos](../v-photos/README.md) |

*Team MadEngineerz, WRO 2026 Future Engineers*
