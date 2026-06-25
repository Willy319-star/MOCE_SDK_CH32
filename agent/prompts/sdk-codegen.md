# SDK Code Generation Guardrails

生成代码前先读取当前 SDK 摘要，确认已有组件和 API。

输出工程必须包含：

- `project/<project_name>/CMakeLists.txt`
- `project/<project_name>/main/CMakeLists.txt`
- `project/<project_name>/main/main.c`

代码要求：

- 复用 `service_device`、`driver_led`、`driver_button`、`driver_oled`、`driver_mpu6050`、`driver_tof2000c_vl53l0x`、`driver_servo`、`driver_tw_tts`、`service_wifi` 等已有组件。
- `main.c` 尽量保持清晰；复杂功能拆成独立 `.c/.h`。
- 编译目标是 ESP-IDF 工程。
- 不输出 SDK 内部目录文件。
