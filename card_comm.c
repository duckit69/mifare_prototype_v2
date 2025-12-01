// Compile: gcc -shared -o libcard.so -fPIC card_comm.c $(pkg-config --cflags --libs libpcsclite)  

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PCSC/winscard.h>

// Global context and card handle
SCARDCONTEXT hContext = 0; // The session context to PC/SC system 
SCARDHANDLE hCard = 0; // The live connection to a card 
DWORD dwActiveProtocol = 0; // The communication protocol negotiated 
static char error_msg[512];

// Get last error message
char* get_last_error() {
    return error_msg;
}

// List all available readers
// Returns: Comma separated string of reader names, or error message on failure
char* list_readers() {
    static char readers[2048]; // Static buffer to hold reader names
    DWORD dwReaders = sizeof(readers); // Size of buffer
    LONG rv; // Return value for PC/SC function calls
    
    memset(readers, 0, sizeof(readers)); // Clear buffer in other words reserve 2048b and write 0 in all of it 
     
    // Establish context - creates connection to PC/SC (Personal Computer/Smart Card) Resource Manager
    // It returns a handler we store it in a global variable hContext to be used by all functions
    // SCARD_SCOPE_SYSTEM: operate at system level (requires permissions) and can access all readers
    rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &hContext);
    if (rv != SCARD_S_SUCCESS) {
        sprintf(readers, "ERROR: Cannot establish context (0x%08lX)", rv);
        return readers;
    }
    
    // Get list of readers
    // returns multi-string (null-separated strings)
    rv = SCardListReaders(hContext, NULL, readers, &dwReaders);
    if (rv != SCARD_S_SUCCESS) {
        sprintf(readers, "ERROR: No readers found (0x%08lX)", rv);
        return readers;
    }
    
    // Convert multi-string to comma-separated
    char temp[2048];  // Temporary buffer for conversion
    memset(temp, 0, sizeof(temp));
    char* p = readers; // Pointer to walk through multi-string
    int first = 1; // Flag to track first reader (for comma position)
    
    // Walk through "\0" separated strings
    while (*p) {
        if (!first) strcat(temp, ","); // Add comma before subsequent readers
        strcat(temp, p); // Append reader name
        p += strlen(p) + 1; // Move to next string (skip null terminator)
        first = 0; // disable the first reader flag
    }
    
    strcpy(readers, temp); // Copy formatted result back to static buffer
    return readers; // Return pointer to static buffer
}

// Connect to a specific reader
// Parameters:
//   reader_name: Name of the reader to connect to (from list_readers)
// Returns: 0 on success, -1 on failure
int connect_reader(const char* reader_name) {
    LONG rv; // Return value for PC/SC function calls
    
    // Disconnect if already connected (prevents multiple connections)
    if (hCard) {
        SCardDisconnect(hCard, SCARD_LEAVE_CARD); // Leave card powered
        hCard = 0; // Clear handle
    }
    // Connect to the card in the specified reader
    // SCARD_SHARE_SHARED: Allow other applications to use the card simultaneously
    // SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1: Accept either T=0 or T=1 protocol
    // hCard: Receives connection handle on success
    // dwActiveProtocol: Receives the actual protocol negotiated with the card
    rv = SCardConnect(hContext, reader_name, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &hCard, &dwActiveProtocol);
    
    if (rv != SCARD_S_SUCCESS) {
        sprintf(error_msg, "SCardConnect failed (0x%08lX)", rv);
        return -1;
    }

    // Store success message with protocol information
    // T0: Character-oriented protocol (older, byte-by-byte transmission)
    // T1: Block-oriented protocol (newer, packet-based transmission)
    
    sprintf(error_msg, "Connected using protocol: %s", (dwActiveProtocol == SCARD_PROTOCOL_T0) ? "T0" : "T1");
    
    return 0;
}

// Disconnect from card
void disconnect_card() {
    // if connection handler with card exist ( connected to reader ) 
    // disconnect and set handler to empty (0)
    if (hCard) {
        SCardDisconnect(hCard, SCARD_LEAVE_CARD);
        hCard = 0;
    }
}

// Convert string to hex (for writing)
int string_to_hex(const char* str, BYTE* bytes, int max_len) {
    int len = strlen(str);
    if (len > max_len)
        len = max_len;
        
    // Copy each character as its ASCII byte value
    // Example: "Hi" becomes [0x48, 0x69] (ASCII codes for 'H' and 'i')
    for (int i = 0; i < len; i++) {
        bytes[i] = (BYTE)str[i];
    }
    
    // rest of empty spaces fill with zeros ( 16 bytes MIFARE block size)
    for (int i = len; i < 16 && i < max_len; i++) {
        bytes[i] = 0x00;
    }
    // Return padded length (16) or actual length if string was longer
    // that means we return min(16, len)
    return (len < 16) ? 16 : len;
}

