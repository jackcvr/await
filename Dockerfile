FROM debian:12-slim

RUN apt-get update \
    && apt-get install -y wget valgrind

RUN wget https://musl.cc/x86_64-linux-musl-native.tgz \
    && tar xvfz x86_64-linux-musl-native.tgz

ENV PATH=$PATH:/x86_64-linux-musl-native/bin