# RETROTV Server in a container, for a NAS or any always-on Linux box (server/README.md, "NAS").
# Built from the repository root, because the demo clip comes from tools/:
#
#   docker build -f server/Dockerfile -t retrotv-server .
#
# The image carries the server, FFmpeg and the synthetic demo clip; the channel list is mounted
# from outside (config/channels.json). Home network only: never publish the port to the Internet.
FROM python:3.12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY server/requirements.txt server/requirements.txt
RUN pip install --no-cache-dir -r server/requirements.txt

COPY tools/make_demo_clip.sh tools/make_index.py tools/
COPY server/app server/app
COPY server/config/channels.example.json server/config/channels.example.json

# The demo channel's clip (no third-party content), made once at build time.
RUN scratch="$(mktemp -d)" \
    && bash tools/make_demo_clip.sh "$scratch" 30 >/dev/null \
    && mkdir -p server/media/demo \
    && mv "$scratch"/retrotv/media/demo/demo.mjpeg "$scratch"/retrotv/media/demo/demo.aac server/media/demo/ \
    && python tools/make_index.py server/media/demo/demo.mjpeg \
    && rm -rf "$scratch" \
    && cp server/config/channels.example.json server/config/channels.json

RUN useradd --system --no-create-home pautv
USER pautv
WORKDIR /app/server
ENV PAUTV_ANNOUNCE=1 PAUTV_PORT=8080
EXPOSE 8080
# Streams never end on their own: on stop, give them 1 s and cut them.
CMD ["sh", "-c", "exec uvicorn app.main:create_app --factory --host 0.0.0.0 --port \"$PAUTV_PORT\" --timeout-graceful-shutdown 1"]