// Convert hexadecimal string to byte array
// hex: Input hex string (e.g., "FF A0 6B" or "FFA06B")
// bytes: Output buffer to store converted bytes
int hex_to_bytes(const char* hex, BYTE* bytes, int max_len) {
    int len = 0; // Count of bytes written
    const char* p = hex; // Pointer to walk through hex string
    
    // Skip spaces and convert pairs of hex characters to bytes
    while (*p && len < max_len) {
        // Skip whitespace characters (allows formats like "FF A0 6B")
        if (*p == ' ') {
            p++;
            continue;
        }
        // Parse next 2 hex characters as a single byte
        // %2hhx: Read exactly 2 characters as hexadecimal into a byte
        // Example: "FF" -> 0xFF, "A0" -> 0xA0, "6B" -> 0x6B
        if (sscanf(p, "%2hhx", &bytes[len]) == 1) {
            len++; // Successfully converted one byte
            p += 2; // Move pointer forward by 2 characters
        } else {
            break;
        }
    }
    return len;
}

// Convert byte array to printable ASCII string 
void bytes_to_string(BYTE* bytes, int len, char* str, int max_str_len) {
    int j = 0; // Output string position counter
    for (int i = 0; i < len && j < max_str_len - 1; i++) {
        // Only convert printable ASCII characters (space through tilde)
        // ASCII 32-126 includes letters, numbers, punctuation, symbols
        if (bytes[i] >= 32 && bytes[i] <= 126) {
            str[j++] = bytes[i]; // Copy printable character directly
        } else if (bytes[i] == 0x00) {
            // Stop at "\0" terminator
            break;
        } else {
            // Replace non-printable with '.' (like hex editors do)
            // This includes control characters (0x01-0x1F) and extended ASCII (127+)
            str[j++] = '.';
        }
    }
    str[j] = '\0'; // Add "\0" terminator to make valid C string
}


// Load MIFARE authentication key into reader's volatile memory
// The key must be loaded before authenticating blocks for read/write operations
//   key: 6-byte key as hex string ("FF FF FF FF FF FF" )
//   key_location: Memory slot in reader (typically 0x00 or 0x01)
int load_key(const char* key, int key_location) {
    BYTE pbRecvBuffer[258]; // prepare space for card response
    DWORD dwRecvLength = sizeof(pbRecvBuffer); // Response size
    BYTE pbSendBuffer[11]; // APDU command space (5 header (class-inst-p1-p2-lc) + data: 6 key bytes)
    LONG rv; // PC/SC return value
    
    if (!hCard) {
        sprintf(error_msg, "Not connected to card");
        return -1;
    }
    
    // Build APDU command: FF 82 20 [KeyLoc] 06 [6-byte key]
    // In our reader MIFARE keys can be stored in the first 32 open spots 0-31 because it is the only pockets that has 6bytes ( rest pockets eitehr have 8-16)
    // UPDATED: Try different P1/P2 parameters
    // Some readers need P1=0x20, P2=key_location
    pbSendBuffer[0] = 0xFF;  // CLA
    pbSendBuffer[1] = 0x82;  // INS "Load Key" operation in OMNIKEY's APDU 
    pbSendBuffer[2] = 0x20;  // 0x20 volatile memory 
    pbSendBuffer[3] = key_location & 0xFF;  // P2 Key slot (0x00 or 0x01)
    pbSendBuffer[4] = 0x06;  // Lc (key length = 6 bytes)
    
    // Convert hex key string to bytes and place in command buffer
    // Example: "FF FF FF FF FF FF" -> [0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]
    int key_len = hex_to_bytes(key, &pbSendBuffer[5], 6);
    if (key_len != 6) {
        sprintf(error_msg, "Invalid key length: expected 12 hex chars, got %d bytes", key_len);
        return -1;
    }
    
    // Select protocol structure based on active protocol (T0 or T1)
    SCARD_IO_REQUEST* pioSendPci = (dwActiveProtocol == SCARD_PROTOCOL_T0) ? SCARD_PCI_T0 : SCARD_PCI_T1;
    
    // Send APDU command to reader
    rv = SCardTransmit(hCard, pioSendPci, pbSendBuffer, 11, NULL, pbRecvBuffer, &dwRecvLength);
    
    if (rv != SCARD_S_SUCCESS) {
        sprintf(error_msg, "SCardTransmit failed (0x%08lX)", rv);
        return -1;
    }
    
    // Check response status word (SW1 SW2)
    if (dwRecvLength >= 2) {
        if (pbRecvBuffer[0] == 0x90 && pbRecvBuffer[1] == 0x00) {
            sprintf(error_msg, "Key loaded successfully");
            return 0;
        } else if (pbRecvBuffer[0] == 0x69 && pbRecvBuffer[1] == 0x86) {
            sprintf(error_msg, "Load key not supported - trying alternative method");
            return -1;
        } else {
            sprintf(error_msg, "Load key failed: SW=%02X%02X", 
                    pbRecvBuffer[0], pbRecvBuffer[1]);
            return -1;
        }
    }
    
    sprintf(error_msg, "Invalid response length: %lu", dwRecvLength);
    return -1;
}


