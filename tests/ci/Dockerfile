FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates git g++ ninja-build pkg-config python3 python3-venv nodejs npm \
    libboost-all-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
    libgrpc++-dev libjsoncpp-dev libhiredis-dev libmysqlcppconn-dev libssl-dev \
    qt6-base-dev qt6-svg-dev qt6-tools-dev-tools && \
    python3 -m venv /opt/test-env
ENV PATH="/opt/test-env/bin:${PATH}" QT_QPA_PLATFORM=offscreen
COPY tests/requirements.lock /tmp/requirements.lock
RUN pip install --no-cache-dir cmake==3.31.6 -r /tmp/requirements.lock
WORKDIR /workspace
