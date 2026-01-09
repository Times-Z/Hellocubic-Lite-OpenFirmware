#!/bin/bash
set -e

if [ ! -d ".githooks" ]; then
  echo "No .githooks directory found. Nothing to install." >&2
  exit 1
fi

git config core.hooksPath .githooks
chmod +x .githooks/*

echo "Git hooks installed"
