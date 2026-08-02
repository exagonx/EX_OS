#!/bin/bash

git add .
git commit   # apre l’editor
branch=$(git rev-parse --abbrev-ref HEAD)
git push origin "$branch"

