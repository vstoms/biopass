import type { VideoDeviceInfo } from "@/types/config";
import { invokeCommand } from "./core";

function listImages() {
  return invokeCommand<string[]>("list_faces");
}

function saveImage(data: string) {
  return invokeCommand<string>("capture_face", { data });
}

function deleteImage(path: string) {
  return invokeCommand<void>("delete_face", { path });
}

function listVideoDevices() {
  return invokeCommand<VideoDeviceInfo[]>("list_video_devices");
}

export const face = {
  listImages,
  saveImage,
  deleteImage,
  listVideoDevices,
};
