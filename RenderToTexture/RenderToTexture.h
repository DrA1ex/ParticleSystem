#pragma once

#include "resource.h"
LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void initVertexData();

struct ParticleCoord;
struct ParticleVel;

void processIfOutOfBounds(ParticleCoord &coord, ParticleVel &vel);