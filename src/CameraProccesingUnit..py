import numpy as np
from picamera2 import Picamera2
import libcamera
import time

picam2 = Picamera2()
#Config:
config = picam2.create_video_configuration(
    main={"size":(160,140),
    "format": "YUV422" } #QQVGA at raw yuv422 due to the weak single core processor of the Pi Zero W


)
#TODO: REFERENCE COLOUR DETECTION SYSTEM HERE, COMMENT OUT 
picam2.configure(config)
picam2.start()
u_min, u_max = 90, 110 # For colour selection, purely for testing purposes
v_min, v_max = 140, 160 

try:
    while True:
        frame_yuv = picam2.capture_array("main")

        u_channel = frame_yuv[:, :, 1] #Slicing via numpy (Index: y-0, u - 1, v - 2)
        v_channel = frame_yuv[:, :, 2]

        matching_indices = (u_channel >= u_min) & (u_channel <= u_max) & \
                           (v_channel >= v_min) & (v_channel <= v_max) 
        # Defines matching_indices to be u & v values that fall under the colour range
        y_positions, x_positions = np.where(matching_indices)

        if len(y_positions) > 30: #len of x positions is equal to len of y positions, therefore no need to run both
            
            center_x =  int(np.mean(x_positions)) #Determines centre of object on x axis
            center_y = int(np.mean(y_positions))  #May delete line. Purpose of centerpoint is for our PID avoidance algorithm. Refer to README
            print("Center x of object: ", center_x,"\n", "Center y of object: ", center_y, "\n")
        

except(KeyboardInterrupt):
    print("Terminated code!")
#TODO: Remove this, perhaps change it for an error LED. Only present for SSH Pi testing.

#TODO: PID algorithm and ref angle control system thing in separate files.