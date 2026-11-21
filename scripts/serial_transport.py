"""USB serial transport for the Teensy command interface."""
import json
import os
import select
import time


class SerialTransport:
    simulation = False

    def __init__(self, port):
        if os.name != "posix":
            raise RuntimeError("USB mode currently supports macOS/Linux. Desktop preview is available here.")
        import termios
        self.termios = termios
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            settings = termios.tcgetattr(self.fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
            import array
            import fcntl
            fcntl.ioctl(self.fd, termios.TIOCMBIS, array.array("i", [termios.TIOCM_DTR]))
        except Exception:
            os.close(self.fd)
            raise
        self.out_of_sync = False

    def command(self, text):
        if self.out_of_sync:
            raise RuntimeError("USB session is out of sync. Reset the helmet and restart the USB tool.")
        data = (text + "\n").encode("ascii")
        try:
            return self._exchange(data)
        except (OSError, RuntimeError):
            # Replies carry no command ID. A late reply cannot be assigned safely.
            self.out_of_sync = True
            raise

    def _exchange(self, data):
        deadline = time.monotonic() + 3
        while data:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([], [self.fd], [], remaining)[1]:
                raise RuntimeError("USB write timed out. Close other serial monitors and check the cable.")
            try:
                data = data[os.write(self.fd, data):]
            except BlockingIOError:
                continue
        pending = b""
        while b"\n" not in pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise RuntimeError("Helmet did not respond. Reset the helmet and restart the USB tool.")
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            if not chunk:
                raise RuntimeError("Helmet disconnected.")
            pending += chunk
            if len(pending) > 8192:
                raise RuntimeError("Unexpected USB response length.")
        line, trailing = pending.split(b"\n", 1)
        if trailing:
            self.out_of_sync = True
        try:
            return json.loads(line)
        except (ValueError, UnicodeDecodeError) as error:
            raise RuntimeError("Unexpected firmware response. Check that firmware 8.0 is installed.") from error

    def tick(self):
        pass

    def close(self):
        os.close(self.fd)
