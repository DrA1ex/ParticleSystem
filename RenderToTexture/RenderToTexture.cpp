// RenderToTexture.cpp: определяет точку входа для приложения.
//
#include "stdafx.h"
#include "RenderToTexture.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <shellapi.h>

#define SHIPPING_VERSION 

#define RELEASE(x) if(x) {x->Release(); x = nullptr; }

LPDIRECT3D9 d3d = NULL;
LPDIRECT3DDEVICE9 device = NULL;
IDirect3DTexture9* renderTexture = NULL, *particleTexture = NULL,
*overlayTexture = NULL;

IDirect3DSurface9* orig = NULL
, *renderTarget = NULL;

LPDIRECT3DVERTEXBUFFER9 pVertexObject = NULL;
LPDIRECT3DVERTEXDECLARATION9 vertexDecl = NULL;

LPDIRECT3DVERTEXBUFFER9 pRectObject = NULL;
LPDIRECT3DVERTEXDECLARATION9 RectDecl = NULL;

ID3DXEffect* effect = NULL;

HWND hMainWnd = NULL;
UINT_PTR timer = NULL;

//Constant

const float defG = 16.6738480f; // Гравитационная постоянная умноженная на массу "курсора"
const float defResistance = 0.9975f; // Сопротивление среде
//////////

float G = defG;
float Resistance = defResistance;

int Width = 0;
int Height = 0;

bool ManualControl = false;
int animateSlowDown = 5;
int animationType = 1;
int mathModel = 1;

#ifdef _DEBUG
int partCount = 50000;
#else
int partCount = 200000;
#endif

int updateDataDelay = 8;

float pointSize = 3;

struct VertexData
{
	float x, y, z;
	float u, v;
};

struct ParticleCoord
{
	float x, y;
};

struct ParticleVel
{
	float vx, vy;
};

ParticleCoord *particlesCoord;
ParticleVel *particlesVel;

float step = 0.0;
double r;
int tmpCounter = 0;
void UpdatePosition(float &mx, float &my)
{
	if(step > 360)
	{
		step = 0;
	}

	auto angle = step / (M_PI * 2);

	if(animationType == 0)
	{
		mx = r*cos(angle) + Width / 2;
		my = r*sin(angle) + Height / 2;
	}
	else if(animationType == 1)
	{
		mx = sin(2 * angle)*r + (Width / 2);
		my = cos(3 * angle)*r + (Height / 2);
	}
	else if(animationType == 2)
	{
		mx = sin(2 * angle)*r + (Width / 2);
		my = sin(2 * angle)*r + (Height / 2);
	}
	else
	{
		mx = (step - 180)*(r / 360) + Width / 2;
		my = (step - 180)*(r / 360) + Height / 2;
	}

	if(++tmpCounter % animateSlowDown == 0)
		step += 0.5;
}