// Authenticate a block using loaded key
// key_type: Authentication key type, use 0x60 for Key A, or 0x61 for Key B
// key_location: Location of the loaded key in reader memory (0x00 or 0x01)
int authenticate_block(int block_number, int key_type, int key_location) {
    BYTE pbRecvBuffer[258]; // Buffer for receiving card response
    DWORD dwRecvLength = sizeof(pbRecvBuffer); // Size of receive buffer
    BYTE pbSendBuffer[10]; // APDU command buffer
    LONG rv; // PC/SC return code
    
    if (!hCard) {
        sprintf(error_msg, "Not connected to card");
        return -1;
    }
    // Build APDU command to authenticate block:
    // FF 86 00 00 05 01 00 [Block Number] [Key Type] [Key Location]
    pbSendBuffer[0] = 0xFF;  // CLA
    pbSendBuffer[1] = 0x86;  // INS (General Authenticate)
    pbSendBuffer[2] = 0x00;  // P1
    pbSendBuffer[3] = 0x00;  // P2
    pbSendBuffer[4] = 0x05;  // Lc
    pbSendBuffer[5] = 0x01;  // Version
    pbSendBuffer[6] = 0x00;  // Block MSB
    pbSendBuffer[7] = block_number & 0xFF;  // Block LSB
    pbSendBuffer[8] = key_type & 0xFF;  // Key type (60=A, 61=B)
    pbSendBuffer[9] = key_location & 0xFF;  // Key location


    // Select correct protocol (T=0 or T=1) PCI structure
    SCARD_IO_REQUEST* pioSendPci = (dwActiveProtocol == SCARD_PROTOCOL_T0) ? SCARD_PCI_T0 : SCARD_PCI_T1;
    
    // Send APDU
    rv = SCardTransmit(hCard, pioSendPci, pbSendBuffer, 10, NULL, pbRecvBuffer, &dwRecvLength);
    
    if (rv != SCARD_S_SUCCESS) {
        sprintf(error_msg, "Auth SCardTransmit failed (0x%08lX)", rv);
        return -1;
    }
    
    // Check response
    if (dwRecvLength >= 2) {
        if (pbRecvBuffer[0] == 0x90 && pbRecvBuffer[1] == 0x00) {
            sprintf(error_msg, "Authentication successful");
            return 0;
        } else {
            sprintf(error_msg, "Authentication failed: SW=%02X%02X", 
                    pbRecvBuffer[0], pbRecvBuffer[1]);
            return -1;
        }
    }
    
    sprintf(error_msg, "Invalid auth response length: %lu", dwRecvLength);
    return -1;
}

