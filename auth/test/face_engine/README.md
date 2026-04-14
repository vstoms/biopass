# Face Engine in Face Pass
## Preparation

Dependencies (ONNX Runtime and openpnp-capture) are automatically fetched via CMake FetchContent. No manual installation required.

## Build
```bash
cmake -S auth -B auth/build -DBUILD_TESTS=ON
cmake --build auth/build --target face_engine_test -j$(nproc)
```

## Run
Compare two images from the `auth` build directory:
```bash
./auth/build/test/face_engine/face_engine_test \
    /path/to/image1 \
    /path/to/image2
```

Optional arguments:
```bash
./auth/build/test/face_engine/face_engine_test \
    --threshold 0.5 \
    --det-model /path/to/yolov8n-face.onnx \
    --reg-model /path/to/edgeface_s_gamma_05.onnx \
    --save-crops --crop1 result1.jpg --crop2 result2.jpg \
    /path/to/image1 \
    /path/to/image2
```

Notes:
- Default models are loaded from `auth/face/models`.
- The CLI detects all faces and uses the largest one in each image for comparison.
- Similarity score and match result are printed to the terminal.
