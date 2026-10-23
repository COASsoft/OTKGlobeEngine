#pragma once

#include <sokol_gfx.h>

#include <vector>

bool readImageRgba(sg_image image, int width, int height, const void* device, const void* context, std::vector<unsigned char>& rgba);
bool saveImagePng(sg_image image, int width, int height, const void* device, const void* context, const char* path);
bool writePng(const char* path, int width, int height, const std::vector<unsigned char>& rgba);
bool writeBmp(const char* path, int width, int height, const std::vector<unsigned char>& rgba);
