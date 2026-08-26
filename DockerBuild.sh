#!/bin/bash
PROJECT_ROOT=$(cd -P -- "$(dirname -- "$0")" && printf '%s\n' "$(pwd -P)")

set -x

# Cap resource usage so the build never exceeds ~85% CPU / 32GB RAM on the
# host. Two layers:
#  - NCORES limits ninja/cmake parallelism (via CMAKE_BUILD_PARALLEL_LEVEL
#    in the Dockerfile), which is what actually keeps peak RAM bounded.
#  - --resource cpu-quota/memory is a cgroup hard ceiling as a safety net.
HOST_CORES=$(nproc)
NCORES=${NCORES:-14}
CPU_PERIOD=100000
CPU_QUOTA=$(( HOST_CORES * CPU_PERIOD * 85 / 100 ))
MEMORY_LIMIT=${MEMORY_LIMIT:-32g}

# Wishlist hint:  For developers, creating a Docker Compose
# setup with persistent volumes for the build & deps directories
# would speed up recompile times significantly.  For end users,
# the simplicity of a single Docker image and a one-time compilation
# seems better.
docker build -t crealityprint \
  --build-arg USER=$USER \
  --build-arg UID=$(id -u) \
  --build-arg GID=$(id -g) \
  --build-arg NCORES=$NCORES \
  --resource memory=$MEMORY_LIMIT \
  --resource cpu-quota=$CPU_QUOTA \
  $PROJECT_ROOT
