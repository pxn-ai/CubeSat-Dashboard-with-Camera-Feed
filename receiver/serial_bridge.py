import asyncio
import websockets
import serial
import json
import time

# --- CONFIGURATION ---
SERIAL_PORT = '/dev/tty.usbmodem101'  # Update to your Arduino port (run: ls /dev/cu.usb*)
BAUD_RATE = 9600
SERVER_PORT = 8101
CLIENTS = set()

# --- SERIAL COMMUNICATION ---

def setup_serial():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        time.sleep(2)
        ser.reset_input_buffer()
        print(f"Successfully connected to {SERIAL_PORT}")
        return ser
    except serial.SerialException as e:
        print(f"CRITICAL: Could not open serial port {SERIAL_PORT}. Error: {e}")
        return None

def get_telemetry_packet(ser):
    try:
        line = ser.readline().decode('utf-8').strip()
        if not line: return None
        
        # Check for JSON start (Object '{' or Array '[')
        if line.startswith('{') or line.startswith('['):
            try:
                json.loads(line) # Validate JSON
                print(f"Sent packet: {line}")
                return line
            except json.JSONDecodeError:
                return None
        return None
    except Exception:
        return None

# --- WEBSOCKET SERVER ---

async def register(websocket):
    CLIENTS.add(websocket)
    print(f"Client connected. Total: {len(CLIENTS)}")

async def unregister(websocket):
    CLIENTS.remove(websocket)
    print(f"Client disconnected. Total: {len(CLIENTS)}")

async def producer_handler(ser):
    while True:
        packet = get_telemetry_packet(ser)
        if packet and CLIENTS:
            message = packet.encode('utf-8')
            # Create tasks for all sends
            tasks = [asyncio.create_task(client.send(message)) for client in CLIENTS]
            # Wait for all tasks to complete or timeout
            if tasks:
                await asyncio.wait(tasks, timeout=1)
        await asyncio.sleep(0.1)

# --- FIX 1: Removed 'path' argument here ---
async def websocket_handler(websocket, ser):
    await register(websocket)
    try:
        # Keep connection open
        await websocket.wait_closed()
    finally:
        await unregister(websocket)

async def main():
    print("Starting WebSocket Server...")
    ser = setup_serial()
    if not ser: return

    try:
        # --- FIX 2: Removed 'path' from the lambda here ---
        start_server = websockets.serve(
            lambda websocket: websocket_handler(websocket, ser), 
            "localhost", 
            SERVER_PORT
        )
        await start_server
    except OSError as e:
        if e.errno == 10048:
             print(f"\nCRITICAL ERROR: Port {SERVER_PORT} is already in use.")
             print("It seems another instance of this script is already running.")
             print("Please kill the existing terminal or python process (PID 8456 was the culprit previously).")
             return
        raise e
    await producer_handler(ser)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServer stopped.")
