#include "thread_pool.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <random>
#include <algorithm>
#include <ctime>
#include <future>
#include <array>

namespace py = pybind11;

// ----------------------------------------------------------------------
// RGB packing/unpacking
static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8)  |
           static_cast<uint32_t>(b);
}

static inline std::tuple<uint8_t, uint8_t, uint8_t> unpack_rgb(uint32_t key) {
    return {static_cast<uint8_t>(key >> 16),
            static_cast<uint8_t>(key >> 8),
            static_cast<uint8_t>(key)};
}

// ----------------------------------------------------------------------
// Statistics
static float mean(const std::vector<float>& v) {
    double sum = 0.0;
    for (float x : v) sum += x;
    return static_cast<float>(sum / v.size());
}

static float stddev(const std::vector<float>& v, float mean_val) {
    double sq_sum = 0.0;
    for (float x : v) {
        double d = x - mean_val;
        sq_sum += d * d;
    }
    return static_cast<float>(std::sqrt(sq_sum / v.size()));
}

static float median(std::vector<float> v) {
    if (v.empty()) return 0.0f;
    size_t n = v.size();
    size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    if (n % 2 == 1) return v[mid];
    std::nth_element(v.begin(), v.begin() + mid - 1, v.begin() + mid);
    return (v[mid - 1] + v[mid]) * 0.5f;
}

// ----------------------------------------------------------------------
// Cauchy random number
static std::mt19937 rng(std::time(nullptr));
static std::uniform_real_distribution<float> uniform(0.0f, 1.0f);

static float cauchy_random(float median, float scale) {
    float u = uniform(rng) - 0.5f;
    return median + scale * std::tan(static_cast<float>(M_PI) * u);
}

// ----------------------------------------------------------------------
// RGB → HSV
static void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v) {
    float max = std::max({r, g, b});
    float min = std::min({r, g, b});
    float delta = max - min;
    v = max;
    s = (max == 0.0f) ? 0.0f : delta / max;
    if (delta == 0.0f) {
        h = 0.0f;
        return;
    }
    if (max == r)
        h = 60.0f * std::fmod((g - b) / delta, 6.0f);
    else if (max == g)
        h = 60.0f * ((b - r) / delta + 2.0f);
    else
        h = 60.0f * ((r - g) / delta + 4.0f);
    if (h < 0) h += 360.0f;
    h /= 360.0f;
}

// ----------------------------------------------------------------------
// Custom amplitude for a column of RGB pixels
static float calculate_custom_amplitude(const std::vector<std::array<uint8_t,3>>& column) {
    if (column.empty()) return 0.0f;
    int rows = static_cast<int>(column.size());
    float total_amp = 0.0f;
    for (const auto& pix : column) {
        float r = pix[0] / 255.0f;
        float g = pix[1] / 255.0f;
        float b = pix[2] / 255.0f;

        float rgb_intensity = std::sqrt(r*r + g*g + b*b) / std::sqrt(3.0f);

        float h, s, v;
        rgb_to_hsv(r, g, b, h, s, v);

        float non_linear = (r*r + g*g + b*b) *
                           std::sin(h * 2.0f * static_cast<float>(M_PI)) *
                           std::cos(s * static_cast<float>(M_PI));

        float color_amp = 0.1f * (rgb_intensity * non_linear) +
                          0.2f * r + 0.1f * g + 0.2f * b +
                          0.05f * std::sin(h * 2.0f * static_cast<float>(M_PI)) +
                          0.3f * s * s -
                          0.05f * std::fabs(0.5f - v);

        total_amp += color_amp;
    }
    return total_amp / static_cast<float>(rows);
}

