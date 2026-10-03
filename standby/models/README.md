# Wake word models for the standby app

| File | Word | Source | License |
|---|---|---|---|
| `hey_jarvis.tflite` | «Hey Jarvis» | [esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models) `models/v2/hey_jarvis.tflite` (Kevin Ahrendt), manifest: cutoff 0.97, window 5, arena 22860 B | Apache-2.0 |

| `heyretro.tflite` (not in Git) | «Hey Retro» | trained on the Mac with microWakeWord, the user's voice and synthetic voices (docs/WAKEWORD.md) | personal, non-commercial (its training data) |

Without `heyretro.tflite`, the build embeds an empty file in its place (`../model_placeholder.py`) and the app
starts with «Hola ESP» and the claps only.

Apache-2.0 text: [../../LICENSES/Apache-2.0.txt](../../LICENSES/Apache-2.0.txt).

`hey_jarvis` was only the stand-in that proves the microWakeWord engine on the board before a «Hey Retro»
model is trained (docs/WAKEWORD.md).
