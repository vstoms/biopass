#include <getopt.h>

#include <iostream>
#include <memory>
#include <vector>

#include "face_detection.h"
#include "face_recognition.h"
#include "image_utils.h"

using namespace std;

int main(int argc, char **argv) {
  if (argc < 3) {
    cerr << "Usage: " << argv[0] << " <image1.bmp> <image2.bmp>" << endl;
    return 1;
  }

  // Face det model
  FaceDetection model_face_det("./weights/yolov8n-face.onnx");

  // Face reg model
  FaceRecognition model_face_reg("./weights/edgeface_s_gamma_05.onnx");

  // Face1 Inference
  std::vector<Detection> det_images;
  ImageRGB image = image_load_bmp(argv[1]);
  if (image.empty()) {
    cerr << "Failed to load image: " << argv[1] << endl;
    return 1;
  }
  det_images = model_face_det.inference(image);

  if (det_images.empty()) {
    cerr << "No face detected in image 1" << endl;
    return 1;
  }

  ImageRGB face1 = det_images[0].image;
  image_save_bmp("result1.bmp", face1);

  cout << "Face1 saved at result1.bmp\n";
  cout << "\n\n";

  // Face2 Inference
  image = image_load_bmp(argv[2]);
  if (image.empty()) {
    cerr << "Failed to load image: " << argv[2] << endl;
    return 1;
  }
  det_images = model_face_det.inference(image);

  if (det_images.empty()) {
    cerr << "No face detected in image 2" << endl;
    return 1;
  }

  ImageRGB face2 = det_images[0].image;
  image_save_bmp("result2.bmp", face2);

  cout << "Face2 saved at result2.bmp\n";
  cout << "\n\n";

  MatchResult match_result = model_face_reg.match(face1, face2);
  cout << "Face matching result (close to 1 mean look similar, over than 0.5 mean they might "
          "be the same person): "
       << match_result.dist << endl;
  if (match_result.similar)
    cout << "Same person!\n";
  else
    cout << "Different person!\n";
}