// ----------------------------------------------------------------------
// Cubic spline interpolation (quadratic through three points)
static std::vector<float> cubic_spline_interpolate(float current, float target, int n_points) {
    std::vector<float> out(n_points);
    if (n_points <= 0) return out;
    if (n_points == 1) {
        out[0] = current;
        return out;
    }
    int mid = n_points / 2;
    float y_mid = (current + target) * 0.5f;
    float x0 = 0.0f, x1 = static_cast<float>(mid), x2 = static_cast<float>(n_points - 1);
    float y0 = current, y1 = y_mid, y2 = target;

    float x1_sq = x1 * x1, x2_sq = x2 * x2;
    float det = x1_sq * x2 - x2_sq * x1;
    if (std::fabs(det) < 1e-6f) {
        // fallback linear
        for (int i = 0; i < n_points; ++i) {
            float t = static_cast<float>(i) / (n_points - 1);
            out[i] = current * (1.0f - t) + target * t;
        }
        return out;
    }
    float a = ((y1 - y0) * x2 - (y2 - y0) * x1) / det;
    float b = ((y2 - y0) * x1_sq - (y1 - y0) * x2_sq) / det;
    for (int i = 0; i < n_points; ++i) {
        float x = static_cast<float>(i);
        out[i] = a * x * x + b * x + y0;
    }
    return out;
}

// ----------------------------------------------------------------------
// Process one segment (returns audio buffer)
static std::vector<float> process_one_segment(
    int segment,
    const uint8_t* pixels,
    int height, int width,
    const std::vector<float>& angular_freqs,
    int total_samples,
    int num_segs,
    int samples_per_column)
{
    int segment_height = height / num_segs;
    int start_y = segment * segment_height;
    int end_y = (segment == num_segs - 1) ? height : (segment + 1) * segment_height;
    int col_height = end_y - start_y;

    std::vector<float> audio(total_samples, 0.0f);
    float current_amplitude = 0.0f;
    float angular_frequency = angular_freqs[segment];
    float phase_shift = std::fmod(segment * 271.0f + angular_frequency * 26.0f,
                                  2.0f * static_cast<float>(M_PI));

    for (int x = 0; x < width; ++x) {
        std::vector<std::array<uint8_t,3>> column;
        column.reserve(col_height);
        for (int y = start_y; y < end_y; ++y) {
            int idx = (y * width + x) * 3;
            column.push_back({pixels[idx], pixels[idx+1], pixels[idx+2]});
        }

        float amplitude = calculate_custom_amplitude(column);
        amplitude = amplitude * amplitude;
        float target_amplitude = amplitude;

        std::vector<float> interp = cubic_spline_interpolate(current_amplitude, target_amplitude, samples_per_column);

        int sample_idx = x * samples_per_column;
        for (int i = 0; i < samples_per_column; ++i) {
            int sample = sample_idx + i;
            if (sample < total_samples) {
                audio[sample] += interp[i] * std::sin(angular_frequency * sample + phase_shift);
            }
        }
        current_amplitude = target_amplitude;
    }
    return audio;
}

// ----------------------------------------------------------------------
// Process all segments in parallel using a thread pool
static py::list process_all_segments(
    py::array_t<uint8_t> pixels,
    py::list color_freq_list,     // each element: (color_tuple, frequency, angular_frequency)
    int total_samples,
    int num_segs,
    int samples_per_column)
{
    // Get image dimensions and raw pointer
    auto buf = pixels.request();
    if (buf.ndim != 3 || buf.shape[2] != 3) {
        throw std::runtime_error("Expected 3D array of shape (height, width, 3)");
    }
    int height = static_cast<int>(buf.shape[0]);
    int width  = static_cast<int>(buf.shape[1]);
    uint8_t* ptr = static_cast<uint8_t*>(buf.ptr);

    // Extract angular frequencies for each segment
    std::vector<float> angular_freqs(num_segs);
    for (int i = 0; i < num_segs; ++i) {
        py::tuple tup = color_freq_list[i].cast<py::tuple>();
        angular_freqs[i] = tup[2].cast<float>();   // third element
    }

    // Create thread pool
    ThreadPool pool(std::thread::hardware_concurrency());
    std::vector<std::future<std::vector<float>>> futures;

    // Enqueue all segment tasks
    for (int seg = 0; seg < num_segs; ++seg) {
        futures.emplace_back(pool.enqueue([seg, ptr, height, width, angular_freqs,
                                           total_samples, num_segs, samples_per_column] {
            return process_one_segment(seg, ptr, height, width, angular_freqs,
                                       total_samples, num_segs, samples_per_column);
        }));
    }

    // Collect results as NumPy arrays
    py::list results;
    for (int seg = 0; seg < num_segs; ++seg) {
        std::vector<float> audio = futures[seg].get();
        py::array_t<float> arr(audio.size());
        auto arr_buf = arr.request();
        float* arr_ptr = static_cast<float*>(arr_buf.ptr);
        std::copy(audio.begin(), audio.end(), arr_ptr);
        results.append(arr);
    }
    return results;
}

