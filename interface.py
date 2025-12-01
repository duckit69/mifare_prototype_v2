import sys
from PyQt5.QtWidgets import (QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, QWidget, QTableWidget, QTableWidgetItem, QPushButton, QLineEdit, QLabel, QGroupBox)
from PyQt5.QtCore import pyqtSignal, QObject
from api_client import fetch_article
from mifare_writer import MifareWriter

class ArticleSignals(QObject):
    article_added = pyqtSignal(str)  # article_name

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.signals = ArticleSignals()
        self.articles = {}
        self.writer = None
        self.init_ui()
        self.connect_signals()
        
    def init_ui(self):
        self.setWindowTitle("RFID Batch → MIFARE Writer")
        self.setGeometry(100, 100, 900, 600)
        
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        
        # Articles Table
        table_group = QGroupBox("Scanned Articles")
        table_layout = QVBoxLayout(table_group)
        
        self.table = QTableWidget()
        self.table.setColumnCount(3)
        self.table.setHorizontalHeaderLabels(["Article", "Quantity", "Action"])
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.setRowCount(0)
        table_layout.addWidget(self.table)
        layout.addWidget(table_group)
        
        # Controls
        controls = QHBoxLayout()

        # RFID Input Field (replaces Start/Stop buttons)
        self.rfid_input = QLineEdit()
        self.rfid_input.setPlaceholderText("RFID reader types here → Press Enter")
        self.rfid_input.returnPressed.connect(self.scan_rfid_input)
        self.rfid_input.setStyleSheet("font-size: 16px; padding: 10px;")  # Make it obvious
        controls.addWidget(QLabel("RFID:"))
        controls.addWidget(self.rfid_input)

        self.clear_btn = QPushButton("Clear Table")
        self.clear_btn.clicked.connect(self.clear_articles)
        controls.addWidget(self.clear_btn)

        controls.addStretch()
        layout.addLayout(controls)
        
        # MIFARE Write Section
        write_group = QGroupBox("Write to MIFARE Card")
        write_layout = QHBoxLayout(write_group)
        
        self.block_input = QLineEdit("4")
        self.block_input.setPlaceholderText("Start block (e.g. 4)")
        write_layout.addWidget(QLabel("Block:"))
        write_layout.addWidget(self.block_input)
        
        self.connect_btn = QPushButton("Connect Reader")
        self.connect_btn.clicked.connect(self.connect_reader)
        write_layout.addWidget(self.connect_btn)
        
        self.write_btn = QPushButton("WRITE ALL TO CARD")
        self.write_btn.clicked.connect(self.write_to_card)
        self.write_btn.setEnabled(False)
        write_layout.addWidget(self.write_btn)
        
        layout.addWidget(write_group)
        
        self.status_label = QLabel("Ready - Scan tags with 125kHz reader")
        layout.addWidget(self.status_label)
        # Read

        read_group = QGroupBox("Read Data Blocks")
        read_layout = QHBoxLayout(read_group)

        self.key_read_input = QLineEdit()
        self.key_read_input.setPlaceholderText("Auth Key hex (FFFFFFFFFFFF)")
        read_layout.addWidget(QLabel("Key:"))
        read_layout.addWidget(self.key_read_input)

        self.block_read_input = QLineEdit()
        self.block_read_input.setPlaceholderText("Block number (1)")
        read_layout.addWidget(QLabel("Block:"))
        read_layout.addWidget(self.block_read_input)

        self.read_btn = QPushButton("Read Block")
        self.read_btn.clicked.connect(self.read_block)
        read_layout.addWidget(self.read_btn)

        self.read_result = QLabel("Data:")
        read_layout.addWidget(self.read_result)

        layout.addWidget(read_group)
    def connect_signals(self):
        self.signals.article_added.connect(self.add_article)
        
    def scan_rfid_input(self):
        """Handle RFID input from QLineEdit - API returns ONLY article name"""
        tag_id = self.rfid_input.text().strip()
        if tag_id:
            # Same logic as before - fetch article and add to table
            data = fetch_article(tag_id)
            if data:
                article = data['article']
                self.signals.article_added.emit(article)
            else:
                self.status_label.setText(f"❌ Unknown RFID: {tag_id}")
            
            self.rfid_input.clear()
            self.rfid_input.setFocus()  # Keep focus for next scan

    def add_article(self, article):
        # Always add quantity 1 per scan
        if article in self.articles:
            self.articles[article] += 1
        else:
            self.articles[article] = 1
        
        self.update_table()
        self.status_label.setText(f"✓ Added {article} x1 | Total unique items: {len(self.articles)}")

    def update_table(self):
        self.table.setRowCount(len(self.articles))
        row = 0
        for article, qty in self.articles.items():
            self.table.setItem(row, 0, QTableWidgetItem(article))
            self.table.setItem(row, 1, QTableWidgetItem(str(qty)))
            
            # Remove button per row
            remove_btn = QPushButton("Remove")
            remove_btn.clicked.connect(lambda _, a=article: self.remove_article(a))
            self.table.setCellWidget(row, 2, remove_btn)
            row += 1

        
    def remove_article(self, article):
        if article in self.articles:
            del self.articles[article]
            self.update_table()
            
    def clear_articles(self):
        self.articles.clear()
        self.update_table()
        self.status_label.setText("Table cleared")
        
    def connect_reader(self):
        try:
            reader_name = "Your MIFARE Reader Name"  # Update from listreaders()
            self.writer = MifareWriter(reader_name=reader_name)
            self.writer.connect()
            self.write_btn.setEnabled(True)
            self.status_label.setText("✅ MIFARE reader connected")
        except Exception as e:
            self.status_label.setText(f"❌ Reader connect failed: {e}")
            
    def write_to_card(self):
        if not self.writer or not self.articles:
            return
            
        try:
            block = int(self.block_input.text())
            self.writer.set_articles(self.articles)
            self.writer.write_articles(block)  # Pass start block
            self.status_label.setText(f"✅ Wrote {len(self.articles)} articles starting block {block}")
        except Exception as e:
            self.status_label.setText(f"❌ Write failed: {e}")

    def read_block(self):
        if not self.writer:
            self.status_label.setText("❌ Connect the reader first!")
            return
        try:
            key = self.key_read_input.text().strip().replace(" ", "")
            if not key:
                self.status_label.setText("❌ Authentication key required")
                return
            block_num = int(self.block_read_input.text())
            data = self.writer.read_block_string(key.encode(), block_num)
            self.read_result.setText(f"Data (Block {block_num}): {data}")
            self.status_label.setText(f"✅ Read block {block_num} successful")
        except Exception as e:
            self.status_label.setText(f"❌ Read error: {e}")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())
