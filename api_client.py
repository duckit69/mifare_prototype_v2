# api_client.py - Local RFID to Article mapping
rfid_to_article_mapping = {
    "RFID_1234": {"article": "Premium Coffee Beans"},
    "RFID_5678": {"article": "Organic Green Tea"},
    "RFID_9012": {"article": "Pure Maple Syrup"},
    "RFID_1111": {"article": "Premium Coffee Beans"},  # Same article
    "RFID_2222": {"article": "Organic Green Tea"},     # Same article
}

def fetch_article(rfid_id):
    """Returns article data for RFID ID from local mapping"""
    article_data = rfid_to_article_mapping.get(rfid_id)
    if article_data:
        print(f"Found: {rfid_id} → {article_data['article']}")
        return article_data
    else:
        print(f"Unknown RFID: {rfid_id}")
        return None
