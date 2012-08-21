// RenderToTexture.cpp: определяет точку входа для приложения.
//
#include "stdafx.h"
#include "RenderToTexture.h"
#include <shellapi.h>

#define SHIPPING_VERSION 

LPDIRECT3D9 d3d = NULL;
LPDIRECT3DDEVICE9 device = NULL;
IDirect3DTexture9* renderTexture = NULL, *particleTexture = NULL,
	*overlayTexture = NULL;

IDirect3DSurface9* orig =NULL
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

#ifdef _DEBUG
int partCount = 50000; // Смещение между создаваемыми частицами
#else
int partCount = 500000; // Смещение между создаваемыми частицами
#endif

int updateDataDelay = 8;

float pointSize = 3;



struct VertexData
{
	float x,y,z;
	float u,v;
};

struct Particle
{
	float x, y, vx, vy;
};
std::deque<Particle> particles;

void animate()
{
	POINT pos;
	GetCursorPos(&pos);
	RECT rc;
	GetClientRect(hMainWnd, &rc); 
	ScreenToClient(hMainWnd, &pos);

	const int mx = pos.x;
	const int my = pos.y;
	const auto size = particles.size();

	float force;
	float dist;

	VertexData *pVertexBuffer;
	pVertexObject->Lock(0, 0, (void**)&pVertexBuffer, D3DLOCK_DISCARD);

	int i;
#pragma omp parallel \
	shared(pVertexBuffer, particles, mx, my, size) \
	private(i, force, dist)
{
		#pragma omp for
		for( i = 0; i < size; ++i )
		{
			auto &x = particles[i].x;
			auto &y = particles[i].y;

			dist = sqrt( pow( x - mx, 2 ) + pow( y - my, 2 ) );
			if( dist < 20 )
			{
				force = 0;
			}
			else
			{
				force = G / pow( dist, 2 );
			}

			const float xForce = (mx - x) * force;
			const float yForce = (my - y) * force;

			particles[i].vx *= Resistance;
			particles[i].vy *= Resistance;

			particles[i].vx += xForce;
			particles[i].vy += yForce;

			x+= particles[i].vx;
			y+= particles[i].vy;

			if( x > Width )
				x -= Width;
			else if( x < 0 )
				x += Width;

			if( y > Height )
				y -= Height;
			else if( y < 0 )
				y += Height;

			pVertexBuffer[i].x = particles[i].x;
			pVertexBuffer[i].y = particles[i].y;
		}
	}
	pVertexObject->Unlock();
}

void initParticles()
{
	srand(clock());

	Particle tmp;
	for( int i = 0; i<partCount; ++i )
	{
			tmp.x  = rand()%Width;
			tmp.y  = rand()%Height;
			tmp.vx = rand()%2;
			tmp.vy = rand()%2;

			particles.push_back( tmp );
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
	UINT AdapterToUse=D3DADAPTER_DEFAULT;
	D3DDEVTYPE DeviceType=D3DDEVTYPE_HAL;
#ifdef SHIPPING_VERSION
	d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hMainWnd, 
		D3DCREATE_HARDWARE_VERTEXPROCESSING, &PresentParams,
		&device);
#else
	// Look for 'NVIDIA PerfHUD' adapter
	// If it is present, override default settings
	for (UINT Adapter=0;Adapter<d3d->GetAdapterCount();Adapter++) 
	{
		D3DADAPTER_IDENTIFIER9  Identifier;
		HRESULT  Res;
		Res = d3d->GetAdapterIdentifier(Adapter,0,&Identifier);
		if (strstr(Identifier.Description,"PerfHUD") != 0)
		{
			AdapterToUse=Adapter;
			DeviceType=D3DDEVTYPE_REF;
			break;
		}
	}
#endif
	if (FAILED(d3d->CreateDevice( AdapterToUse, DeviceType, hMainWnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING,
		&PresentParams, &device) ) )
	{
		MessageBoxA(hMainWnd, "Unable to initialize PerfectHUD",0,MB_ICONHAND);
		terminate();
	}

	//////////////////////////////////////////////////////////////////////

	device->SetRenderState(D3DRS_POINTSIZE_MAX, *((DWORD*)&pointSize));
	device->SetRenderState(D3DRS_POINTSIZE, *((DWORD*)&pointSize));
	device->SetRenderState(D3DRS_LIGHTING,FALSE);
	device->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE ); 
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
		&D3DXVECTOR3(0,0,0),
		&D3DXVECTOR3(0,0,1),
		&D3DXVECTOR3(0,1,0));

	D3DXMatrixOrthoOffCenterLH(&matrixProjection, 0, Width, Height, 0, 0, 255);

	device->SetTransform(D3DTS_VIEW,&matrixView);
	device->SetTransform(D3DTS_PROJECTION,&matrixProjection);

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
		&errorBuffer );

	if( errorBuffer )
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

	effect->SetTechnique( effect->GetTechnique(0) );

	auto hr = effect->SetTexture( effect->GetParameterByName(NULL, "Overlay"), overlayTexture);
	hr |= effect->SetTexture( effect->GetParameterByName(NULL, "Base"), particleTexture);
	hr |= effect->SetTexture( effect->GetParameterByName(NULL, "PreRender"), renderTexture);

	if(hr != 0)
	{
		MessageBox(hMainWnd, L"Unable to set effect textures.", L"", MB_ICONHAND);
	}
}
void initVertexData() 
{
	size_t count = particles.size();
	VertexData *vertexData = new VertexData[count];

	for(size_t i=0; i<count; ++i)
	{
		vertexData[i].x = particles[i].x;
		vertexData[i].y = particles[i].y;
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
		{0,			0,			0,		0,	0},
		{Width,		0,			0,		1,	0},
		{0,			Height,		0,		0,	1},
		{Width,		Height,		0,		1,	1}
	};

	void *pRectBuffer = NULL; 
	device->CreateVertexBuffer(4*sizeof(VertexData), D3DUSAGE_WRITEONLY,
		D3DFVF_XYZ | D3DFVF_TEX0, D3DPOOL_DEFAULT, &pRectObject, NULL);

	pRectObject->Lock(0, 4*sizeof(VertexData), &pRectBuffer, 0);

	memcpy(pRectBuffer, data, 4*sizeof(VertexData));
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
	if(pVertexObject) pVertexObject->Release();
	if(vertexDecl) vertexDecl->Release();
	if(pRectObject) pRectObject->Release();
	if(RectDecl) RectDecl->Release();
	if(effect) effect->Release();
	if(renderTarget) renderTarget->Release();

	if(overlayTexture) overlayTexture->Release();
	if(particleTexture) particleTexture->Release();
	if(renderTexture) renderTexture->Release();

	if(device) device->Release();
	if(d3d) d3d->Release();
}

