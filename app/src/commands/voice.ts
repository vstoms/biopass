import { invokeCommand } from "./core";

function listRecordings() {
  return invokeCommand<string[]>("list_voice_recordings");
}

function saveRecording(audioData: string) {
  return invokeCommand<void>("save_voice_recording", { audioData });
}

function deleteRecording(path: string) {
  return invokeCommand<void>("delete_voice_recording", { path });
}

export const voice = {
  listRecordings,
  saveRecording,
  deleteRecording,
};
