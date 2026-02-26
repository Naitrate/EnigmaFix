#!/usr/bin/env bash
# Check the distribution ID in /etc/*release
DISTRO_NAME=$(cat /etc/*release | grep ^NAME= | cut -d '=' -f 2 | tr -d '"')

# Check if the system is NixOS by looking for "NixOS"
if [ "$DISTRO_NAME" == "NixOS" ]; then
  echo "This system is NixOS. Cleaning through..."
  nix-store --gc
  nix-store --delete $(nix-store --query --requisites ./result)
  rm -rf Intermediate
else
  echo "This system is not NixOS. Cleaning Natively..."
  rm -rf Intermediate
fi