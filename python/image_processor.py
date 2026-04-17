import numpy as np
import image_processor_cpp

def load_image_and_select_colors(image_path, width, height, num_colors):
    from PIL import Image
    # Load and resize image
    img = Image.open(image_path).convert('RGB').resize((width, height))
    # Convert to NumPy array (height, width, 3) uint8
    img_array = np.array(img, dtype=np.uint8)
    # Call C++ function
    selected_colors = image_processor_cpp.process_image_colors(img_array, num_colors)
    # Convert list of tuples to a set (as in original)
    selected_set = set(tuple(c) for c in selected_colors)
    return img_array, selected_set

def process_all_segments(pixels, color_freq_list, total_samples, num_segs, samples_per_column):
    """Wrapper for the C++ parallel segment processor."""
    return image_processor_cpp.process_all_segments(
        pixels, color_freq_list, total_samples, num_segs, samples_per_column
    )