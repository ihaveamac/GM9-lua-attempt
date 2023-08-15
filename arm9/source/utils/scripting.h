#pragma once

#include "common.h"

#define SCRIPT_EXT      "gm9"
#define SCRIPT_MAX_SIZE STD_BUFFER_SIZE

bool ValidateText(const char* text, u32 size);
bool MemTextViewer(const char* text, u32 len, u32 start, bool as_script);
bool MemToCViewer(const char* text, u32 len, const char* title);
bool FileTextViewer(const char* path, bool as_script);
