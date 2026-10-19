#pragma once

#include <sokol_gfx.h>

bool saveImagePng(sg_image image, int width, int height, const void* device, const void* context, const char* path);