void animate()
{
	float mx;
	float my;
	if(ManualControl)
	{
		POINT pos;
		GetCursorPos(&pos);
		RECT rc;
		GetClientRect(hMainWnd, &rc);
		ScreenToClient(hMainWnd, &pos);

		mx = pos.x;
		my = pos.y;
	}
	else
	{
		UpdatePosition(mx, my);
	}


	const auto size = partCount;

	VertexData *pVertexBuffer;
	pVertexObject->Lock(0, 0, (void**)&pVertexBuffer, D3DLOCK_DISCARD);

	_mm256_zeroall();

#pragma omp parallel \
	shared(pVertexBuffer, particlesCoord, particlesVel, mx, my, size)
	{
#pragma omp for nowait
		for(int i = 0; i < size; i += 4)
		{
			float mouseCoordVec[8] = { mx, my, mx, my, mx, my, mx, my };

			float *particleCoordsVec = (float*)particlesCoord + i;
			float *velocityVec = (float*)particlesVel + i;

			auto xyCoord = _mm256_loadu_ps(particleCoordsVec);
			auto hwTempData = _mm256_sub_ps(xyCoord, _mm256_loadu_ps(mouseCoordVec));

			auto squares = _mm256_mul_ps(hwTempData, hwTempData);
			auto distSquare = _mm256_hadd_ps(squares, squares);
			distSquare = _mm256_shuffle_ps(distSquare, distSquare, 0x50);

			auto theForce = _mm256_div_ps(_mm256_set1_ps(G), distSquare);

			if(distSquare.m256_f32[0] < 400)
			{
				theForce.m256_f32[0] = 0;
				theForce.m256_f32[1] = 0;
			}

			if(distSquare.m256_f32[2] < 400)
			{
				theForce.m256_f32[2] = 0;
				theForce.m256_f32[3] = 0;
			}
			if(distSquare.m256_f32[4] < 400)
			{
				theForce.m256_f32[4] = 0;
				theForce.m256_f32[5] = 0;
			}

			if(distSquare.m256_f32[6] < 400)
			{
				theForce.m256_f32[6] = 0;
				theForce.m256_f32[7] = 0;
			}

			auto xyForces = _mm256_mul_ps(_mm256_xor_ps(hwTempData, _mm256_set1_ps(-0.f)), theForce);

			auto xyVelocities = _mm256_loadu_ps(velocityVec);
			xyVelocities = _mm256_mul_ps(xyVelocities, _mm256_set1_ps(Resistance));
			xyVelocities = _mm256_add_ps(xyVelocities, xyForces);

			xyCoord = _mm256_add_ps(xyCoord, xyVelocities);

			_mm256_storeu_ps(velocityVec, xyVelocities);
			_mm256_storeu_ps(particleCoordsVec, xyCoord);


			processIfOutOfBounds(((ParticleCoord*)particleCoordsVec)[0], ((ParticleVel*)velocityVec)[0]);
			processIfOutOfBounds(((ParticleCoord*)particleCoordsVec)[1], ((ParticleVel*)velocityVec)[1]);
			processIfOutOfBounds(((ParticleCoord*)particleCoordsVec)[2], ((ParticleVel*)velocityVec)[2]);
			processIfOutOfBounds(((ParticleCoord*)particleCoordsVec)[3], ((ParticleVel*)velocityVec)[3]);

			pVertexBuffer[i].x = ((ParticleCoord*)particleCoordsVec)[0].x;
			pVertexBuffer[i].y = ((ParticleCoord*)particleCoordsVec)[0].y;
			pVertexBuffer[i + 1].x = ((ParticleCoord*)particleCoordsVec)[1].x;
			pVertexBuffer[i + 1].y = ((ParticleCoord*)particleCoordsVec)[1].y;
			pVertexBuffer[i + 2].x = ((ParticleCoord*)particleCoordsVec)[2].x;
			pVertexBuffer[i + 2].y = ((ParticleCoord*)particleCoordsVec)[2].y;
			pVertexBuffer[i + 3].x = ((ParticleCoord*)particleCoordsVec)[3].x;
			pVertexBuffer[i + 3].y = ((ParticleCoord*)particleCoordsVec)[3].y;
		}
	}
	pVertexObject->Unlock();

	_mm256_zeroall();
}

void processIfOutOfBounds(ParticleCoord &coord, ParticleVel &vel)
{
	bool reflectedX = false, reflectedY = false;

	auto &x = coord.x;
	auto &y = coord.y;

	if(x > Width)
	{
		reflectedX = true;
		x = Width - (x - Width);
	}
	else if(x < 0)
	{
		reflectedX = true;
		x = -x;
	}

	if(y > Height)
	{
		reflectedY = true;
		y = Height - (y - Height);
	}
	else if(y < 0)
	{
		reflectedY = true;
		y = -y;
	}

	if(reflectedX)
	{
		vel.vx = -vel.vx * 0.5;
	}
	if(reflectedY)
	{
		vel.vy = -vel.vy * 0.5;
	}

}

void initParticles()
{
	srand(clock());

	particlesCoord = new ParticleCoord[partCount];
	particlesVel = new ParticleVel[partCount];
	for(int i = 0; i < partCount; ++i)
	{
		particlesCoord[i].x = rand() % Width;
		particlesCoord[i].y = rand() % Height;
		particlesVel[i].vx = rand() % 2;
		particlesVel[i].vy = rand() % 2;
	}
}

