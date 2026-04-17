from setuptools import setup, Extension
import pybind11
import numpy as np
import sys

extra_compile_args = ['-O3', '-std=c++17']
if sys.platform == 'darwin':
    extra_compile_args.append('-mmacosx-version-min=10.14')

ext_module = Extension(
    'image_processor_cpp',
    sources=['cpp/image_processor.cpp'],
    include_dirs=[pybind11.get_include(), np.get_include()],
    language='c++',
    extra_compile_args=extra_compile_args,
)

setup(
    name='image_processor_cpp',
    version='0.1',
    ext_modules=[ext_module],
    install_requires=['pybind11', 'numpy'],
)