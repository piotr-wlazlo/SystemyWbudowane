import serial
import time
from datetime import datetime

ser = serial.Serial('/dev/tty.ubserial-', 115200)

now = datetime.now()
czas = now.strftime("%Y-%m-%d %H:%M:%S\n")

# na razie niezakodowane zeby zobaczyc czy jest sens
ser.write(czas.encode())
ser.close()
