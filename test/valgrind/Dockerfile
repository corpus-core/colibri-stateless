# Use an Ubuntu base image
FROM ubuntu:latest

# Set environment variable to avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Update package lists and install necessary tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    valgrind \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set the working directory inside the container
WORKDIR /workspace

# Copy your source code into the container
COPY libs /workspace/libs
COPY src /workspace/src
COPY test /workspace/test
COPY CMakeLists.txt /workspace/CMakeLists.txt

# Create a build directory
RUN mkdir build

# Set the working directory to the build directory
WORKDIR /workspace/build

# Run CMake with the specified option and build the project
RUN cmake -DTEST=true -DCURL=OFF .. 
RUN make

# Set the default command to run Valgrind on your application
CMD ["valgrind", "--leak-check=full", "./test/unittests/test_verify_tx"]