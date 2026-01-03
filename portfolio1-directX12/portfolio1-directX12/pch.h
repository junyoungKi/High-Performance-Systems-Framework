// pch.h
#pragma once

extern bool g_UseWarp;

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <wrl.h>

// DirectX 12 headers
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "d3dx12.h" // 프로젝트 내 파일이지만 거의 수정 안 하므로 포함 가능

// STL
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

// 프로젝트 전역 헬퍼
#include "Helpers.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// 매크로 충돌 방지
#if defined(min)
#undef min
#endif
#if defined(max)
#undef max
#endif
#if defined(CreateWindow)
#undef CreateWindow
#endif