import tkinter as tk
import serial
import threading
import serial.tools.list_ports
import time
import logging
import queue

# Configure logging for better debugging
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')

# RFC2217 Serial Connection Configuration
RFC2217_SERVER = 'localhost'
RFC2217_PORT = 4000  # Ensure this matches the port in wokwi.toml
BAUD_RATE = 115200    # Ensure this matches the Arduino's baud rate

class BinaryCalcUI:
    def __init__(self, master):
        self.master = master
        self.master.title("Binary Calculator UI")

        self.label = tk.Label(self.master, text="Expression/Result:", font=("Arial", 14))
        self.label.pack(pady=10)

        # A label to show what's displayed or result
        self.display_var = tk.StringVar()
        self.display_var.set("Ready...")
        self.display_label = tk.Label(
            self.master,
            textvariable=self.display_var,
            font=("Arial", 16),
            bg="white",
            width=30,
            anchor='w',
            padx=10
        )
        self.display_label.pack()

        # Frame for buttons
        btn_frame = tk.Frame(self.master)
        btn_frame.pack(pady=10)

        buttons = [
            ('0', '0'), ('1', '1'), ('(', '('), (')', ')'),
            ('+', '+'), ('-', '-'), ('*', '*'), ('/', '/'),
            ('=', '='), ('Back', 'B')
        ]

        for i, (txt, val) in enumerate(buttons):
            b = tk.Button(
                btn_frame,
                text=txt,
                width=6,
                height=2,
                command=lambda v=val: self.send_char(v)
            )
            b.grid(row=i // 5, column=i % 5, padx=5, pady=5)

        # Initialize a queue for thread-safe communication
        self.queue = queue.Queue()

        # Attempt to connect to RFC2217 serial port
        self.ser = None
        try:
            ser_url = f'rfc2217://{RFC2217_SERVER}:{RFC2217_PORT}'
            logging.debug(f"Connecting to serial port at {ser_url} with baudrate {BAUD_RATE}")
            self.ser = serial.serial_for_url(
                ser_url,
                baudrate=BAUD_RATE,
                timeout=1
            )
            self.display_var.set("Ready...")

        except serial.SerialException as e:
            self.display_var.set(f"Serial Error: {e}")
            logging.error(f"Serial Error: {e}")

        # Start thread to read from Arduino
        if self.ser and self.ser.is_open:
            self.running = True
            self.read_thread = threading.Thread(
                target=self.read_from_serial,
                daemon=True
            )
            self.read_thread.start()
            logging.debug("Started thread to read from serial.")
        else:
            self.running = False
            logging.critical("Serial port not open. Unable to start read thread.")

        # Start processing the queue
        self.master.after(100, self.process_queue)

    def send_char(self, ch):
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(ch.encode('utf-8'))
                logging.debug(f"Sent: {ch}")
            except serial.SerialException as e:
                self.display_var.set(f"Write Error: {e}")
                logging.error(f"Write Error: {e}")
        else:
            self.display_var.set("Serial port not open.")
            logging.warning("Attempted to send data, but serial port is not open.")

    def read_from_serial(self):
        """Continuously read lines from Arduino, parse them, and put them in the queue."""
        while self.running:
            try:
                if self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        logging.debug(f"Received: {line}")
                        self.queue.put(line)
            except serial.SerialException as e:
                self.queue.put(f"Serial Error: {e}")
                logging.error(f"Read Error: {e}")
                break
            except UnicodeDecodeError as e:
                logging.error(f"Decode Error: {e}")
                continue
            time.sleep(0.01)  # Slight delay to prevent high CPU usage

    def process_queue(self):
        """Process messages from the queue and update the UI accordingly."""
        try:
            while not self.queue.empty():
                message = self.queue.get_nowait()
                logging.debug(f"Processing message: {message}")
                if message.startswith("EXPR:"):
                    expr = message[5:]
                    self.display_var.set(expr)
                elif message.startswith("RESULT:"):
                    res = message[7:]
                    self.display_var.set("Result => " + res)
                    # Schedule the result to be cleared after 2 seconds (2000 milliseconds)
                    self.master.after(2000, self.clear_result)
                elif message.startswith("Serial Error:"):
                    self.display_var.set(message)
                else:
                    # Handle any other messages if necessary
                    self.display_var.set(message)
        except queue.Empty:
            pass
        finally:
            # Schedule the next queue check
            self.master.after(100, self.process_queue)

    def clear_result(self):
        """Clear the result from the display after a timeout."""
        current_text = self.display_var.get()
        if current_text.startswith("Result =>"):
            self.display_var.set("")
            logging.debug("Cleared the result from the display.")

    def close(self):
        """Stop thread, close serial, then destroy window."""
        self.running = False
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
                logging.info("Serial port closed.")
            except serial.SerialException as e:
                logging.error(f"Close Error: {e}")
        self.master.destroy()


def main():
    root = tk.Tk()
    app = BinaryCalcUI(root)
    root.protocol("WM_DELETE_WINDOW", app.close)
    root.mainloop()


if __name__ == "__main__":
    main()
