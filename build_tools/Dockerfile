FROM ubuntu:latest

LABEL Description="Build environment for crypto-market-data-collector"

RUN apt-get update
RUN apt-get install -y build-essential cmake clang clang-tidy libboost-all-dev libssl-dev libcurl4-openssl-dev
RUN apt-get install -y git
RUN apt-get install -y gdb