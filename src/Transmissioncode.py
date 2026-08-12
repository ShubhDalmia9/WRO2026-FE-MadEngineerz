import serial as sr
import time
ServoAngle = int(input("Give input here: ")) #Input statements for testing, will be removed
MotorPWM = int(input("Next input: "))
try:
    with sr.Serial('/dev/ttyUSB0', 115200, timeout=0) as ser:
        while True:
            command_string = f"${ServoAngle},{MotorPWM}\n" #string formation
            ser.write(command_string.encode('utf-8')) #
            print(f"Generated: {repr(command_string)}") #Repr makes all characters 

            if ser.in_waiting > 0:
            # Read line, decode bytes back to text, and strip the newline formatting
                response = ser.readline().decode('utf-8').strip()
                print(f"Nano Response: {response}")
            
        time.sleep(0.1) # Loop delay to account for FPS. To be tuned
except KeyboardInterrupt:
    print("\nTest stopped successfully.")

#NOTE: Remove input statements in the future