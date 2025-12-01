import ctypes

class MifareWriter:
    def __init__(self, lib_path="./libcard.so", reader_name="Your Reader Name", key="FFFFFFFFFFFF"):
        self.cardlib = ctypes.CDLL(lib_path)
        self.reader_name = reader_name.encode()
        self.key = key.encode()
        self._set_function_signatures()
        self.articles = {}

    def _set_function_signatures(self):
        self.cardlib.connectreader.argtypes = [ctypes.c_char_p]
        self.cardlib.connectreader.restype = ctypes.c_int
        self.cardlib.writeblockstring.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]
        self.cardlib.writeblockstring.restype = ctypes.c_int
        self.cardlib.cleanup.restype = None

    def connect(self):
        result = self.cardlib.connectreader(self.reader_name)
        if result != 0:
            raise RuntimeError("Failed to connect to reader")

    def write_articles(self, start_block=4):  # Add start_block parameter
        block = start_block
        for article, qty in self.articles.items():
            text = f"{article}:{qty}"[:16].encode()
            result = self.cardlib.writeblockstring(self.key, block, text)
            print(f"Wrote '{text.decode()}' to block {block}: {result}")
            block += 1


    def close(self):
        self.cardlib.cleanup()

    def set_articles(self, articles_dict):
        self.articles = articles_dict
