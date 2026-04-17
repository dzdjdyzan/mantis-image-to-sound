## MANTIS IMAGE TO SOUND GENERATOR

## Description

Roughly, this algorithm scans any image by column left to right and generates sequential ticks of audio data which are then saved to a wav file with the same base name as the image file selected. The colour distribution of the image affects which audio frequencies will most likely be heard, likewise cauchy randomness is introduced to ensure that subsequent generations of audio of the same image will result in slightly different audio each time.

This is a quick mock up of an algorithm to be used for the Mantis audio synthesizer. The final version of Mantis will use this algorithm to initially generate a short audio file whose data is generated from the rgb pixel data of a user selected image. The user will then be able to further scan pixel data which will trigger grains of sound to be played from the initially generate audio file.

WARNING: The resulting generated audio can sometimes be harsh particularily in the higher frequency spectrum, it is reccommended for now to first play the audio at a low enough volume to avoid discomfort. Increase the volume slowly afterwards to your comfort and enjoy the various sound worlds.

## Requirements:

- Python 3.8+
- A C++ compiler
- macOS Monterey 12.7.5 or above/Windows 10 or above

## Setup:

1. **Clone the repository**  
   `git clone ... && cd mantis-image-to-sound-main`

2. **Create a virtual environment**  
   `python -m venv venv` and activate it.

3. **Install Python dependencies**  
   `pip install -r requirements.txt`

4. **Build the C++ extension**  
   `pip install .`

5. **Generate the frequency database** (one time)
	`cd db`  
    `python generate_rgb_to_freq_db.py`  
   This will create the `rgb_to_frequency.db` 
   inside the `db` folder where it needs to be.

6. **Run the program**  
   `cd python`  
   `python main.py`  
   This will open up a file explorer for image selection.
   Alternatively,
   `python main.py full_path_to_img.jpg`
   can be used for a direct call.

The output `.wav` file will appear in the `python/` folder.

Note: if you're on macos use `python3` instead of `python` command of course.


## Performance

- The pixel‑processing loops run in **C++** with a thread pool (parallel over segments).
- The FM synthesis and final mixing use **NumPy** vectorisation.

## Example

[Example image](example.jpg) → [example.wav](example.wav)

## License

[MIT](LICENSE)