// ----------------------------------------------------------------------
// Process image and return selected colours (original function)
py::list process_image_colors(py::array_t<uint8_t> image, int num_colors) {
    auto buf = image.request();
    if (buf.ndim != 3 || buf.shape[2] != 3) {
        throw std::runtime_error("Expected 3D array of shape (height, width, 3)");
    }
    int height = static_cast<int>(buf.shape[0]);
    int width  = static_cast<int>(buf.shape[1]);
    uint8_t* ptr = static_cast<uint8_t*>(buf.ptr);

    std::unordered_map<uint32_t, int> freq_map;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 3;
            uint32_t key = pack_rgb(ptr[idx], ptr[idx+1], ptr[idx+2]);
            freq_map[key]++;
        }
    }

    std::vector<uint32_t> unique_keys;
    std::vector<float> frequencies;
    unique_keys.reserve(freq_map.size());
    frequencies.reserve(freq_map.size());
    for (const auto& pair : freq_map) {
        unique_keys.push_back(pair.first);
        frequencies.push_back(static_cast<float>(pair.second));
    }
    int num_unique = static_cast<int>(unique_keys.size());

    if (num_unique <= num_colors) {
        py::list result;
        for (uint32_t key : unique_keys) {
            auto [r,g,b] = unpack_rgb(key);
            result.append(py::make_tuple(r, g, b));
        }
        return result;
    }

    float mean_freq = mean(frequencies);
    float stddev_freq = stddev(frequencies, mean_freq);
    float med = median(frequencies);
    float scale = 0.5f * stddev_freq;

    std::unordered_set<uint32_t> selected_keys;
    int attempts = 0;
    const int max_attempts = 1000;
    while (selected_keys.size() < static_cast<size_t>(num_colors) && attempts < max_attempts) {
        float cauchy_val = cauchy_random(med, scale);
        float norm_val = (cauchy_val - med) / (stddev_freq * 6.0f) + 0.5f;
        norm_val = std::max(0.0f, std::min(norm_val, 1.0f));
        int idx = static_cast<int>(norm_val * (num_unique - 1));
        selected_keys.insert(unique_keys[idx]);
        attempts++;
    }

    py::list result;
    for (uint32_t key : selected_keys) {
        auto [r,g,b] = unpack_rgb(key);
        result.append(py::make_tuple(r, g, b));
    }
    return result;
}

// ----------------------------------------------------------------------
// Module definition
PYBIND11_MODULE(image_processor_cpp, m) {
    m.doc() = "Fast image colour processing and audio synthesis module";
    m.def("process_image_colors", &process_image_colors, "Process image and return selected colours",
          py::arg("image"), py::arg("num_colors"));
    m.def("process_all_segments", &process_all_segments, "Process all image segments in parallel",
          py::arg("pixels"), py::arg("color_freq_list"), py::arg("total_samples"),
          py::arg("num_segs"), py::arg("samples_per_column"));
}