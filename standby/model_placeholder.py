# The «Hey Retro» model stays out of Git (models/README.md). Without it, an empty file is embedded in its place
# and the app starts with «Hola ESP» and the claps only (main.cpp initMicroWakeWord).
import os

Import("env")

model = os.path.join(env.subst("$PROJECT_DIR"), "models", "heyretro.tflite")
if not os.path.isfile(model):
    open(model, "wb").close()
    print("Warning! models/heyretro.tflite missing: building without «Hey Retro» (empty placeholder)")