// Read a 16-byte block 
// key: Hex string representing the authentication key
// Returns: Pointer to a static buffer containing readable string representation 
//          of block data and its hex dump, or error message if failed
char* read_block_string(const char* key, int block_number) {
    static char result[512]; // Static buffer to hold the result string
    BYTE pbRecvBuffer[258]; // Buffer for data received from card
    DWORD dwRecvLength = sizeof(pbRecvBuffer); // Receive buffer size
    LONG rv; // return value of PC/SC package
    
    memset(result, 0, sizeof(result));  // Clear previous results

    // Ensure connection to card
    if (!hCard) {
        strcpy(result, "ERROR: Not connected to card");
        return result;
    }
    // Step 1: Load authentication key into reader
    // Uses volatile key storage slot 0x00, and P1=0x20 
    if (load_key(key, 0x00) != 0) {
        sprintf(result, "ERROR: %s", error_msg);
        return result;
    }

    // Step 2: Authenticate the target block using Key A (0x60) and key slot 0x00
    if (authenticate_block(block_number, 0x60, 0x00) != 0) {
        sprintf(result, "ERROR: %s", error_msg);
        return result;
    }

    // Step 3: Send Read Binary APDU command to read 16 bytes from the block
    // APDU command structure: FF B0 00 [BlockNumber] 10 (read 16 bytes)
    BYTE pbSendBuffer[] = {0xFF, 0xB0, 0x00, (BYTE)block_number, 0x10};
    
    // Choose IO request structure based on active protocol (T0 or T1)
    SCARD_IO_REQUEST* pioSendPci = (dwActiveProtocol == SCARD_PROTOCOL_T0) ? SCARD_PCI_T0 : SCARD_PCI_T1;
    
    // Transmit APDU to card and receive block data
    rv = SCardTransmit(hCard, pioSendPci, pbSendBuffer, sizeof(pbSendBuffer), NULL, pbRecvBuffer, &dwRecvLength);
    
    if (rv != SCARD_S_SUCCESS) {
        sprintf(result, "ERROR: Read failed (0x%08lX)", rv);
        return result;
    }
    
    if (dwRecvLength < 2) {
        strcpy(result, "ERROR: Invalid response");
        return result;
    }
    
    // Check status word in response (last two bytes)
    if (pbRecvBuffer[dwRecvLength-2] != 0x90 || pbRecvBuffer[dwRecvLength-1] != 0x00) {
        sprintf(result, "ERROR: Read failed [SW=%02X%02X]", 
                pbRecvBuffer[dwRecvLength-2], pbRecvBuffer[dwRecvLength-1]);
        return result;
    }
    
    // Convert received bytes (excluding status words) to a human-readable string
    char text[128];
    bytes_to_string(pbRecvBuffer, dwRecvLength - 2, text, sizeof(text));
    
    // Convert the bytes to hex string for debugging
    char hex[256] = "";
    for (DWORD i = 0; i < dwRecvLength - 2; i++) {
        sprintf(hex + strlen(hex), "%02X ", pbRecvBuffer[i]);
    }
    
    // Format final result with readable text and hexdump
    sprintf(result, "%s\n[Hex: %s]", text, hex);
    
    return result;
}


// Write a 16-byte block
int write_block_string(const char* key, int block_number, const char* text) {
    BYTE pbRecvBuffer[258]; // Buffer to receive response from card
    DWORD dwRecvLength = sizeof(pbRecvBuffer); // Response buffer size
    BYTE pbSendBuffer[256]; // Buffer to build APDU command
    BYTE data[16]; // 16-byte data block to send
    LONG rv;
    
    if (!hCard) {
        sprintf(error_msg, "Not connected to card");
        return -1;
    }
    
    // Step 1: Load authentication key into reader
    if (load_key(key, 0x00) != 0) {
        return -1;
    }
    
    // Step 2: Authenticate block with Key A and loaded key slot 0x00
    if (authenticate_block(block_number, 0x60, 0x00) != 0) {
        return -2;
    }
    
    // Step 3: Convert input text string into a 16-byte data array
    // Pads with zeros if text is shorter than block size
    int data_len = string_to_hex(text, data, 16);
    
    // Step 4: Build Update Binary APDU command to write block data
    // Command format: FF D6 00 [BlockNumber] [DataLength] [Data...]
    pbSendBuffer[0] = 0xFF;  // CLA
    pbSendBuffer[1] = 0xD6;  // INS (Update/Write Binary)
    pbSendBuffer[2] = 0x00;  // P1
    pbSendBuffer[3] = (BYTE)block_number;  // P2 - block to write
    pbSendBuffer[4] = data_len;  // Lc

    // Copy 16 bytes of data to send buffer
    memcpy(&pbSendBuffer[5], data, data_len);

    // Select proper PCI structure (T=0 or T=1)
    SCARD_IO_REQUEST* pioSendPci = (dwActiveProtocol == SCARD_PROTOCOL_T0) ? SCARD_PCI_T0 : SCARD_PCI_T1;
    
    // Step 5: Transmit APDU to card
    rv = SCardTransmit(hCard, pioSendPci, pbSendBuffer, 5 + data_len, NULL, pbRecvBuffer, &dwRecvLength);
    
    // Step 6: Check return status
    if (rv != SCARD_S_SUCCESS) {
        sprintf(error_msg, "Write failed (0x%08lX)", rv);
        return -3;
    }
    
    // Check response status words (SW1 SW2)
    if (dwRecvLength >= 2 && pbRecvBuffer[0] == 0x90 && pbRecvBuffer[1] == 0x00) {
        sprintf(error_msg, "Write successful");
        return 0;
    }
    
    sprintf(error_msg, "Write failed: SW=%02X%02X", 
            pbRecvBuffer[0], pbRecvBuffer[1]);
    return -4;
}
// Cleanup
void cleanup() {
    disconnect_card();
    if (hContext) {
        SCardReleaseContext(hContext);
        hContext = 0;
    }
}
