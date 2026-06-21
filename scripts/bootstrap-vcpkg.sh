#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vcpkg_dir="/opt/vcpkg"

if [ ! -d "${vcpkg_dir}/.git" ]; then
  if [ ! -w "$(dirname "${vcpkg_dir}")" ]; then
    echo "Creating ${vcpkg_dir} requires sudo."
    sudo mkdir -p "${vcpkg_dir}"
    sudo chown -R "$(id -u):$(id -g)" "${vcpkg_dir}"
  else
    mkdir -p "${vcpkg_dir}"
  fi
  git clone https://github.com/microsoft/vcpkg.git "${vcpkg_dir}"
fi

"${vcpkg_dir}/bootstrap-vcpkg.sh"

echo "vcpkg is ready at ${vcpkg_dir}"
echo "Configure with: mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build . -- -j8"
