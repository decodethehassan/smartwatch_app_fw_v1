import sys
import asyncio

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QTextEdit, QListWidget, QListWidgetItem,
    QMessageBox, QTabWidget
)
from PySide6.QtCore import Qt

from qasync import QEventLoop, asyncSlot
from bleak import BleakScanner, BleakClient

LOG_NOTIFY_UUID = "9f7b0001-6c35-4d2c-9c85-4a8c1a2b3c4d"


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("BLE Log Viewer (Tabbed)")
        self.resize(1000, 620)

        self.client: BleakClient | None = None
        self.devices = {}

        # Buffer to reassemble fragmented notifications into full lines
        self._rx_buf = ""

        # -------- Widgets --------
        self.device_list = QListWidget()

        self.scan_btn = QPushButton("Scan")
        self.connect_btn = QPushButton("Connect")
        self.disconnect_btn = QPushButton("Disconnect")
        self.clear_btn = QPushButton("Clear Current Tab")

        self.status_lbl = QLabel("Status: Idle")

        # Tabs: All + per-module
        self.tabs = QTabWidget()
        self.logs = {}  # name -> QTextEdit

        self._ensure_tab("All")  # create All tab

        # -------- Layout --------
        root = QWidget()
        main = QHBoxLayout(root)

        # LEFT: device list
        left = QVBoxLayout()
        left.addWidget(QLabel("Discovered BLE Devices"))
        left.addWidget(self.device_list)

        btn_row = QHBoxLayout()
        btn_row.addWidget(self.scan_btn)
        btn_row.addWidget(self.connect_btn)
        btn_row.addWidget(self.disconnect_btn)

        left.addLayout(btn_row)
        left.addWidget(self.clear_btn)
        left.addWidget(self.status_lbl)
        main.addLayout(left, 3)

        # RIGHT: tabs
        right = QVBoxLayout()
        right.addWidget(QLabel("Logs (grouped by module)"))
        right.addWidget(self.tabs)
        main.addLayout(right, 7)

        self.setCentralWidget(root)

        # -------- Signals --------
        self.scan_btn.clicked.connect(self.on_scan)
        self.connect_btn.clicked.connect(self.on_connect)
        self.disconnect_btn.clicked.connect(self.on_disconnect)
        self.clear_btn.clicked.connect(self.on_clear_current_tab)

    # -------- Tab helpers --------
    def _ensure_tab(self, name: str) -> QTextEdit:
        if name in self.logs:
            return self.logs[name]

        w = QTextEdit()
        w.setReadOnly(True)
        self.logs[name] = w
        self.tabs.addTab(w, name)
        return w

    def _append(self, tab_name: str, text: str):
        w = self._ensure_tab(tab_name)
        w.append(text)
        w.ensureCursorVisible()

    def set_status(self, text: str):
        self.status_lbl.setText(f"Status: {text}")

    def on_clear_current_tab(self):
        idx = self.tabs.currentIndex()
        if idx < 0:
            return
        w = self.tabs.widget(idx)
        if isinstance(w, QTextEdit):
            w.clear()

    # -------- BLE Scan --------
    @asyncSlot()
    async def on_scan(self):
        self.set_status("Scanning...")
        self._append("All", "Scanning for BLE devices...")
        self.device_list.clear()
        self.devices.clear()

        try:
            devices = await BleakScanner.discover(timeout=5.0)
        except Exception as e:
            self.set_status("Scan failed")
            QMessageBox.critical(self, "Scan failed", str(e))
            return

        if not devices:
            self.set_status("No devices found")
            self._append("All", "No devices found.")
            return

        for d in devices:
            name = d.name or "(Unknown)"
            addr = d.address
            self.devices[addr] = d

            item = QListWidgetItem(f"{name} | {addr}")
            item.setData(Qt.UserRole, addr)
            self.device_list.addItem(item)

        self.set_status(f"Found {len(devices)} device(s)")
        self._append("All", f"Found {len(devices)} device(s).")

    # -------- Notifications (reassemble chunks into full lines) --------
    def on_notify(self, sender: int, data: bytearray):
        chunk = bytes(data).decode("utf-8", errors="replace")
        self._rx_buf += chunk

        # Normalize newlines
        self._rx_buf = self._rx_buf.replace("\r\n", "\n").replace("\r", "\n")

        # Flush complete lines only
        while "\n" in self._rx_buf:
            line, self._rx_buf = self._rx_buf.split("\n", 1)
            line = line.strip()
            if not line:
                continue

            # Example line:
            #   as6221_demo: [AS6221] ...
            #   lsm6dso_app: [LSM6DSO] ...
            if ":" in line:
                module = line.split(":", 1)[0].strip()
            else:
                module = "Unknown"

            # Always append to All
            self._append("All", line)

            # Append to module tab
            self._append(module, line)

    # -------- BLE Connect --------
    @asyncSlot()
    async def on_connect(self):
        item = self.device_list.currentItem()
        if not item:
            QMessageBox.warning(self, "Select device", "Please select a device first.")
            return

        address = item.data(Qt.UserRole)

        if self.client:
            await self.on_disconnect()

        self._rx_buf = ""

        self.set_status(f"Connecting to {address} ...")
        self._append("All", f"Connecting to {address} ...")

        self.client = BleakClient(address, timeout=10.0)

        try:
            await self.client.connect()
        except Exception as e:
            self.set_status("Connection failed")
            QMessageBox.critical(self, "Connection failed", str(e))
            self.client = None
            return

        self.set_status("Connected")
        self._append("All", "Connected.")

        # Service discovery (helps confirm UUID exists)
        try:
            services = await self.client.get_services()
            found = False
            for s in services:
                for c in s.characteristics:
                    if c.uuid.lower() == LOG_NOTIFY_UUID.lower():
                        props = list(c.properties) if c.properties else []
                        self._append("All", f"Found char {c.uuid} props={props}")
                        found = True
                        break
                if found:
                    break
            if not found:
                self.set_status("Notify UUID not found")
                QMessageBox.critical(
                    self,
                    "UUID not found",
                    f"Notify characteristic not found:\n{LOG_NOTIFY_UUID}"
                )
                await self.on_disconnect()
                return
        except Exception:
            pass

        self.set_status("Enabling notifications...")
        self._append("All", f"Starting notify on: {LOG_NOTIFY_UUID}")

        try:
            await self.client.start_notify(LOG_NOTIFY_UUID, self.on_notify)
        except Exception as e:
            self.set_status("Notify failed")
            QMessageBox.critical(
                self,
                "Notify failed",
                f"Could not start notify for UUID:\n{LOG_NOTIFY_UUID}\n\nError:\n{e}"
            )
            await self.on_disconnect()
            return

        self.set_status("Streaming logs")
        self._append("All", "✅ Notifications enabled. Waiting for logs...")

    # -------- BLE Disconnect --------
    @asyncSlot()
    async def on_disconnect(self):
        if self.client:
            try:
                try:
                    await self.client.stop_notify(LOG_NOTIFY_UUID)
                except Exception:
                    pass
                await self.client.disconnect()
            except Exception:
                pass

            self.client = None

        self._rx_buf = ""
        self.set_status("Disconnected")
        self._append("All", "Disconnected.")


def main():
    if sys.platform.startswith("win"):
        try:
            asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
        except Exception:
            pass

    app = QApplication(sys.argv)
    loop = QEventLoop(app)
    asyncio.set_event_loop(loop)

    win = MainWindow()
    win.show()

    with loop:
        loop.run_forever()


if __name__ == "__main__":
    main()
