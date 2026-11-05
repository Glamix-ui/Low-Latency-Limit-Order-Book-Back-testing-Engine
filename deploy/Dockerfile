FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target lob_engine
RUN pip install --no-cache-dir -r server/requirements.txt
ENV PORT=10000
# One worker + threads: the order book lives in this process's memory.
CMD gunicorn --chdir server -w 1 --threads 16 -b 0.0.0.0:$PORT app:app
