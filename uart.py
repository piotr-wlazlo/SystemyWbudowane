import serial

ser = serial.Serial('/dev/tty.usbserial-', 115200)
while True:
    line = ser.readline().decode().strip()
    print(line)