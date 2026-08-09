# Source layout

The OLED runtime is split by responsibility so each part can be read and changed independently:

| File | Responsibility |
| --- | --- |
| `main.cpp` | Process entry point only |
| `runtime.cpp` | Command-line handling and the display lifecycle loop |
| `device.hpp` | Low-level I2C OLED operations |
| `status_source.hpp` | D-Bus subscription and status refresh handling |
| `status.cpp` | Decoding Core status payloads |
| `controller.cpp` | OneKVM extension lifecycle action planning |
| `config.cpp` | Device profiles, settings, layouts, and defaults |
| `json.cpp` | Small JSON parser used by runtime inputs |
| `renderer.cpp` | Turning a layout and live status into a frame |
| `canvas.hpp` | Pixel drawing, rotation, text, shapes, and bitmaps |
| `font.hpp` | OLED font loading and glyph measurements |
| `types.hpp` | Shared constants and data structures |

Dependencies point toward the small shared modules: runtime code can use configuration, status, device, and rendering code, while those modules do not depend on the application loop.
