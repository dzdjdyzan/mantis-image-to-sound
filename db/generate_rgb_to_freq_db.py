import sqlite3
import numpy as np

# Constants
FREQ_MIN = 20.0
FREQ_MAX = 20000.0
DB_PATH = 'rgb_to_frequency.db'
BATCH_SIZE = 100000  # number of rows per batch insert

def generate_database():
    # Generate logarithmically spaced frequencies for all RGB combinations
    num_combinations = 256 * 256 * 256
    frequencies = np.geomspace(FREQ_MIN, FREQ_MAX, num_combinations)
    
    # Connect to SQLite database
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    
    # Drop existing table and index if they exist (clean rebuild)
    cursor.execute('DROP TABLE IF EXISTS rgb_frequency')
    
    # Create table
    cursor.execute('''
        CREATE TABLE rgb_frequency (
            R INTEGER,
            G INTEGER,
            B INTEGER,
            Frequency REAL
        )
    ''')
    
    # Prepare batch insertion
    total_rows = 0
    batch_data = []
    
    print("Generating database... This will take a few minutes.")
    
    for r in range(256):
        for g in range(256):
            for b in range(256):
                idx = (r << 16) | (g << 8) | b  # faster than multiplication
                freq = frequencies[idx]
                batch_data.append((r, g, b, freq))
                total_rows += 1
                
                # When batch is full, insert and clear
                if len(batch_data) >= BATCH_SIZE:
                    cursor.executemany('INSERT INTO rgb_frequency (R, G, B, Frequency) VALUES (?, ?, ?, ?)', batch_data)
                    batch_data.clear()
                    print(f"Inserted {total_rows} rows...", end='\r')
    
    # Insert any remaining rows
    if batch_data:
        cursor.executemany('INSERT INTO rgb_frequency (R, G, B, Frequency) VALUES (?, ?, ?, ?)', batch_data)
    
    # Create index after all data is inserted (much faster)
    print("\nCreating index on (R, G, B)... This may take a few minutes.")
    cursor.execute('DROP INDEX IF EXISTS idx_rgb')
    cursor.execute('CREATE INDEX idx_rgb ON rgb_frequency (R, G, B)')
    
    conn.commit()
    conn.close()
    print(f"Database '{DB_PATH}' generated successfully with {total_rows} rows.")

if __name__ == '__main__':
    generate_database()