void initVariables() 
{
	int args = 0;
	auto data = CommandLineToArgvW(GetCommandLineW(), &args);

	for(int i=1; i<args; ++i)
	{
		if(data[i][0] == '-') //We recive a parameter
		{
			if(i < args-1) //We have a data after parameter
			{
				if(data[i+1][0] != '-') //We have a value
				{
					if(wcscmp(data[i]+1, L"pointSize") == 0)
					{
						pointSize = _wtof(data[i+1]);
					}
					else if(wcscmp(data[i]+1, L"updateDelay") == 0)
					{
						updateDataDelay = _wtoi(data[i+1]);
					}
					else if(wcscmp(data[i]+1, L"particles") == 0)
					{
						partCount = _wtoi(data[i+1]);
					}
				}
			}
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nShow)
{

	initVariables();

	MSG msg;

	WNDCLASSEX wc = {sizeof(WNDCLASSEX), CS_VREDRAW|CS_HREDRAW|CS_OWNDC, 
		WndProc, 0, 0, hInstance, NULL, NULL, (HBRUSH)(COLOR_WINDOW+1), 
		NULL, L"RenderToTextureClass", NULL}; 

	RegisterClassEx(&wc);

	Width = GetSystemMetrics(SM_CXSCREEN);
	Height = GetSystemMetrics(SM_CYSCREEN);

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

	device->DrawPrimitive(D3DPRIMITIVETYPE::D3DPT_POINTLIST, 0, particles.size());

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
	for(UINT i=0; i<passes; ++i)
	{
		effect->BeginPass(i);

		if(i == 0)
		{
			device->Clear( 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,0), 1.0f, 0 );

			device->SetRenderTarget(0, renderTarget);
			device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0,0,0), 1.0f, 0);
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


		SetCursor( LoadCursor(NULL, IDC_CROSS) );
		device->ShowCursor( TRUE );

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
		G=-G;
		return 0;

	case WM_MOUSEWHEEL:
		{
			const auto keyState = GET_KEYSTATE_WPARAM(wParam);
			int wheelDelta;

			if(keyState & MK_SHIFT)
			{
				wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam)/6;
			}
			else
			{
				wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam)/120;
			}

			G+= wheelDelta*0.05f;
			return 0;
		}

	case WM_RBUTTONUP:
		{
			G=0;
			return 0;
		}

		return 0;

	case WM_KEYUP:
		{
			switch (wParam)
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
				Resistance+= 0.001f;

				if(Resistance > 1.f)
					Resistance = 1.f;
				break;
			case 'W':
				Resistance-= 0.001f;

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
			return 0;
		}

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return(DefWindowProc(hwnd, msg, wParam, lParam));
}