void initD3D()
{
	d3d = Direct3DCreate9(D3D_SDK_VERSION);
	D3DPRESENT_PARAMETERS PresentParams;
	memset(&PresentParams, 0, sizeof(D3DPRESENT_PARAMETERS));

	PresentParams.Windowed = TRUE;
	PresentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;

	///////////////////////////////////////////////////////////////////////

	// Set default settings
	UINT AdapterToUse = D3DADAPTER_DEFAULT;
	D3DDEVTYPE DeviceType = D3DDEVTYPE_HAL;
#ifdef SHIPPING_VERSION
	if(FAILED(d3d->CreateDevice(AdapterToUse, DeviceType, hMainWnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		&PresentParams, &device)))
	{
		MessageBoxA(hMainWnd, "Unable to initialize PerfectHUD", 0, MB_ICONHAND);
		terminate();
	}
#else
	// Look for 'NVIDIA PerfHUD' adapter
	// If it is present, override default settings
	for (UINT Adapter = 0; Adapter < d3d->GetAdapterCount(); Adapter++)
	{
		D3DADAPTER_IDENTIFIER9  Identifier;
		HRESULT  Res;
		Res = d3d->GetAdapterIdentifier(Adapter, 0, &Identifier);
		if (strstr(Identifier.Description, "PerfHUD") != 0)
		{
			AdapterToUse = Adapter;
			DeviceType = D3DDEVTYPE_REF;
			break;
		}
	}
#endif

	//////////////////////////////////////////////////////////////////////

	device->SetRenderState(D3DRS_POINTSIZE_MAX, *((DWORD*)&pointSize));
	device->SetRenderState(D3DRS_POINTSIZE, *((DWORD*)&pointSize));
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	device->SetRenderState(D3DRS_ZENABLE, FALSE);

	D3DXCreateTextureFromFile(device, L"particle.png", &particleTexture);
	D3DXCreateTextureFromFile(device, L"overlay.png", &overlayTexture);

	D3DXCreateTexture(device, Width, Height, 0, D3DUSAGE_RENDERTARGET, D3DFMT_X8B8G8R8, D3DPOOL_DEFAULT, &renderTexture);
	renderTexture->GetSurfaceLevel(0, &renderTarget);
	device->GetRenderTarget(0, &orig);

	//Инициализация матриц
	D3DXMATRIX matrixView;
	D3DXMATRIX matrixProjection;

	D3DXMatrixLookAtLH(
		&matrixView,
		&D3DXVECTOR3(0, 0, 0),
		&D3DXVECTOR3(0, 0, 1),
		&D3DXVECTOR3(0, 1, 0));

	D3DXMatrixOrthoOffCenterLH(&matrixProjection, 0, Width, Height, 0, 0, 255);

	device->SetTransform(D3DTS_VIEW, &matrixView);
	device->SetTransform(D3DTS_PROJECTION, &matrixProjection);

	device->SetTexture(0, particleTexture);
}
void initEffect()
{
	ID3DXBuffer* errorBuffer = 0;
	D3DXCreateEffectFromFile(
		device,
		L"effect.fx",
		NULL, // CONST D3DXMACRO* pDefines,
		NULL, // LPD3DXINCLUDE pInclude,
		D3DXSHADER_USE_LEGACY_D3DX9_31_DLL,
		NULL, // LPD3DXEFFECTPOOL pPool,
		&effect,
		&errorBuffer);

	if(errorBuffer)
	{
		MessageBoxA(hMainWnd, (char*)errorBuffer->GetBufferPointer(), 0, 0);
		errorBuffer->Release();
		terminate();
	}

	D3DXMATRIX W, V, P, Result;
	D3DXMatrixIdentity(&Result);
	device->GetTransform(D3DTS_WORLD, &W);
	device->GetTransform(D3DTS_VIEW, &V);
	device->GetTransform(D3DTS_PROJECTION, &P);
	D3DXMatrixMultiply(&Result, &W, &V);
	D3DXMatrixMultiply(&Result, &Result, &P);

	effect->SetMatrix(effect->GetParameterByName(0, "WorldViewProj"), &Result);

	effect->SetTechnique(effect->GetTechnique(0));

	auto hr = effect->SetTexture(effect->GetParameterByName(NULL, "Overlay"), overlayTexture);
	hr |= effect->SetTexture(effect->GetParameterByName(NULL, "Base"), particleTexture);
	hr |= effect->SetTexture(effect->GetParameterByName(NULL, "PreRender"), renderTexture);

	if(hr != 0)
	{
		MessageBox(hMainWnd, L"Unable to set effect textures.", L"", MB_ICONHAND);
	}
}
void initVertexData()
{
	size_t count = partCount;
	VertexData *vertexData = new VertexData[count];

	for(size_t i = 0; i < count; ++i)
	{
		vertexData[i].x = particlesCoord[i].x;
		vertexData[i].y = particlesCoord[i].y;
		vertexData[i].z = 0.f;
		vertexData[i].u = 0;
		vertexData[i].v = 0;
	}

	void *pRectBuffer = NULL;
	device->CreateVertexBuffer(count*sizeof(VertexData), D3DUSAGE_WRITEONLY,
		D3DFVF_XYZ | D3DFVF_TEX0, D3DPOOL_DEFAULT, &pVertexObject, NULL);

	pVertexObject->Lock(0, count*sizeof(VertexData), &pRectBuffer, 0);

	memcpy(pRectBuffer, vertexData, count*sizeof(VertexData));
	pVertexObject->Unlock();

	delete[] vertexData;
	vertexData = nullptr;

	D3DVERTEXELEMENT9 decl[] =
	{
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
		D3DDECL_END()
	};

	device->CreateVertexDeclaration(decl, &vertexDecl);
}
void initRectangleVertexes()
{
	VertexData data[4] = {
		/*   x          y           z       u   v   */
		{ 0, 0, 0, 0, 0 },
		{ Width, 0, 0, 1, 0 },
		{ 0, Height, 0, 0, 1 },
		{ Width, Height, 0, 1, 1 }
	};

	void *pRectBuffer = NULL;
	device->CreateVertexBuffer(4 * sizeof(VertexData), D3DUSAGE_WRITEONLY,
		D3DFVF_XYZ | D3DFVF_TEX0, D3DPOOL_DEFAULT, &pRectObject, NULL);

	pRectObject->Lock(0, 4 * sizeof(VertexData), &pRectBuffer, 0);

	memcpy(pRectBuffer, data, 4 * sizeof(VertexData));
	pRectObject->Unlock();

	D3DVERTEXELEMENT9 decl[] =
	{
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
		D3DDECL_END()
	};

	device->CreateVertexDeclaration(decl, &RectDecl);
}

