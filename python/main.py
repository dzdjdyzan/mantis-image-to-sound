import numpy as np
from image_processor import load_image_and_select_colors, process_all_segments
from scipy.io.wavfile import write
from scipy.interpolate import interp1d
import tkinter as tk
from tkinter import filedialog
import sys
import os

# Constants
IMAGE_WIDTH = 1080
IMAGE_HEIGHT = 1080
DESIRED_NUM_SEGMENTS = 120
SAMPLE_RATE = 48000
DURATION_SECONDS = 30.0
DB_PATH = '../db/rgb_to_frequency.db'

def select_image_file():
    """Open a file dialog to select an image file."""
    root = tk.Tk()
    root.withdraw()
    file_path = filedialog.askopenfilename(
        title="Select an image",
        filetypes=[
            ("Image files", "*.jpg *.jpeg *.png *.bmp *.tiff"),
            ("All files", "*.*")
        ]
    )
    root.destroy()
    return file_path

def fetch_frequencies(db_path, top_colors):
    import sqlite3
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    color_freq_list = []

    print(f"Fetching frequencies from rgb to frequency database...")
    for color in top_colors:
        color_int = tuple(int(c) for c in color)
        try:
            cursor.execute('SELECT Frequency FROM rgb_frequency WHERE R = ? AND G = ? AND B = ?', color_int)
            freq = cursor.fetchone()
            if freq:
                frequency = freq[0]
                angular_frequency = 2 * np.pi * frequency / SAMPLE_RATE
                color_freq_list.append((color_int, frequency, angular_frequency))
                
            else:
                print(f"No frequency found for color {color_int}")
        except Exception as e:
            print(f"Error querying for color {color_int}: {e}")

    color_freq_list.sort(key=lambda x: x[1], reverse=True)
    return color_freq_list

def fm_synthesis(carrier_freq, modulator_freq, mod_index, total_samples, sample_rate):
    """Apply frequency modulation synthesis."""
    t = np.arange(total_samples) / sample_rate
    modulator = np.sin(2 * np.pi * modulator_freq * t)
    return np.sin(2 * np.pi * carrier_freq * t + mod_index * modulator)

def main(image_path):
    # Load image and preprocess using the C++ module
    pixels, unique_selected_colors = load_image_and_select_colors(
        image_path, IMAGE_WIDTH, IMAGE_HEIGHT, DESIRED_NUM_SEGMENTS
    )

    print("Image loaded successfully.")

    NUM_SEGMENTS = len(unique_selected_colors)

    # Fetch frequencies for the top colors
    color_freq_3uple = fetch_frequencies(DB_PATH, unique_selected_colors)
    print("Frequencies fetched from database succesfully.")

    # Extract the frequencies from the tuples
    frequencies = [t[1] for t in color_freq_3uple]

    # Calculate the average frequency
    average_frequency = np.mean(frequencies)

    # Compute samples per column dynamically
    samples_per_column = int((SAMPLE_RATE * DURATION_SECONDS - IMAGE_WIDTH) / (IMAGE_WIDTH + 2))
    # Recalculate total_samples to be exact
    total_samples = IMAGE_WIDTH + samples_per_column * (IMAGE_WIDTH + 2)

    print("Generating audio signal from colour data of image left to right per column.")
    # Process all segments in C++ for speed
    results = process_all_segments(
        pixels, color_freq_3uple, total_samples, NUM_SEGMENTS, samples_per_column
    )
    print("Audio signal generated.")

    # Combine all audio buffers with FM synthesis
    final_audio = np.zeros(total_samples, dtype=np.float32)
    mod_index = average_frequency / 2000

    # Frequency weighting (equal‑loudness curve)
    freq_points = np.array([20, 40, 80, 160, 315, 630, 1000, 1250, 1615, 1875, 2104, 2500, 3333, 4166, 5000, 9166, 10000, 15000, 20000])
    spl_db = np.array([55.0, 46.11, 40.0, 33.89, 30.56, 27.78, 27.22, 28.33, 29.44, 28.33, 27.22, 25.56, 25.0, 25.56, 27.78, 33.33, 34.44, 33.89, 31.67])

    # Normalize amplitudes based on human hearing response, numbers approximated from equal loudness contour
    num_rgb_values = 255 * 255 * 255
    frequencies_interpolated = np.linspace(20, 20000, num_rgb_values)
    spl_interpolation_function = interp1d(freq_points, spl_db, kind='linear', fill_value="extrapolate")
    spl_interpolated_values = spl_interpolation_function(frequencies_interpolated)
    dbfs_values = spl_interpolated_values - 56.0
    amplitude_values = 10 ** (dbfs_values / 20)
    print("Amplitude values for frequencies based on human hearing response ready.")

    for segment, result in enumerate(results):
        carrier_freq = color_freq_3uple[segment][1]
        modulator_freq = 0.01 * color_freq_3uple[NUM_SEGMENTS - segment - 1][1]

        fm_signal = fm_synthesis(carrier_freq, modulator_freq, mod_index, total_samples, SAMPLE_RATE)

        idx = np.argmin(np.abs(frequencies_interpolated - carrier_freq))
        weight = amplitude_values[idx]

        final_audio += fm_signal * result * weight
    print("Frequency modulation applied to generated audio signal.")

    final_audio -= np.mean(final_audio)
    max_val = np.max(np.abs(final_audio))
    if max_val > 0:
        final_audio /= max_val
        final_audio *= 0.9
    print("Audio signal normalized.")

    #base name of input image + .wav
    base_name = os.path.splitext(os.path.basename(image_path))[0]
    output_path = base_name + '.wav'   # saved in current working directory

    write(output_path, SAMPLE_RATE, final_audio.astype(np.float32))
    print(f"Saved audio to: {os.path.abspath(output_path)}")

if __name__ == "__main__":
    if len(sys.argv) == 2:
        image_path = sys.argv[1]
    else:
        image_path = select_image_file()
        if not image_path:
            print("No image selected. Exiting.")
            sys.exit(1)

    if not os.path.exists(image_path):
        print(f"Error: Image file '{image_path}' does not exist.")
        sys.exit(1)

    main(image_path)