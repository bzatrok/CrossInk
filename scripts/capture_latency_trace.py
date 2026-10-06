# Capture serial from the X4 Pro for N seconds; reconnect if the port drops (reset / sleep).
import serial, sys, time
port_name = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem1101'
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 120
KEYS = ('LAT:', 'Wait complete', 'UC8279', 'SSD1677', 'controller', 'probe', 'input', 'Wake', 'sleep')
end = time.time() + secs
with open('serial_full.log', 'a') as full:
    while time.time() < end:
        try:
            with serial.Serial(port_name, 115200, timeout=1) as port:
                print(f"[connected {time.strftime('%H:%M:%S')}]", flush=True)
                last = time.time()
                while time.time() < end:
                    line = port.readline()
                    if not line:
                        if time.time() - last > 30:
                            raise OSError('idle 30 s, reopening')
                        continue
                    last = time.time()
                    text = line.decode('utf-8', 'replace')
                    full.write(text); full.flush()
                    if any(k in text for k in KEYS):
                        sys.stdout.write(text); sys.stdout.flush()
        except (serial.SerialException, OSError) as e:
            print(f"[port lost {time.strftime('%H:%M:%S')}: {e}; retrying]", flush=True)
            time.sleep(1)
