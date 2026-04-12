import { useEffect, useMemo, useState } from "react";
import { cmd } from "@/commands";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import type { VideoDeviceInfo } from "@/types/config";
import { useConfigurationStore } from "../../-stores/configuration-store";
import { ModelSelect } from "../methods/shared/ModelSelect";
import { Threshold } from "../methods/shared/Threshold";
import { FaceCapture } from "./FaceCapture";

export function FaceSetting() {
  const config = useConfigurationStore((state) => state.config?.methods.face);
  const models = useConfigurationStore((state) => state.config?.models ?? []);
  const setFaceConfig = useConfigurationStore((state) => state.setFaceConfig);
  const [videoDevices, setVideoDevices] = useState<VideoDeviceInfo[]>([]);

  useEffect(() => {
    const fetchDevices = async () => {
      try {
        setVideoDevices(await cmd.face.listVideoDevices());
      } catch (err) {
        console.error("Failed to fetch devices:", err);
      }
    };

    fetchDevices();
  }, []);

  const selectedIrCamera = useMemo(() => {
    if (!config) return null;
    const irCameraPath = `/dev/video${config.ir_camera.device_id}`;
    return videoDevices.find((device) => device.path === irCameraPath) ?? null;
  }, [config, videoDevices]);

  if (!config) return null;

  return (
    <div className="grid gap-4">
      <div className="grid grid-cols-2 gap-6 p-4 rounded-lg bg-muted/50 border border-border/50">
        <div className="grid gap-2">
          <Label
            htmlFor="face-max-retries"
            className="text-sm font-medium text-muted-foreground"
          >
            Max Retries
          </Label>
          <Input
            id="face-max-retries"
            type="number"
            min="0"
            max="10"
            value={config.retries}
            onChange={(e) =>
              setFaceConfig({
                ...config,
                retries: parseInt(e.target.value, 10) || 0,
              })
            }
            className="h-10"
          />
        </div>
        <div className="grid gap-2">
          <Label
            htmlFor="face-retry-delay"
            className="text-sm font-medium text-muted-foreground"
          >
            Retry Delay (ms)
          </Label>
          <Input
            id="face-retry-delay"
            type="number"
            min="0"
            max="5000"
            step="100"
            value={config.retry_delay}
            onChange={(e) =>
              setFaceConfig({
                ...config,
                retry_delay: parseInt(e.target.value, 10) || 0,
              })
            }
            className="h-10"
          />
        </div>
      </div>

      <FaceCapture />

      <div className="p-4 rounded-lg bg-muted/50 border border-border/50">
        <h4 className="font-medium mb-3 text-sm">Detection</h4>
        <div className="flex gap-6 items-end">
          <div className="flex-1 min-w-0">
            <ModelSelect
              label="Model"
              value={config.detection.model}
              models={models.filter((m) => m.type === "detection")}
              error={config.enable}
              onChange={(model) =>
                setFaceConfig({
                  ...config,
                  detection: { ...config.detection, model },
                })
              }
            />
          </div>
          <div className="w-48 shrink-0">
            <Threshold
              label="Threshold"
              value={config.detection.threshold}
              onChange={(threshold) =>
                setFaceConfig({
                  ...config,
                  detection: { ...config.detection, threshold },
                })
              }
            />
          </div>
        </div>
        <h4 className="font-medium my-3 text-sm">Recognition</h4>
        <div className="flex gap-6 items-end">
          <div className="flex-1 min-w-0">
            <ModelSelect
              label="Model"
              value={config.recognition.model}
              models={models.filter((m) => m.type === "recognition")}
              error={config.enable}
              onChange={(model) =>
                setFaceConfig({
                  ...config,
                  recognition: { ...config.recognition, model },
                })
              }
            />
          </div>
          <div className="w-48 shrink-0">
            <Threshold
              label="Threshold"
              value={config.recognition.threshold}
              onChange={(threshold) =>
                setFaceConfig({
                  ...config,
                  recognition: { ...config.recognition, threshold },
                })
              }
            />
          </div>
        </div>
      </div>

      <div className="p-4 rounded-lg bg-muted/50 border border-border/50 space-y-3">
        <div className="flex items-center justify-between">
          <h4 className="font-medium text-sm">Anti-Spoofing</h4>
          <Switch
            checked={config.anti_spoofing.enable}
            onCheckedChange={(enable) =>
              setFaceConfig({
                ...config,
                anti_spoofing: { ...config.anti_spoofing, enable },
              })
            }
            className="cursor-pointer"
          />
        </div>
        {config.anti_spoofing.enable && (
          <div className="space-y-4">
            <div className="flex gap-6 items-end">
              <div className="flex-1 min-w-0">
                <ModelSelect
                  label="Model"
                  value={config.anti_spoofing.model}
                  models={models.filter((m) => m.type === "anti-spoofing")}
                  error={config.anti_spoofing.enable}
                  onChange={(model) =>
                    setFaceConfig({
                      ...config,
                      anti_spoofing: { ...config.anti_spoofing, model },
                    })
                  }
                />
              </div>
              <div className="w-48 shrink-0">
                <Threshold
                  label="Threshold"
                  value={config.anti_spoofing.threshold}
                  onChange={(threshold) =>
                    setFaceConfig({
                      ...config,
                      anti_spoofing: { ...config.anti_spoofing, threshold },
                    })
                  }
                />
              </div>
            </div>

            <div className="grid gap-2">
              <h5 className="font-medium text-sm">IR Camera</h5>
              <Label
                htmlFor="ir-device"
                className="text-xs text-muted-foreground"
              >
                Select Device
              </Label>
              <Select
                value={
                  config.ir_camera.enable ? (selectedIrCamera?.path ?? "") : ""
                }
                onValueChange={(value) =>
                  setFaceConfig({
                    ...config,
                    ir_camera: {
                      ...config.ir_camera,
                      enable: true,
                      device_id:
                        Number.parseInt(value.replace("/dev/video", ""), 10) ||
                        0,
                    },
                  })
                }
              >
                <SelectTrigger id="ir-device" className="h-10 w-full">
                  <SelectValue placeholder="Select IR Camera Device" />
                </SelectTrigger>
                <SelectContent>
                  {videoDevices.length > 0 ? (
                    videoDevices.map((device) => (
                      <SelectItem key={device.path} value={device.path}>
                        {device.display_name}
                      </SelectItem>
                    ))
                  ) : (
                    <SelectItem value="none" disabled>
                      No video devices found
                    </SelectItem>
                  )}
                </SelectContent>
              </Select>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
