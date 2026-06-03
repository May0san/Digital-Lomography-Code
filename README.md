For a guide on building the camera body itself, follow the instructables tutorial here: https://www.instructables.com/Tiny-Outdoor-Camera/

You will need the Arduino IDE app as well as the PNGenc library and the FileConfig library (installed within the Arduino App itself)

The process for loading the code to the ESP32 is very obscure so read the instructions carefully. I have removed the code that puts the camera to sleep so you don't need to worry about holding the rset button to update the code unless you put the instructables code on your ESP32 already.

Replace the camera code with the code found in this repository and load the SD card with the contents of SD Card Files folder.

The Dithered version of the code will create a dithered photograph using a palette that you give it. The JPEG and PNG versions output JPEGs and PNGs of the image, respectively. The PNG encoder library used by both the Dithered and PNG versions of the code are intensive on the ESP32 and so the camera cannot handle some of the larger photo resolutions.