void Init()
{
	initD3D();
	initParticles();
	initVertexData();
	initRectangleVertexes();
	initEffect();
}

void CleanUp()
{
	RELEASE(pVertexObject)
		RELEASE(vertexDecl)
		RELEASE(pRectObject)
		RELEASE(RectDecl)
		RELEASE(effect)
		RELEASE(renderTarget)

		RELEASE(overlayTexture)
		RELEASE(particleTexture)
		RELEASE(renderTexture)

		RELEASE(device)
		RELEASE(d3d)

		delete[] particlesVel;
	delete[] particlesCoord;
}

void initVariables()
{
	int args = 0;
	auto data = CommandLineToArgvW(GetCommandLineW(), &args);

	for(int i = 1; i < args; ++i)
	{
		if(data[i][0] == '-') //We recive a parameter
		{
			if(i < args - 1) //We have a data after parameter
			{
				if(data[i + 1][0] != '-') //We have a value
				{
					if(wcscmp(data[i] + 1, L"pointSize") == 0)
					{
						pointSize = _wtof(data[i + 1]);
					}
					else if(wcscmp(data[i] + 1, L"updateDelay") == 0)
					{
						updateDataDelay = _wtoi(data[i + 1]);
					}
					else if(wcscmp(data[i] + 1, L"particles") == 0)
					{
						partCount = _wtoi(data[i + 1]);
						auto align = partCount % 4;
						if(align != 0)
							partCount -= align;
					}
					else if(wcscmp(data[i] + 1, L"animateSlowDown") == 0)
					{
						animateSlowDown = _wtoi(data[i + 1]);
					}
					else if(wcscmp(data[i] + 1, L"G") == 0)
					{
						G = _wtof(data[i + 1]);
					}
					else if(wcscmp(data[i] + 1, L"animationType") == 0)
					{
						animationType = _wtoi(data[i + 1]);
					}
					else if(wcscmp(data[i] + 1, L"mathModel") == 0)
					{
						mathModel = _wtoi(data[i + 1]);
					}
				}
				else if(wcscmp(data[i] + 1, L"manual") == 0)
				{
					ManualControl = true;
				}
			}
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nShow)
{

	initVariables();

	MSG msg;

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_VREDRAW | CS_HREDRAW | CS_OWNDC,
		WndProc, 0, 0, hInstance, NULL, NULL, (HBRUSH)(COLOR_WINDOW + 1),
		NULL, L"RenderToTextureClass", NULL };

	RegisterClassEx(&wc);

	Width = GetSystemMetrics(SM_CXSCREEN);
	Height = GetSystemMetrics(SM_CYSCREEN);

	r = (min(Height, Width)*0.7) / 2; //Radius for animation (UpdatePosition)

	hMainWnd = CreateWindow(L"RenderToTextureClass",
		L"Render to texture",
		WS_POPUP, 0, 0, Width, Height,

		NULL, NULL, hInstance, NULL);

	Init();

	ShowWindow(hMainWnd, nShow);
	UpdateWindow(hMainWnd);

	timer = SetTimer(hMainWnd, 0, updateDataDelay, 0);

	while(GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	CleanUp();

	return(0);
}

void DrawParticles()
{
	device->SetStreamSource(0, pVertexObject, 0, sizeof(VertexData));
	device->SetVertexDeclaration(vertexDecl);

	device->BeginScene();

	device->DrawPrimitive(D3DPRIMITIVETYPE::D3DPT_POINTLIST, 0, partCount);

	device->EndScene();
}

void DrawRect()
{
	device->SetStreamSource(0, pRectObject, 0, sizeof(VertexData));
	device->SetVertexDeclaration(RectDecl);

	device->BeginScene();

	device->DrawPrimitive(D3DPRIMITIVETYPE::D3DPT_TRIANGLESTRIP, 0, 2);

	device->EndScene();
}

void Render()
{
	UINT passes = 0;
	effect->Begin(&passes, 0);
	for(UINT i = 0; i < passes; ++i)
	{
		effect->BeginPass(i);

		if(i == 0)
		{
			device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

			device->SetRenderTarget(0, renderTarget);
			device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
			DrawParticles();
		}
		else if(i == 1)
		{
			device->SetRenderTarget(0, orig);

			DrawRect();
		}

		effect->EndPass();
	}
	effect->End();

	device->Present(NULL, NULL, NULL, NULL);
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
		case WM_SETCURSOR:
			SetCursor(LoadCursor(NULL, IDC_CROSS));
			device->ShowCursor(TRUE);

			return TRUE;

		case WM_PAINT:

			Render();
			ValidateRect(hwnd, NULL);
			return 0;

		case WM_TIMER:

			animate();
			InvalidateRect(hwnd, 0, FALSE);
			return 0;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
			if(ManualControl)
			{
				G = -G;
			}
			return 0;

		case WM_MOUSEWHEEL:
			if(ManualControl)
			{
				const auto keyState = GET_KEYSTATE_WPARAM(wParam);
				int wheelDelta;

				if(keyState & MK_SHIFT)
				{
					wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam) / 6;
				}
				else
				{
					wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam) / 120;
				}

				G += wheelDelta*0.05f;
			}
			return 0;

		case WM_RBUTTONUP:
			if(ManualControl)
			{
				G = 0;
			}

			return 0;

		case WM_KEYUP:
			if(ManualControl)
			{
				switch(wParam)
				{
					case VK_ESCAPE:
						ShowWindow(hMainWnd, SW_HIDE);
						PostQuitMessage(0);
						break;
					case VK_SPACE:
						Resistance = 1;
						break;
					case VK_TAB:
						Resistance = defResistance;
						break;
					case 'Q':
						Resistance += 0.001f;

						if(Resistance > 1.f)
							Resistance = 1.f;
						break;
					case 'W':
						Resistance -= 0.001f;

						if(Resistance < 0)
							Resistance = 0;
						break;
					case 'S':
						Resistance = 0;
						G = 0;
						break;
					case 'G':
						G = defG;
						break;
				}
			}
			else
			{
				switch(wParam)
				{
					case VK_ESCAPE:
						ShowWindow(hMainWnd, SW_HIDE);
						PostQuitMessage(0);
						break;
				}
			}
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}

	return(DefWindowProc(hwnd, msg, wParam, lParam));
}