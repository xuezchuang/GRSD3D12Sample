#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN // 从 Windows 头中排除极少使用的资料
#include <windows.h>
#include <tchar.h>
#include <fstream>  //for ifstream
#include <wrl.h> //添加WTL支持 方便使用COM
#include <atlconv.h>
#include <atlcoll.h>  //for atl array
#include <strsafe.h>  //for StringCchxxxxx function
#include <dxgi1_6.h>
#include <d3d12.h> //for d3d12
#include <d3dcompiler.h>
#if defined(_DEBUG)
#include <dxgidebug.h>
#endif
#include <DirectXMath.h>
#include "..\WindowsCommons\DDSTextureLoader12.h"

using namespace std;
using namespace Microsoft;
using namespace Microsoft::WRL;
using namespace DirectX;

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

#define GRS_USEPRINTF() TCHAR pBuf[1024] = {};TCHAR pszOutput[1024] = {};
#define GRS_PRINTF(...) \
    StringCchPrintf(pBuf,1024,__VA_ARGS__);\
	StringCchPrintf(pszOutput,1024,_T("【ID:% 8u】：%s"),::GetCurrentThreadId(),pBuf);\
	OutputDebugString(pszOutput);

#define GRS_WND_CLASS_NAME _T("GRS Game Window Class")
#define GRS_WND_TITLE	_T("GRS DirectX12 Multithreaded Rendering Sample")

#define GRS_THROW_IF_FAILED(hr) {HRESULT _hr = (hr);if (FAILED(_hr)){ throw CGRSCOMException(_hr); }}

//新定义的宏用于上取整除法
#define GRS_UPPER_DIV(A,B) ((UINT)(((A)+((B)-1))/(B)))
//更简洁的向上边界对齐算法 内存管理中常用 请记住
#define GRS_UPPER(A,B) ((UINT)(((A)+((B)-1))&~(B - 1)))

// 内存分配的宏定义
#define GRS_ALLOC(sz)		::HeapAlloc(GetProcessHeap(),0,(sz))
#define GRS_CALLOC(sz)		::HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(sz))
#define GRS_CREALLOC(p,sz)	::HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(p),(sz))
#define GRS_SAFE_FREE(p)		if( nullptr != (p) ){ ::HeapFree( ::GetProcessHeap(),0,(p) ); (p) = nullptr; }

//------------------------------------------------------------------------------------------------------------
// 为了调试加入下面的内联函数和宏定义，为每个接口对象设置名称，方便查看调试输出
#if defined(_DEBUG)
inline void GRS_SetD3D12DebugName(ID3D12Object* pObject, LPCWSTR name)
{
	pObject->SetName(name);
}

inline void GRS_SetD3D12DebugNameIndexed(ID3D12Object* pObject, LPCWSTR name, UINT index)
{
	WCHAR _DebugName[MAX_PATH] = {};
	if (SUCCEEDED(StringCchPrintfW(_DebugName, _countof(_DebugName), L"%s[%u]", name, index)))
	{
		pObject->SetName(_DebugName);
	}
}
#else

inline void GRS_SetD3D12DebugName(ID3D12Object*, LPCWSTR)
{
}
inline void GRS_SetD3D12DebugNameIndexed(ID3D12Object*, LPCWSTR, UINT)
{
}

#endif

#define GRS_SET_D3D12_DEBUGNAME(x)						GRS_SetD3D12DebugName(x, L#x)
#define GRS_SET_D3D12_DEBUGNAME_INDEXED(x, n)			GRS_SetD3D12DebugNameIndexed(x[n], L#x, n)

#define GRS_SET_D3D12_DEBUGNAME_COMPTR(x)				GRS_SetD3D12DebugName(x.Get(), L#x)
#define GRS_SET_D3D12_DEBUGNAME_INDEXED_COMPTR(x, n)	GRS_SetD3D12DebugNameIndexed(x[n].Get(), L#x, n)

#if defined(_DEBUG)
inline void GRS_SetDXGIDebugName(IDXGIObject* pObject, LPCWSTR name)
{
	size_t szLen = 0;
	StringCchLengthW(name, 50, &szLen);
	pObject->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(szLen - 1), name);
}

inline void GRS_SetDXGIDebugNameIndexed(IDXGIObject* pObject, LPCWSTR name, UINT index)
{
	size_t szLen = 0;
	WCHAR _DebugName[MAX_PATH] = {};
	if (SUCCEEDED(StringCchPrintfW(_DebugName, _countof(_DebugName), L"%s[%u]", name, index)))
	{
		StringCchLengthW(_DebugName, _countof(_DebugName), &szLen);
		pObject->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(szLen), _DebugName);
	}
}
#else

inline void GRS_SetDXGIDebugName(IDXGIObject*, LPCWSTR)
{
}
inline void GRS_SetDXGIDebugNameIndexed(IDXGIObject*, LPCWSTR, UINT)
{
}

#endif

#define GRS_SET_DXGI_DEBUGNAME(x)						GRS_SetDXGIDebugName(x, L#x)
#define GRS_SET_DXGI_DEBUGNAME_INDEXED(x, n)			GRS_SetDXGIDebugNameIndexed(x[n], L#x, n)

#define GRS_SET_DXGI_DEBUGNAME_COMPTR(x)				GRS_SetDXGIDebugName(x.Get(), L#x)
#define GRS_SET_DXGI_DEBUGNAME_INDEXED_COMPTR(x, n)		GRS_SetDXGIDebugNameIndexed(x[n].Get(), L#x, n)
//------------------------------------------------------------------------------------------------------------


class CGRSCOMException
{
public:
	CGRSCOMException(HRESULT hr) : m_hrError(hr)
	{
	}
	HRESULT Error() const
	{
		return m_hrError;
	}
private:
	const HRESULT m_hrError;
};

// 顶点结构
struct ST_GRS_VERTEX
{
	XMFLOAT4 m_v4Position;		//Position
	XMFLOAT2 m_vTex;		//Texcoord
	XMFLOAT3 m_vNor;		//Normal
};

// 常量缓冲区
struct ST_GRS_MVP
{
	XMFLOAT4X4 m_MVP;			//经典的Model-view-projection(MVP)矩阵.
};

// 渲染子线程参数
struct ST_GRS_THREAD_PARAMS
{
	UINT								nIndex;				//序号
	DWORD								dwThisThreadID;
	HANDLE								hThisThread;
	DWORD								dwMainThreadID;
	HANDLE								hMainThread;
	HANDLE								hRunEvent;
	//HANDLE								hEventRenderOver;
	UINT								nCurrentFrameIndex;//当前渲染后缓冲序号
	ULONGLONG							nStartTime;		   //当前帧开始时间
	ULONGLONG							nCurrentTime;	   //当前时间
	XMFLOAT4							v4ModelPos;
	TCHAR								pszDDSFile[MAX_PATH];
	CHAR								pszMeshFile[MAX_PATH];
	ID3D12Device4*						pID3D12Device4;
	ID3D12CommandAllocator*				pICmdAlloc;
	ID3D12GraphicsCommandList*			pICmdList;
	ID3D12RootSignature*				pIRS;
	ID3D12PipelineState*				pIPSO;

	UINT								m_nPassNO;
};

int									g_iWndWidth = 1024;
int									g_iWndHeight = 768;

D3D12_VIEWPORT						g_stViewPort = { 0.0f, 0.0f, static_cast<float>(g_iWndWidth), static_cast<float>(g_iWndHeight), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
D3D12_RECT							g_stScissorRect = { 0, 0, static_cast<LONG>(g_iWndWidth), static_cast<LONG>(g_iWndHeight) };

//初始的默认摄像机的位置
XMFLOAT3							g_f3EyePos = XMFLOAT3(0.0f, 5.0f, -10.0f);  //眼睛位置
XMFLOAT3							g_f3LockAt = XMFLOAT3(0.0f, 0.0f, 0.0f);    //眼睛所盯的位置
XMFLOAT3							g_f3HeapUp = XMFLOAT3(0.0f, 1.0f, 0.0f);    //头部正上方位置

float								g_fYaw = 0.0f;			// 绕正Z轴的旋转量.
float								g_fPitch = 0.0f;			// 绕XZ平面的旋转量

double								g_fPalstance = 10.0f * XM_PI / 180.0f;	//物体旋转的角速度，单位：弧度/秒

XMFLOAT4X4							g_mxWorld = {}; //World Matrix
XMFLOAT4X4							g_mxVP = {};    //View Projection Matrix

// 全局线程参数
const UINT							g_nMaxThread = 3;
const UINT							g_nThdSphere = 0;
const UINT							g_nThdCube = 1;
const UINT							g_nThdPlane = 2;
ST_GRS_THREAD_PARAMS				g_stThreadParams[g_nMaxThread] = {};

const UINT							g_nFrameBackBufCount = 3u;
UINT								g_nRTVDescriptorSize = 0U;
ComPtr<ID3D12Resource>				g_pIARenderTargets[g_nFrameBackBufCount];

ComPtr<ID3D12DescriptorHeap>		g_pIRTVHeap;
ComPtr<ID3D12DescriptorHeap>		g_pIDSVHeap;				//深度缓冲描述符堆

TCHAR								g_pszAppPath[MAX_PATH] = {};


// 线程池管理内核同步对象
HANDLE									g_hEventPassOver;
HANDLE									g_hSemaphore;
SYNCHRONIZATION_BARRIER					g_stCPUThdBarrier = {};

UINT __stdcall		RenderThread(void* pParam);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL				LoadMeshVertex(const CHAR*pszMeshFileName, UINT&nVertexCnt, ST_GRS_VERTEX*&ppVertex, UINT*&ppIndices);

#include "GameTimer.h"
int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR    lpCmdLine, int nCmdShow)
{
	::CoInitialize(nullptr);  //for WIC & COM

	HWND								hWnd = nullptr;
	MSG									msg = {};

	UINT								nDXGIFactoryFlags = 0U;
	const float							faClearColor[] = { 0.2f, 0.5f, 1.0f, 1.0f };

	ComPtr<IDXGIFactory5>				pIDXGIFactory5;
	ComPtr<IDXGIAdapter1>				pIAdapter1;
	ComPtr<ID3D12Device4>				pID3D12Device4;
	ComPtr<ID3D12CommandQueue>			pIMainCmdQueue;
	ComPtr<IDXGISwapChain1>				pISwapChain1;
	ComPtr<IDXGISwapChain3>				pISwapChain3;

	ComPtr<ID3D12Resource>				pIDepthStencilBuffer;	//深度蜡板缓冲区
	UINT								nCurrentFrameIndex = 0;

	DXGI_FORMAT							emRTFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT							emDSFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ComPtr<ID3D12Fence>					pIFence;
	UINT64								n64FenceValue = 1ui64;
	HANDLE								hEventFence = nullptr;

	ComPtr<ID3D12CommandAllocator>		pICmdAllocPre;
	ComPtr<ID3D12GraphicsCommandList>	pICmdListPre;

	ComPtr<ID3D12CommandAllocator>		pICmdAllocPost;
	ComPtr<ID3D12GraphicsCommandList>	pICmdListPost;

	CAtlArray<HANDLE>					arHWaited;
	CAtlArray<HANDLE>					arHSubThread;

	ComPtr<ID3DBlob>					pIVSSphere;
	ComPtr<ID3DBlob>					pIPSSphere;
	ComPtr<ID3D12RootSignature>			pIRootSignature;
	ComPtr<ID3D12PipelineState>			pIPSOSphere;

	try
	{
		// 得到当前的工作目录，方便我们使用相对路径来访问各种资源文件
		{
			if (0 == ::GetModuleFileName(nullptr, g_pszAppPath, MAX_PATH))
			{
				GRS_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
			}

			WCHAR* lastSlash = _tcsrchr(g_pszAppPath, _T('\\'));
			if (lastSlash)
			{//删除Exe文件名
				*(lastSlash) = _T('\0');
			}

			lastSlash = _tcsrchr(g_pszAppPath, _T('\\'));
			if (lastSlash)
			{//删除x64路径
				*(lastSlash) = _T('\0');
			}

			lastSlash = _tcsrchr(g_pszAppPath, _T('\\'));
			if (lastSlash)
			{//删除Debug 或 Release路径
				*(lastSlash + 1) = _T('\0');
			}
		}

		//1、创建窗口
		{
			//---------------------------------------------------------------------------------------------
			WNDCLASSEX wcex = {};
			wcex.cbSize = sizeof(WNDCLASSEX);
			wcex.style = CS_GLOBALCLASS;
			wcex.lpfnWndProc = WndProc;
			wcex.cbClsExtra = 0;
			wcex.cbWndExtra = 0;
			wcex.hInstance = hInstance;
			wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wcex.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);		//防止无聊的背景重绘
			wcex.lpszClassName = GRS_WND_CLASS_NAME;
			RegisterClassEx(&wcex);

			DWORD dwWndStyle = WS_OVERLAPPED | WS_SYSMENU;
			RECT rtWnd = { 0, 0, g_iWndWidth, g_iWndHeight };
			AdjustWindowRect(&rtWnd, dwWndStyle, FALSE);

			// 计算窗口居中的屏幕坐标
			INT posX = (GetSystemMetrics(SM_CXSCREEN) - rtWnd.right - rtWnd.left) / 2;
			INT posY = (GetSystemMetrics(SM_CYSCREEN) - rtWnd.bottom - rtWnd.top) / 2;

			hWnd = CreateWindowW(GRS_WND_CLASS_NAME, GRS_WND_TITLE, dwWndStyle
				, posX, posY, rtWnd.right - rtWnd.left, rtWnd.bottom - rtWnd.top
				, nullptr, nullptr, hInstance, nullptr);

			if (!hWnd)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}
		}

		// 创建用于CPU子渲染线程池的同步对象
		{
			// 通知线程池启动渲染的信标量，初始信标个数为0，最大信标个数即线程池线程个数
			g_hSemaphore = ::CreateSemaphore(nullptr, 0, g_nMaxThread, nullptr);
			if (!g_hSemaphore)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			// 线程池Pass渲染结束事件句柄
			g_hEventPassOver = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (!g_hEventPassOver)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			// 创建CPU线程屏障对象，允许进入的线程数即线程池线程个数
			if (!InitializeSynchronizationBarrier(&g_stCPUThdBarrier, g_nMaxThread, -1))
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}
		}

		//2、打开显示子系统的调试支持
		{
#if defined(_DEBUG)
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				// 打开附加的调试支持
				nDXGIFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
#endif
		}

		//3、创建DXGI Factory对象
		{
			GRS_THROW_IF_FAILED(CreateDXGIFactory2(nDXGIFactoryFlags, IID_PPV_ARGS(&pIDXGIFactory5)));
			GRS_SET_DXGI_DEBUGNAME_COMPTR(pIDXGIFactory5);
		}

		//4、枚举适配器创建设备
		{//选择NUMA架构的独显来创建3D设备对象,暂时先不支持集显了，当然你可以修改这些行为
			D3D12_FEATURE_DATA_ARCHITECTURE stArchitecture = {};
			DXGI_ADAPTER_DESC1 stAdapterDesc = {};
			for (UINT nAdapterIndex = 0; DXGI_ERROR_NOT_FOUND != pIDXGIFactory5->EnumAdapters1(nAdapterIndex, &pIAdapter1); ++nAdapterIndex)
			{
				pIAdapter1->GetDesc1(&stAdapterDesc);

				if (stAdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{//跳过软件虚拟适配器设备
					continue;
				}

				GRS_THROW_IF_FAILED(D3D12CreateDevice(pIAdapter1.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pID3D12Device4)));
				GRS_THROW_IF_FAILED(pID3D12Device4->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE
					, &stArchitecture, sizeof(D3D12_FEATURE_DATA_ARCHITECTURE)));

				if (!stArchitecture.UMA)
				{
					break;
				}

				pID3D12Device4.Reset();
			}

			//---------------------------------------------------------------------------------------------
			if (nullptr == pID3D12Device4.Get())
			{// 可怜的机器上居然没有独显 还是先退出了事 
				throw CGRSCOMException(E_FAIL);
			}

			GRS_SET_D3D12_DEBUGNAME_COMPTR(pID3D12Device4);

			TCHAR pszWndTitle[MAX_PATH] = {};
			GRS_THROW_IF_FAILED(pIAdapter1->GetDesc1(&stAdapterDesc));
			::GetWindowText(hWnd, pszWndTitle, MAX_PATH);
			StringCchPrintf(pszWndTitle
				, MAX_PATH
				, _T("%s (GPU:%s)")
				, pszWndTitle
				, stAdapterDesc.Description);
			::SetWindowText(hWnd, pszWndTitle);
		}

		//5、创建直接命令队列
		{
			D3D12_COMMAND_QUEUE_DESC stQueueDesc = {};
			stQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandQueue(&stQueueDesc, IID_PPV_ARGS(&pIMainCmdQueue)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIMainCmdQueue);
		}

		//6、创建交换链
		{
			DXGI_SWAP_CHAIN_DESC1 stSwapChainDesc = {};
			stSwapChainDesc.BufferCount = g_nFrameBackBufCount;
			stSwapChainDesc.Width = g_iWndWidth;
			stSwapChainDesc.Height = g_iWndHeight;
			stSwapChainDesc.Format = emRTFormat;
			stSwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			stSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			stSwapChainDesc.SampleDesc.Count = 1;

			GRS_THROW_IF_FAILED(pIDXGIFactory5->CreateSwapChainForHwnd(
				pIMainCmdQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
				hWnd,
				&stSwapChainDesc,
				nullptr,
				nullptr,
				&pISwapChain1
			));
			GRS_SET_DXGI_DEBUGNAME_COMPTR(pISwapChain1);

			//注意此处使用了高版本的SwapChain接口的函数
			GRS_THROW_IF_FAILED(pISwapChain1.As(&pISwapChain3));
			GRS_SET_DXGI_DEBUGNAME_COMPTR(pISwapChain3);

			// 获取当前第一个供绘制的后缓冲序号
			nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();

			//创建RTV(渲染目标视图)描述符堆(这里堆的含义应当理解为数组或者固定大小元素的固定大小显存池)
			D3D12_DESCRIPTOR_HEAP_DESC stRTVHeapDesc = {};
			stRTVHeapDesc.NumDescriptors = g_nFrameBackBufCount;
			stRTVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			stRTVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&stRTVHeapDesc, IID_PPV_ARGS(&g_pIRTVHeap)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(g_pIRTVHeap);

			//得到每个描述符元素的大小
			g_nRTVDescriptorSize = pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			//---------------------------------------------------------------------------------------------
			D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = g_pIRTVHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT i = 0; i < g_nFrameBackBufCount; i++)
			{//这个循环暴漏了描述符堆实际上是个数组的本质
				GRS_THROW_IF_FAILED(pISwapChain3->GetBuffer(i, IID_PPV_ARGS(&g_pIARenderTargets[i])));
				GRS_SET_D3D12_DEBUGNAME_INDEXED_COMPTR(g_pIARenderTargets, i);
				pID3D12Device4->CreateRenderTargetView(g_pIARenderTargets[i].Get(), nullptr, stRTVHandle);
				stRTVHandle.ptr += g_nRTVDescriptorSize;
			}

			// 关闭ALT+ENTER键切换全屏的功能，因为我们没有实现OnSize处理，所以先关闭
			GRS_THROW_IF_FAILED(pIDXGIFactory5->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
		}

		//7、创建深度缓冲及深度缓冲描述符堆
		{
			D3D12_HEAP_PROPERTIES stDSBufHeapDesc = {};

			stDSBufHeapDesc.Type = D3D12_HEAP_TYPE_DEFAULT;
			stDSBufHeapDesc.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stDSBufHeapDesc.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stDSBufHeapDesc.CreationNodeMask = 0;
			stDSBufHeapDesc.VisibleNodeMask = 0;

			D3D12_CLEAR_VALUE stDepthOptimizedClearValue = {};
			stDepthOptimizedClearValue.Format = emDSFormat;
			stDepthOptimizedClearValue.DepthStencil.Depth = 1.0f;
			stDepthOptimizedClearValue.DepthStencil.Stencil = 0;

			D3D12_RESOURCE_DESC stDSResDesc = {};
			stDSResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			stDSResDesc.Alignment = 0;
			stDSResDesc.Format = emDSFormat;
			stDSResDesc.Width = g_iWndWidth;
			stDSResDesc.Height = g_iWndHeight;
			stDSResDesc.DepthOrArraySize = 1;
			stDSResDesc.MipLevels = 0;
			stDSResDesc.SampleDesc.Count = 1;
			stDSResDesc.SampleDesc.Quality = 0;
			stDSResDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			stDSResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			//使用隐式默认堆创建一个深度蜡板缓冲区，
			//因为基本上深度缓冲区会一直被使用，重用的意义不大，所以直接使用隐式堆，图方便
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommittedResource(
				&stDSBufHeapDesc
				, D3D12_HEAP_FLAG_NONE
				, &stDSResDesc
				, D3D12_RESOURCE_STATE_DEPTH_WRITE
				, &stDepthOptimizedClearValue
				, IID_PPV_ARGS(&pIDepthStencilBuffer)
			));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIDepthStencilBuffer);

			D3D12_DEPTH_STENCIL_VIEW_DESC stDepthStencilDesc = {};
			stDepthStencilDesc.Format = emDSFormat;
			stDepthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			stDepthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;

			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = 1;
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_pIDSVHeap)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(g_pIDSVHeap);

			pID3D12Device4->CreateDepthStencilView(pIDepthStencilBuffer.Get()
				, &stDepthStencilDesc
				, g_pIDSVHeap->GetCPUDescriptorHandleForHeapStart());
		}

		//8、创建直接命令列表
		{
			// 预处理命令列表
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT
				, IID_PPV_ARGS(&pICmdAllocPre)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pICmdAllocPre);
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT
				, pICmdAllocPre.Get(), nullptr, IID_PPV_ARGS(&pICmdListPre)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pICmdListPre);

			//后处理命令列表
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT
				, IID_PPV_ARGS(&pICmdAllocPost)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pICmdAllocPost);
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT
				, pICmdAllocPost.Get(), nullptr, IID_PPV_ARGS(&pICmdListPost)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pICmdListPost);
		}

		//9、创建根签名
		{//这个例子中，所有物体使用相同的根签名，因为渲染过程中需要的参数是一样的
			D3D12_FEATURE_DATA_ROOT_SIGNATURE stFeatureData = {};
			// 检测是否支持V1.1版本的根签名
			stFeatureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if (FAILED(pID3D12Device4->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &stFeatureData, sizeof(stFeatureData))))
			{// 1.0版 直接丢异常退出了
				GRS_THROW_IF_FAILED(E_NOTIMPL);
			}

			D3D12_DESCRIPTOR_RANGE1 stDSPRanges[3] = {};

			stDSPRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			stDSPRanges[0].NumDescriptors = 1;
			stDSPRanges[0].BaseShaderRegister = 0;
			stDSPRanges[0].RegisterSpace = 0;
			stDSPRanges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			stDSPRanges[0].OffsetInDescriptorsFromTableStart = 0;

			stDSPRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			stDSPRanges[1].NumDescriptors = 1;
			stDSPRanges[1].BaseShaderRegister = 0;
			stDSPRanges[1].RegisterSpace = 0;
			stDSPRanges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			stDSPRanges[1].OffsetInDescriptorsFromTableStart = 0;

			stDSPRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			stDSPRanges[2].NumDescriptors = 1;
			stDSPRanges[2].BaseShaderRegister = 0;
			stDSPRanges[2].RegisterSpace = 0;
			stDSPRanges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
			stDSPRanges[2].OffsetInDescriptorsFromTableStart = 0;

			D3D12_ROOT_PARAMETER1 stRootParameters[3] = {};

			stRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			stRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			stRootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
			stRootParameters[0].DescriptorTable.pDescriptorRanges = &stDSPRanges[0];

			stRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			stRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			stRootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
			stRootParameters[1].DescriptorTable.pDescriptorRanges = &stDSPRanges[1];

			stRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			stRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
			stRootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
			stRootParameters[2].DescriptorTable.pDescriptorRanges = &stDSPRanges[2];

			D3D12_VERSIONED_ROOT_SIGNATURE_DESC stRootSignatureDesc = {};
			stRootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
			stRootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
			stRootSignatureDesc.Desc_1_1.NumParameters = _countof(stRootParameters);
			stRootSignatureDesc.Desc_1_1.pParameters = stRootParameters;
			stRootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
			stRootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
			
			ComPtr<ID3DBlob> pISignatureBlob;
			ComPtr<ID3DBlob> pIErrorBlob;
			GRS_THROW_IF_FAILED(D3D12SerializeVersionedRootSignature(&stRootSignatureDesc
				, &pISignatureBlob
				, &pIErrorBlob));

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateRootSignature(0
				, pISignatureBlob->GetBufferPointer()
				, pISignatureBlob->GetBufferSize()
				, IID_PPV_ARGS(&pIRootSignature)));

			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIRootSignature);
		}

		//10、编译Shader创建渲染管线状态对象
		{
#if defined(_DEBUG)
			// Enable better shader debugging with the graphics debugging tools.
			UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
			UINT compileFlags = 0;
#endif
			//编译为行矩阵形式	   
			compileFlags |= D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;

			TCHAR pszShaderFileName[MAX_PATH] = {};
			StringCchPrintf(pszShaderFileName, MAX_PATH, _T("%s6-MultiThread\\Shader\\TextureCube.hlsl"), g_pszAppPath);

			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr
				, "VSMain", "vs_5_0", compileFlags, 0, &pIVSSphere, nullptr));
			GRS_THROW_IF_FAILED(D3DCompileFromFile(pszShaderFileName, nullptr, nullptr
				, "PSMain", "ps_5_0", compileFlags, 0, &pIPSSphere, nullptr));

			// 我们多添加了一个法线的定义，但目前Shader中我们并没有使用
			D3D12_INPUT_ELEMENT_DESC stIALayoutSphere[] =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
			};

			// 创建 graphics pipeline state object (PSO)对象
			D3D12_GRAPHICS_PIPELINE_STATE_DESC stPSODesc = {};
			stPSODesc.InputLayout = { stIALayoutSphere, _countof(stIALayoutSphere) };
			stPSODesc.pRootSignature = pIRootSignature.Get();
			stPSODesc.VS.BytecodeLength = pIVSSphere->GetBufferSize();
			stPSODesc.VS.pShaderBytecode = pIVSSphere->GetBufferPointer();
			stPSODesc.PS.BytecodeLength = pIPSSphere->GetBufferSize();
			stPSODesc.PS.pShaderBytecode = pIPSSphere->GetBufferPointer();

			stPSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			stPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

			stPSODesc.BlendState.AlphaToCoverageEnable = FALSE;
			stPSODesc.BlendState.IndependentBlendEnable = FALSE;
			stPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

			stPSODesc.SampleMask = UINT_MAX;
			stPSODesc.SampleDesc.Count = 1;
			stPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			stPSODesc.NumRenderTargets = 1;
			stPSODesc.RTVFormats[0] = emRTFormat;
			stPSODesc.DSVFormat = emDSFormat;
			stPSODesc.DepthStencilState.StencilEnable = FALSE;
			stPSODesc.DepthStencilState.DepthEnable = TRUE;			//打开深度缓冲				
			stPSODesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;//启用深度缓存写入功能
			stPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;     //深度测试函数（该值为普通的深度测试，即较小值写入）

			GRS_THROW_IF_FAILED(pID3D12Device4->CreateGraphicsPipelineState(&stPSODesc
				, IID_PPV_ARGS(&pIPSOSphere)));

			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIPSOSphere);
		}

		//11、准备参数并启动多个渲染线程
		{
			USES_CONVERSION;
			// 球体个性参数
			StringCchPrintf(g_stThreadParams[g_nThdSphere].pszDDSFile, MAX_PATH, _T("%sAssets\\sphere.dds"), g_pszAppPath);
			StringCchPrintfA(g_stThreadParams[g_nThdSphere].pszMeshFile, MAX_PATH, "%sAssets\\sphere.txt", T2A(g_pszAppPath));
			g_stThreadParams[g_nThdSphere].v4ModelPos = XMFLOAT4(2.0f, 2.0f, 0.0f, 1.0f);

			// 立方体个性参数
			StringCchPrintf(g_stThreadParams[g_nThdCube].pszDDSFile, MAX_PATH, _T("%sAssets\\Cube.dds"), g_pszAppPath);
			StringCchPrintfA(g_stThreadParams[g_nThdCube].pszMeshFile, MAX_PATH, "%sAssets\\Cube.txt", T2A(g_pszAppPath));
			g_stThreadParams[g_nThdCube].v4ModelPos = XMFLOAT4(-2.0f, 2.0f, 0.0f, 1.0f);

			// 平板个性参数
			StringCchPrintf(g_stThreadParams[g_nThdPlane].pszDDSFile, MAX_PATH, _T("%sAssets\\Plane.dds"), g_pszAppPath);
			StringCchPrintfA(g_stThreadParams[g_nThdPlane].pszMeshFile, MAX_PATH, "%sAssets\\Plane.txt", T2A(g_pszAppPath));
			g_stThreadParams[g_nThdPlane].v4ModelPos = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);


			// 物体的共性参数，也就是各线程的共性参数
			for (int i = 0; i < g_nMaxThread; i++)
			{
				g_stThreadParams[i].nIndex = i;		//记录序号

				g_stThreadParams[i].m_nPassNO = 0;

				//创建每个线程需要的命令列表和复制命令队列
				GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT
					, IID_PPV_ARGS(&g_stThreadParams[i].pICmdAlloc)));
				GRS_SetD3D12DebugNameIndexed(g_stThreadParams[i].pICmdAlloc, _T("pIThreadCmdAlloc"), i);

				GRS_THROW_IF_FAILED(pID3D12Device4->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT
					, g_stThreadParams[i].pICmdAlloc, nullptr, IID_PPV_ARGS(&g_stThreadParams[i].pICmdList)));
				GRS_SetD3D12DebugNameIndexed(g_stThreadParams[i].pICmdList, _T("pIThreadCmdList"), i);

				g_stThreadParams[i].dwMainThreadID = ::GetCurrentThreadId();
				g_stThreadParams[i].hMainThread = ::GetCurrentThread();
				g_stThreadParams[i].hRunEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
				//g_stThreadParams[i].hEventRenderOver = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
				g_stThreadParams[i].pID3D12Device4 = pID3D12Device4.Get();
				g_stThreadParams[i].pIRS = pIRootSignature.Get();
				g_stThreadParams[i].pIPSO = pIPSOSphere.Get();

				//arHWaited.Add(g_stThreadParams[i].hEventRenderOver); //添加到被等待队列里

				//以暂停方式创建线程
				g_stThreadParams[i].hThisThread = (HANDLE)_beginthreadex(nullptr,
					0, RenderThread, (void*)&g_stThreadParams[i],
					CREATE_SUSPENDED, (UINT*)&g_stThreadParams[i].dwThisThreadID);

				//然后判断线程创建是否成功
				if (nullptr == g_stThreadParams[i].hThisThread
					|| reinterpret_cast<HANDLE>(-1) == g_stThreadParams[i].hThisThread)
				{
					throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
				}

				arHSubThread.Add(g_stThreadParams[i].hThisThread);
			}

			//逐一启动线程
			for (int i = 0; i < g_nMaxThread; i++)
			{
				::ResumeThread(g_stThreadParams[i].hThisThread);
			}
		}

		//12、创建围栏对象
		{
			GRS_THROW_IF_FAILED(pID3D12Device4->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pIFence)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIFence);

			//创建一个Event同步对象，用于等待围栏事件通知
			hEventFence = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (hEventFence == nullptr)
			{
				GRS_THROW_IF_FAILED(HRESULT_FROM_WIN32(GetLastError()));
			}
		}

		arHWaited.RemoveAll();
		arHWaited.Add(g_hEventPassOver);

		D3D12_RESOURCE_BARRIER stBeginResBarrier = {};
		D3D12_RESOURCE_BARRIER stEndResBarrier = {};
		{
			stBeginResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stBeginResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stBeginResBarrier.Transition.pResource = g_pIARenderTargets[nCurrentFrameIndex].Get();
			stBeginResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			stBeginResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			stBeginResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;


			stEndResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stEndResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stEndResBarrier.Transition.pResource = g_pIARenderTargets[nCurrentFrameIndex].Get();
			stEndResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			stEndResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			stEndResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		}

		//UINT nStates = 0; //初始状态为0
		DWORD dwRet = 0;
		DWORD dwWaitCnt = 0;
		CAtlArray<ID3D12CommandList*> arCmdList;
		UINT64 n64fence = 0;

		BOOL bExit = FALSE;

		ULONGLONG n64tmFrameStart = ::GetTickCount64();
		ULONGLONG n64tmCurrent = n64tmFrameStart;
		//计算旋转角度需要的变量
		double dModelRotationYAngle = 0.0f;
		WCHAR str[MAX_PATH] = {};
		//起始的时候关闭一下两个命令列表，因为我们在开始渲染的时候都需要先reset它们，为了防止报错故先Close
		GRS_THROW_IF_FAILED(pICmdListPre->Close());
		GRS_THROW_IF_FAILED(pICmdListPost->Close());

		//SetTimer(hWnd, WM_USER + 100, 1, nullptr); //这句为了保证MsgWaitForMultipleObjects 在 wait for all 为True时 能够及时返回

		ShowWindow(hWnd, nCmdShow);
		UpdateWindow(hWnd);

		UINT					nMaxPass = 2; //假设一帧渲染需要至少5遍渲染（5 Pass），可以根据你的渲染手法（technology）来动态调整
		UINT					nPassNO = 0;    //当前渲染Pass的序号

		GRS_USEPRINTF();
		//13、开始消息循环，并在其中不断渲染
		static float timeElapsed = 0.0f;
		static int frameCnt = 0;
		GameTimer gTimer;
		gTimer.Reset();
		while (!bExit)
		{
			dwWaitCnt = static_cast<DWORD>(arHWaited.GetCount());
			dwRet = ::MsgWaitForMultipleObjects( dwWaitCnt ,arHWaited.GetData(), FALSE,INFINITE, QS_ALLINPUT);
			dwRet -= WAIT_OBJECT_0;

			switch (dwRet)
			{
			case 0:
			{
				if (nPassNO == 0)
				{
					arCmdList.RemoveAll();
					arCmdList.Add(g_stThreadParams[g_nThdSphere].pICmdList);
					arCmdList.Add(g_stThreadParams[g_nThdCube].pICmdList);
					arCmdList.Add(g_stThreadParams[g_nThdPlane].pICmdList);
					pIMainCmdQueue->ExecuteCommandLists(static_cast<UINT>(arCmdList.GetCount()), arCmdList.GetData());
					//开始同步GPU与CPU的执行，先记录围栏标记值
					n64fence = n64FenceValue;
					GRS_THROW_IF_FAILED(pIMainCmdQueue->Signal(pIFence.Get(), n64fence));
					n64FenceValue++;
					GRS_THROW_IF_FAILED(pIFence->SetEventOnCompletion(n64fence, hEventFence));
					arHWaited.RemoveAll();
					arHWaited.Add(hEventFence);
					//ReleaseSemaphore(g_hSemaphore, g_nMaxThread, nullptr);
					++nPassNO; //递增到下一遍渲染
				}
				else if (nPassNO < nMaxPass)
				{
					nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();
					

					//通知各线程开始渲染
					for (int i = 0; i < g_nMaxThread; i++)
					{
						//设定子线程渲染Pass序号
						g_stThreadParams[i].m_nPassNO = nPassNO;
						g_stThreadParams[i].nCurrentFrameIndex = nCurrentFrameIndex;
						g_stThreadParams[i].nStartTime = n64tmFrameStart;
						g_stThreadParams[i].nCurrentTime = n64tmCurrent;
					}
					{
						GRS_THROW_IF_FAILED(pICmdAllocPre->Reset());
						GRS_THROW_IF_FAILED(pICmdListPre->Reset(pICmdAllocPre.Get(), pIPSOSphere.Get()));

						GRS_THROW_IF_FAILED(pICmdAllocPost->Reset());
						GRS_THROW_IF_FAILED(pICmdListPost->Reset(pICmdAllocPost.Get(), pIPSOSphere.Get()));

						nCurrentFrameIndex = pISwapChain3->GetCurrentBackBufferIndex();
						//计算全局的Matrix
						{
							//关于时间的基本运算都放在了主线程中
							//真实的引擎或程序中建议时间值也作为一个每帧更新的参数从主线程获取并传给各子线程
							n64tmCurrent = ::GetTickCount();
							//计算旋转的角度：旋转角度(弧度) = 时间(秒) * 角速度(弧度/秒)
							//下面这句代码相当于经典游戏消息循环中的OnUpdate函数中需要做的事情
							dModelRotationYAngle += ((n64tmCurrent - n64tmFrameStart) / 1000.0f) * g_fPalstance;

							//旋转角度是2PI周期的倍数，去掉周期数，只留下相对0弧度开始的小于2PI的弧度即可
							if (dModelRotationYAngle > XM_2PI)
							{
								dModelRotationYAngle = fmod(dModelRotationYAngle, XM_2PI);
							}

							//计算 World 矩阵 这里是个旋转矩阵
							XMStoreFloat4x4(&g_mxWorld, XMMatrixRotationY(static_cast<float>(dModelRotationYAngle)));
							//计算 视矩阵 view * 裁剪矩阵 projection
							XMStoreFloat4x4(&g_mxVP
								, XMMatrixMultiply(XMMatrixLookAtLH(XMLoadFloat3(&g_f3EyePos)
									, XMLoadFloat3(&g_f3LockAt)
									, XMLoadFloat3(&g_f3HeapUp))
									, XMMatrixPerspectiveFovLH(XM_PIDIV4
										, (FLOAT)g_iWndWidth / (FLOAT)g_iWndHeight, 0.1f, 1000.0f)));
						}

						//渲染前处理
						{
							stBeginResBarrier.Transition.pResource = g_pIARenderTargets[nCurrentFrameIndex].Get();
							pICmdListPre->ResourceBarrier(1, &stBeginResBarrier);

							//偏移描述符指针到指定帧缓冲视图位置
							D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = g_pIRTVHeap->GetCPUDescriptorHandleForHeapStart();
							stRTVHandle.ptr += (nCurrentFrameIndex * g_nRTVDescriptorSize);
							D3D12_CPU_DESCRIPTOR_HANDLE stDSVHandle = g_pIDSVHeap->GetCPUDescriptorHandleForHeapStart();
							//设置渲染目标
							pICmdListPre->OMSetRenderTargets(1, &stRTVHandle, FALSE, &stDSVHandle);

							pICmdListPre->RSSetViewports(1, &g_stViewPort);
							pICmdListPre->RSSetScissorRects(1, &g_stScissorRect);

							pICmdListPre->ClearRenderTargetView(stRTVHandle, faClearColor, 0, nullptr);
							pICmdListPre->ClearDepthStencilView(stDSVHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
						}
					}
					++nPassNO; //递增到下一遍渲染
					// 释放信标量至最大子线程数，相当于通知所有子线程开始渲染运行
					ReleaseSemaphore(g_hSemaphore, g_nMaxThread, nullptr);
					arHWaited.RemoveAll();
					arHWaited.Add(g_hEventPassOver);
					gTimer.Tick();
					frameCnt++;
					// Compute averages over one second period.
					if ((gTimer.TotalTime() - timeElapsed) >= 1.0f)
					{
						float mspf = 1000.0f / frameCnt;
						GRS_PRINTF(_T("fps:%d , mspf:%.2f\n"), frameCnt, mspf);
						frameCnt = 0;
						timeElapsed += 1.0f;
					}
				}
				else
				{
					stEndResBarrier.Transition.pResource = g_pIARenderTargets[nCurrentFrameIndex].Get();
					pICmdListPost->ResourceBarrier(1, &stEndResBarrier);

					//关闭命令列表，可以去执行了
					GRS_THROW_IF_FAILED(pICmdListPre->Close());
					GRS_THROW_IF_FAILED(pICmdListPost->Close());

					arCmdList.RemoveAll();
					//执行命令列表（注意命令列表排队组合的方式）
					arCmdList.Add(pICmdListPre.Get());
					arCmdList.Add(g_stThreadParams[g_nThdSphere].pICmdList);
					arCmdList.Add(g_stThreadParams[g_nThdCube].pICmdList);
					arCmdList.Add(g_stThreadParams[g_nThdPlane].pICmdList);
					arCmdList.Add(pICmdListPost.Get());

					pIMainCmdQueue->ExecuteCommandLists(static_cast<UINT>(arCmdList.GetCount()), arCmdList.GetData());

					//提交画面
					GRS_THROW_IF_FAILED(pISwapChain3->Present(1, 0));

					//开始同步GPU与CPU的执行，先记录围栏标记值
					n64fence = n64FenceValue;
					GRS_THROW_IF_FAILED(pIMainCmdQueue->Signal(pIFence.Get(), n64fence));
					n64FenceValue++;
					GRS_THROW_IF_FAILED(pIFence->SetEventOnCompletion(n64fence, hEventFence));
					arHWaited.RemoveAll();
					arHWaited.Add(hEventFence);
					//nStates = 1;
					nPassNO = 1;
					//更新下帧开始时间为上一帧开始时间
					n64tmFrameStart = n64tmCurrent;
				}
			}
			break;
			case 1:
			{
				//处理消息
				while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_NOYIELD))
				{
					if (WM_QUIT != msg.message)
					{
						::TranslateMessage(&msg);
						::DispatchMessage(&msg);
					}
					else
					{
						bExit = TRUE;
					}
				}
			}
			break;
			default:
			{

			}
			break;
			}

			dwRet = WaitForMultipleObjects(static_cast<DWORD>(arHSubThread.GetCount()), arHSubThread.GetData(), FALSE, 0);
			dwRet -= WAIT_OBJECT_0;
			if (dwRet >= 0 && dwRet < g_nMaxThread)
			{
				bExit = TRUE;
			}

		}

		//KillTimer(hWnd, WM_USER + 100);
	}
	catch (CGRSCOMException& e)
	{//发生了COM异常
		e;
	}

	try
	{
		// 通知子线程退出
		for (int i = 0; i < g_nMaxThread; i++)
		{
			::PostThreadMessage(g_stThreadParams[i].dwThisThreadID, WM_QUIT, 0, 0);
		}

		// 等待所有子线程退出
		DWORD dwRet = WaitForMultipleObjects(static_cast<DWORD>(arHSubThread.GetCount()), arHSubThread.GetData(), TRUE, INFINITE);

		// 清理所有子线程资源
		for (int i = 0; i < g_nMaxThread; i++)
		{
			::CloseHandle(g_stThreadParams[i].hThisThread);
			//::CloseHandle(g_stThreadParams[i].hEventRenderOver);
			g_stThreadParams[i].pICmdList->Release();
			g_stThreadParams[i].pICmdAlloc->Release();
		}

		//::CoUninitialize();
	}
	catch (CGRSCOMException& e)
	{//发生了COM异常
		e;
	}
	::CoUninitialize();
	return 0;
}

UINT __stdcall RenderThread(void* pParam)
{
	ST_GRS_THREAD_PARAMS* pThdPms = static_cast<ST_GRS_THREAD_PARAMS*>(pParam);
	try
	{
		if (nullptr == pThdPms)
		{//参数异常，抛异常终止线程
			throw CGRSCOMException(E_INVALIDARG);
		}

		SIZE_T								szMVPBuf = GRS_UPPER(sizeof(ST_GRS_MVP), 256);

		ComPtr<ID3D12Resource>				pITexture;
		ComPtr<ID3D12Resource>				pITextureUpload;
		ComPtr<ID3D12Resource>				pIVB;
		ComPtr<ID3D12Resource>				pIIB;
		ComPtr<ID3D12Resource>			    pICBWVP;
		ComPtr<ID3D12DescriptorHeap>		pISRVCBVHp;
		ComPtr<ID3D12DescriptorHeap>		pISampleHp;

		ST_GRS_MVP*							pMVPBufModule = nullptr;
		D3D12_VERTEX_BUFFER_VIEW			stVBV = {};
		D3D12_INDEX_BUFFER_VIEW				stIBV = {};
		UINT								nIndexCnt = 0;
		XMMATRIX							mxPosModule = XMMatrixTranslationFromVector(XMLoadFloat4(&pThdPms->v4ModelPos));  //当前渲染物体的位置
		// Mesh Value
		ST_GRS_VERTEX*						pstVertices = nullptr;
		UINT*								pnIndices = nullptr;
		UINT								nVertexCnt = 0;
		// DDS Value
		std::unique_ptr<uint8_t[]>			pbDDSData;
		std::vector<D3D12_SUBRESOURCE_DATA> stArSubResources;
		DDS_ALPHA_MODE						emAlphaMode = DDS_ALPHA_MODE_UNKNOWN;
		bool								bIsCube = false;

		//1、加载DDS纹理
		{
			GRS_THROW_IF_FAILED(LoadDDSTextureFromFile(pThdPms->pID3D12Device4, pThdPms->pszDDSFile, pITexture.GetAddressOf()
				, pbDDSData, stArSubResources, SIZE_MAX, &emAlphaMode, &bIsCube));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pITexture);

			D3D12_RESOURCE_DESC stTXDesc = pITexture->GetDesc();
			UINT64 n64szUpSphere = 0;
			pThdPms->pID3D12Device4->GetCopyableFootprints(&stTXDesc, 0, 1, 0, nullptr, nullptr, nullptr, &n64szUpSphere);
			
			D3D12_HEAP_PROPERTIES stHeapProp = {};
			stHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
			stHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stHeapProp.CreationNodeMask = 1;
			stHeapProp.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC stUploadBufDesc = {};
			stUploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			stUploadBufDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stUploadBufDesc.Width = n64szUpSphere;
			stUploadBufDesc.Height = 1;
			stUploadBufDesc.DepthOrArraySize = 1;
			stUploadBufDesc.MipLevels = 1;
			stUploadBufDesc.Format = DXGI_FORMAT_UNKNOWN;
			stUploadBufDesc.SampleDesc.Count = 1;
			stUploadBufDesc.SampleDesc.Quality = 0;
			stUploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			stUploadBufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			GRS_THROW_IF_FAILED(pThdPms->pID3D12Device4->CreateCommittedResource(
				&stHeapProp
				, D3D12_HEAP_FLAG_NONE
				, &stUploadBufDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pITextureUpload)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pITextureUpload);


			//UpdateSubresources(pThdPms->pICmdList
			//	, pITexture.Get()
			//	, pITextureUpload.Get()
			//	, 0
			//	, 0
			//	, static_cast<UINT>(stArSubResources.size())
			//	, stArSubResources.data());

			UINT nFirstSubresource = 0;
			UINT nNumSubresources = static_cast<UINT>(stArSubResources.size());
			D3D12_RESOURCE_DESC stUploadResDesc = pITextureUpload->GetDesc();
			D3D12_RESOURCE_DESC stDefaultResDesc = pITexture->GetDesc();

			UINT64 n64RequiredSize = 0;
			SIZE_T szMemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT)
				+ sizeof(UINT)
				+ sizeof(UINT64))
				* nNumSubresources;

			void* pMem = GRS_CALLOC(static_cast<SIZE_T>(szMemToAlloc));

			if (nullptr == pMem)
			{
				throw CGRSCOMException(HRESULT_FROM_WIN32(GetLastError()));
			}

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
			UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + nNumSubresources);
			UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + nNumSubresources);

			// 这里是第二次调用GetCopyableFootprints，就得到了所有子资源的详细信息
			pThdPms->pID3D12Device4->GetCopyableFootprints(&stDefaultResDesc, nFirstSubresource, nNumSubresources, 0, pLayouts, pNumRows, pRowSizesInBytes, &n64RequiredSize);

			BYTE* pData = nullptr;
			HRESULT hr = pITextureUpload->Map(0, nullptr, reinterpret_cast<void**>(&pData));
			if (FAILED(hr))
			{
				return 0;
			}

			// 第一遍Copy！注意3重循环每重的意思
			for (UINT i = 0; i < nNumSubresources; ++i)
			{// SubResources
				if (pRowSizesInBytes[i] > (SIZE_T)-1)
				{
					throw CGRSCOMException(E_FAIL);
				}

				D3D12_MEMCPY_DEST stCopyDestData = { pData + pLayouts[i].Offset
					, pLayouts[i].Footprint.RowPitch
					, pLayouts[i].Footprint.RowPitch * pNumRows[i]
				};

				for (UINT z = 0; z < pLayouts[i].Footprint.Depth; ++z)
				{// Mipmap
					BYTE* pDestSlice = reinterpret_cast<BYTE*>(stCopyDestData.pData) + stCopyDestData.SlicePitch * z;
					const BYTE* pSrcSlice = reinterpret_cast<const BYTE*>(stArSubResources[i].pData) + stArSubResources[i].SlicePitch * z;
					for (UINT y = 0; y < pNumRows[i]; ++y)
					{// Rows
						memcpy(pDestSlice + stCopyDestData.RowPitch * y,
							pSrcSlice + stArSubResources[i].RowPitch * y,
							(SIZE_T)pRowSizesInBytes[i]);
					}
				}
			}
			pITextureUpload->Unmap(0, nullptr);

			// 第二次Copy！
			if (stDefaultResDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				// Buffer 一次性复制就可以了，因为没有行对齐和行大小不一致的问题，Buffer中行数就是1
				pThdPms->pICmdList->CopyBufferRegion(
					pITexture.Get(), 0, pITextureUpload.Get(), pLayouts[0].Offset, pLayouts[0].Footprint.Width);
			}
			else
			{
				for (UINT i = 0; i < nNumSubresources; ++i)
				{
					D3D12_TEXTURE_COPY_LOCATION stDstCopyLocation = {};
					stDstCopyLocation.pResource = pITexture.Get();
					stDstCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
					stDstCopyLocation.SubresourceIndex = i;

					D3D12_TEXTURE_COPY_LOCATION stSrcCopyLocation = {};
					stSrcCopyLocation.pResource = pITextureUpload.Get();
					stSrcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
					stSrcCopyLocation.PlacedFootprint = pLayouts[i];

					pThdPms->pICmdList->CopyTextureRegion(&stDstCopyLocation, 0, 0, 0, &stSrcCopyLocation, nullptr);
				}
			}

			D3D12_RESOURCE_BARRIER stTransResBarrier = {};

			stTransResBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			stTransResBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			stTransResBarrier.Transition.pResource = pITexture.Get();
			stTransResBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			stTransResBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			stTransResBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			//同步
			pThdPms->pICmdList->ResourceBarrier(1, &stTransResBarrier);
			// 
			// 创建描述符堆
			D3D12_DESCRIPTOR_HEAP_DESC stSRVCBVHPDesc = {};
			stSRVCBVHPDesc.NumDescriptors = 2; // 1 CBV + 1 SRV
			stSRVCBVHPDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			stSRVCBVHPDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			GRS_THROW_IF_FAILED(pThdPms->pID3D12Device4->CreateDescriptorHeap(&stSRVCBVHPDesc, IID_PPV_ARGS(&pISRVCBVHp)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pISRVCBVHp);

			//创建SRV
			D3D12_SHADER_RESOURCE_VIEW_DESC stSRVDesc = {};
			stSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			stSRVDesc.Format = stTXDesc.Format;
			stSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			stSRVDesc.Texture2D.MipLevels = 1;

			D3D12_CPU_DESCRIPTOR_HANDLE stCbvSrvHandle = pISRVCBVHp->GetCPUDescriptorHandleForHeapStart();
			stCbvSrvHandle.ptr += pThdPms->pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			pThdPms->pID3D12Device4->CreateShaderResourceView(pITexture.Get()
				, &stSRVDesc
				, stCbvSrvHandle);
		}

		{}
		//2、创建Sample
		{
			D3D12_DESCRIPTOR_HEAP_DESC stSamplerHeapDesc = {};
			stSamplerHeapDesc.NumDescriptors = 1;
			stSamplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			stSamplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			GRS_THROW_IF_FAILED(pThdPms->pID3D12Device4->CreateDescriptorHeap(&stSamplerHeapDesc, IID_PPV_ARGS(&pISampleHp)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pISampleHp);

			D3D12_SAMPLER_DESC stSamplerDesc = {};
			stSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			stSamplerDesc.MinLOD = 0;
			stSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
			stSamplerDesc.MipLODBias = 0.0f;
			stSamplerDesc.MaxAnisotropy = 1;
			stSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			stSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			stSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

			pThdPms->pID3D12Device4->CreateSampler(&stSamplerDesc, pISampleHp->GetCPUDescriptorHandleForHeapStart());
		}

		//3、加载网格数据
		{
			LoadMeshVertex(pThdPms->pszMeshFile, nVertexCnt, pstVertices, pnIndices);
			nIndexCnt = nVertexCnt;

			D3D12_HEAP_PROPERTIES stHeapProp = {};
			stHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
			stHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stHeapProp.CreationNodeMask = 1;
			stHeapProp.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC stUploadBufDesc = {};
			stUploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			stUploadBufDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stUploadBufDesc.Width = nVertexCnt * sizeof(ST_GRS_VERTEX);
			stUploadBufDesc.Height = 1;
			stUploadBufDesc.DepthOrArraySize = 1;
			stUploadBufDesc.MipLevels = 1;
			stUploadBufDesc.Format = DXGI_FORMAT_UNKNOWN;
			stUploadBufDesc.SampleDesc.Count = 1;
			stUploadBufDesc.SampleDesc.Quality = 0;
			stUploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			stUploadBufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			//创建 Vertex Buffer 仅使用Upload隐式堆
			GRS_THROW_IF_FAILED(pThdPms->pID3D12Device4->CreateCommittedResource(
				&stHeapProp
				, D3D12_HEAP_FLAG_NONE
				, &stUploadBufDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pIVB)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIVB);

			//使用map-memcpy-unmap大法将数据传至顶点缓冲对象
			UINT8* pVertexDataBegin = nullptr;
			D3D12_RANGE stReadRange = { 0, 0 };		// We do not intend to read from this resource on the CPU.

			GRS_THROW_IF_FAILED(pIVB->Map(0, &stReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));
			memcpy(pVertexDataBegin, pstVertices, nVertexCnt * sizeof(ST_GRS_VERTEX));
			pIVB->Unmap(0, nullptr);


			//创建 Index Buffer 仅使用Upload隐式堆
			stUploadBufDesc.Width = nIndexCnt * sizeof(UINT);
			GRS_THROW_IF_FAILED(pThdPms->pID3D12Device4->CreateCommittedResource(
				&stHeapProp
				, D3D12_HEAP_FLAG_NONE
				, &stUploadBufDesc
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pIIB)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pIIB);

			UINT8* pIndexDataBegin = nullptr;
			GRS_THROW_IF_FAILED(pIIB->Map(0, &stReadRange, reinterpret_cast<void**>(&pIndexDataBegin)));
			memcpy(pIndexDataBegin, pnIndices, nIndexCnt * sizeof(UINT));
			pIIB->Unmap(0, nullptr);

			//创建Vertex Buffer View
			stVBV.BufferLocation = pIVB->GetGPUVirtualAddress();
			stVBV.StrideInBytes = sizeof(ST_GRS_VERTEX);
			stVBV.SizeInBytes = nVertexCnt * sizeof(ST_GRS_VERTEX);

			//创建Index Buffer View
			stIBV.BufferLocation = pIIB->GetGPUVirtualAddress();
			stIBV.Format = DXGI_FORMAT_R32_UINT;
			stIBV.SizeInBytes = nIndexCnt * sizeof(UINT);

			GRS_SAFE_FREE( pstVertices);
			GRS_SAFE_FREE( pnIndices);
		}

		//4、创建常量缓冲 
		{
			D3D12_HEAP_PROPERTIES stHeapProp = {};
			stHeapProp.Type = D3D12_HEAP_TYPE_UPLOAD;
			stHeapProp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			stHeapProp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			stHeapProp.CreationNodeMask = 1;
			stHeapProp.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC stUploadBufDesc = {};
			stUploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			stUploadBufDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
			stUploadBufDesc.Width = szMVPBuf;
			stUploadBufDesc.Height = 1;
			stUploadBufDesc.DepthOrArraySize = 1;
			stUploadBufDesc.MipLevels = 1;
			stUploadBufDesc.Format = DXGI_FORMAT_UNKNOWN;
			stUploadBufDesc.SampleDesc.Count = 1;
			stUploadBufDesc.SampleDesc.Quality = 0;
			stUploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			stUploadBufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			GRS_THROW_IF_FAILED(pThdPms->pID3D12Device4->CreateCommittedResource(
				&stHeapProp
				, D3D12_HEAP_FLAG_NONE
				, &stUploadBufDesc //注意缓冲尺寸设置为256边界对齐大小
				, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr
				, IID_PPV_ARGS(&pICBWVP)));
			GRS_SET_D3D12_DEBUGNAME_COMPTR(pICBWVP);

			// Map 之后就不再Unmap了 直接复制数据进去 这样每帧都不用map-copy-unmap浪费时间了
			GRS_THROW_IF_FAILED(pICBWVP->Map(0, nullptr, reinterpret_cast<void**>(&pMVPBufModule)));

			// 创建CBV
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = pICBWVP->GetGPUVirtualAddress();
			cbvDesc.SizeInBytes = static_cast<UINT>(szMVPBuf);

			D3D12_CPU_DESCRIPTOR_HANDLE stCbvSrvHandle = pISRVCBVHp->GetCPUDescriptorHandleForHeapStart();
			pThdPms->pID3D12Device4->CreateConstantBufferView(&cbvDesc, stCbvSrvHandle);
		}
		GRS_USEPRINTF();
		TCHAR pszPreSpace[] = _T("  ");
		GRS_PRINTF(_T("%s子线程【%d】开始进入渲染循环\n"), pszPreSpace, pThdPms->nIndex);

		//5、设置事件对象 通知并切回主线程 完成资源的第二个Copy命令
		{
			GRS_THROW_IF_FAILED(pThdPms->pICmdList->Close());
			//第一次通知主线程本线程加载资源完毕
			//::SetEvent(pThdPms->hEventRenderOver); // 设置信号，通知主线程本线程资源加载完毕
			if (EnterSynchronizationBarrier(&g_stCPUThdBarrier, 0))
			{
				//GRS_PRINTF(_T("%s子线程【%d】：通知主线程完成第【%d】遍渲染\n"), pszPreSpace, pThdPms->nIndex, pThdPms->m_nPassNO);

				SetEvent(g_hEventPassOver);
			}
		}

		// 仅用于球体跳动物理特效的变量
		float fJmpSpeed = 0.003f; //跳动速度
		float fUp = 1.0f;
		float fRawYPos = pThdPms->v4ModelPos.y;

		DWORD dwRet = 0;
		BOOL  bQuit = FALSE;
		MSG   msg = {};



		//6、渲染循环
		while (!bQuit)
		{
			dwRet = ::MsgWaitForMultipleObjects(1, &g_hSemaphore, FALSE, INFINITE, QS_ALLPOSTMESSAGE);
			
			//WCHAR str[MAX_PATH] = {};
			//StringCchPrintfW(str, MAX_PATH, _T("Current:%d \n"), dwRet);
			//OutputDebugString(str);
			switch (dwRet - WAIT_OBJECT_0)
			{
			case 0:
			{
				//GRS_PRINTF(_T("%s子线程【%d】执行第【%d】遍渲染：OnThreadRender()\n"), pszPreSpace, pThdPms->nIndex, pThdPms->m_nPassNO);

				//命令分配器先Reset一下，刚才已经执行过了一个复制纹理的命令
				GRS_THROW_IF_FAILED(pThdPms->pICmdAlloc->Reset());
				//Reset命令列表，并重新指定命令分配器和PSO对象
				GRS_THROW_IF_FAILED(pThdPms->pICmdList->Reset(pThdPms->pICmdAlloc, pThdPms->pIPSO));

				// 准备MWVP矩阵
				{
					//==============================================================================
					//使用主线程更新的统一帧时间更新下物体的物理状态等，这里仅演示球体上下跳动的样子
					//启发式的向大家说明利用多线程渲染时，物理变换可以在不同的线程中，并且在不同的CPU上并行的执行
					if ( g_nThdSphere == pThdPms->nIndex )
					{
						if ( pThdPms->v4ModelPos.y >= 2.0f * fRawYPos )
						{
							fUp = -1.0f;
							pThdPms->v4ModelPos.y = 2.0f * fRawYPos;
						}

						if ( pThdPms->v4ModelPos.y <= fRawYPos )
						{
							fUp = 1.0f;
							pThdPms->v4ModelPos.y = fRawYPos;
						}

						pThdPms->v4ModelPos.y += fUp * fJmpSpeed * static_cast<float>(pThdPms->nCurrentTime - pThdPms->nStartTime);

						mxPosModule = XMMatrixTranslationFromVector(XMLoadFloat4(&pThdPms->v4ModelPos));
					}
					//==============================================================================

					// Module * World
					XMMATRIX xmMWVP = XMMatrixMultiply(mxPosModule, XMLoadFloat4x4(&g_mxWorld));

					// (Module * World) * View * Projection
					xmMWVP = XMMatrixMultiply(xmMWVP, XMLoadFloat4x4(&g_mxVP));

					XMStoreFloat4x4(&pMVPBufModule->m_MVP, xmMWVP);
				}

				//---------------------------------------------------------------------------------------------
				//设置对应的渲染目标和视裁剪框(这是渲染子线程必须要做的步骤，基本也就是所谓多线程渲染的核心秘密所在了)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE stRTVHandle = g_pIRTVHeap->GetCPUDescriptorHandleForHeapStart();
					stRTVHandle.ptr += ( pThdPms->nCurrentFrameIndex * g_nRTVDescriptorSize);
					D3D12_CPU_DESCRIPTOR_HANDLE stDSVHandle = g_pIDSVHeap->GetCPUDescriptorHandleForHeapStart();
					//设置渲染目标
					pThdPms->pICmdList->OMSetRenderTargets(1, &stRTVHandle, FALSE, &stDSVHandle);
					pThdPms->pICmdList->RSSetViewports(1, &g_stViewPort);
					pThdPms->pICmdList->RSSetScissorRects(1, &g_stScissorRect);
				}
				//---------------------------------------------------------------------------------------------

				//渲染（实质就是记录渲染命令列表）
				{
					pThdPms->pICmdList->SetGraphicsRootSignature(pThdPms->pIRS);
					pThdPms->pICmdList->SetPipelineState(pThdPms->pIPSO);
					ID3D12DescriptorHeap* ppHeapsSphere[] = { pISRVCBVHp.Get(),pISampleHp.Get() };
					pThdPms->pICmdList->SetDescriptorHeaps(_countof(ppHeapsSphere), ppHeapsSphere);

					D3D12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSphere = pISRVCBVHp->GetGPUDescriptorHandleForHeapStart();
					//设置CBV
					pThdPms->pICmdList->SetGraphicsRootDescriptorTable(0, stGPUCBVHandleSphere);
					stGPUCBVHandleSphere.ptr += pThdPms->pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					//设置SRV
					pThdPms->pICmdList->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleSphere);

					//设置Sample
					pThdPms->pICmdList->SetGraphicsRootDescriptorTable(2, pISampleHp->GetGPUDescriptorHandleForHeapStart());

					//注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
					pThdPms->pICmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					pThdPms->pICmdList->IASetVertexBuffers(0, 1, &stVBV);
					pThdPms->pICmdList->IASetIndexBuffer(&stIBV);

					//Draw Call！！！
					pThdPms->pICmdList->DrawIndexedInstanced(nIndexCnt, 1, 0, 0, 0);
					GRS_THROW_IF_FAILED(pThdPms->pICmdList->Close());
				}

				if (EnterSynchronizationBarrier(&g_stCPUThdBarrier, 0))
				{
					//GRS_PRINTF(_T("%s子线程【%d】：通知主线程完成第【%d】遍渲染\n"), pszPreSpace, pThdPms->nIndex, pThdPms->m_nPassNO);

					SetEvent(g_hEventPassOver);
				}
			}
			break;
			case 1:
			{//处理消息
				while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
				{//这里只可能是别的线程发过来的消息，用于更复杂的场景
					if (WM_QUIT != msg.message)
					{
						::TranslateMessage(&msg);
						::DispatchMessage(&msg);
					}
					else
					{
						bQuit = TRUE;
					}
				}
			}
			break;
			case WAIT_TIMEOUT:
				break;
			default:
				break;
			}
		}
	}
	catch (CGRSCOMException&)
	{

	}

	return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
	{
		USHORT n16KeyCode = (wParam & 0xFF);
		if (VK_SPACE == n16KeyCode)
		{//按空格键切换不同的采样器看效果，以明白每种采样器具体的含义
			//UINT g_nCurrentSamplerNO = 0; //当前使用的采样器索引
			//UINT g_nSampleMaxCnt = 5;		//创建五个典型的采样器
			//++g_nCurrentSamplerNO;
			//g_nCurrentSamplerNO %= g_nSampleMaxCnt;

			//=================================================================================================
			//重新设置球体的捆绑包
			//pICmdListSphere->Reset(pICmdAllocSphere.Get(), pIPSOSphere.Get());
			//pICmdListSphere->SetGraphicsRootSignature(pIRootSignature.Get());
			//pICmdListSphere->SetPipelineState(pIPSOSphere.Get());
			//ID3D12DescriptorHeap* ppHeapsSphere[] = { pISRVCBVHp.Get(),pISampleHp.Get() };
			//pICmdListSphere->SetDescriptorHeaps(_countof(ppHeapsSphere), ppHeapsSphere);
			////设置SRV
			//pICmdListSphere->SetGraphicsRootDescriptorTable(0, pISRVCBVHp->GetGPUDescriptorHandleForHeapStart());

			//CD3DX12_GPU_DESCRIPTOR_HANDLE stGPUCBVHandleSphere(pISRVCBVHp->GetGPUDescriptorHandleForHeapStart()
			//	, 1
			//	, pID3D12Device4->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
			////设置CBV
			//pICmdListSphere->SetGraphicsRootDescriptorTable(1, stGPUCBVHandleSphere);
			//CD3DX12_GPU_DESCRIPTOR_HANDLE hGPUSamplerSphere(pISampleHp->GetGPUDescriptorHandleForHeapStart()
			//	, g_nCurrentSamplerNO
			//	, nSamplerDescriptorSize);
			////设置Sample
			//pICmdListSphere->SetGraphicsRootDescriptorTable(2, hGPUSamplerSphere);
			////注意我们使用的渲染手法是三角形列表，也就是通常的Mesh网格
			//pICmdListSphere->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			//pICmdListSphere->IASetVertexBuffers(0, 1, &stVBV);
			//pICmdListSphere->IASetIndexBuffer(&stIBV);

			////Draw Call！！！
			//pICmdListSphere->DrawIndexedInstanced(nIndexCnt, 1, 0, 0, 0);
			//pICmdListSphere->Close();
			//=================================================================================================
		}
		if (VK_ADD == n16KeyCode || VK_OEM_PLUS == n16KeyCode)
		{
			//double g_fPalstance = 10.0f * XM_PI / 180.0f;	//物体旋转的角速度，单位：弧度/秒
			g_fPalstance += 10 * XM_PI / 180.0f;
			if (g_fPalstance > XM_PI)
			{
				g_fPalstance = XM_PI;
			}
			//XMMatrixOrthographicOffCenterLH()
		}

		if (VK_SUBTRACT == n16KeyCode || VK_OEM_MINUS == n16KeyCode)
		{
			g_fPalstance -= 10 * XM_PI / 180.0f;
			if (g_fPalstance < 0.0f)
			{
				g_fPalstance = XM_PI / 180.0f;
			}
		}

		//根据用户输入变换
		//XMVECTOR g_f3EyePos = XMVectorSet(0.0f, 5.0f, -10.0f, 0.0f); //眼睛位置
		//XMVECTOR g_f3LockAt = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);  //眼睛所盯的位置
		//XMVECTOR g_f3HeapUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);  //头部正上方位置
		XMFLOAT3 move(0, 0, 0);
		float fMoveSpeed = 2.0f;
		float fTurnSpeed = XM_PIDIV2 * 0.005f;

		if ('w' == n16KeyCode || 'W' == n16KeyCode)
		{
			move.z -= 1.0f;
		}

		if ('s' == n16KeyCode || 'S' == n16KeyCode)
		{
			move.z += 1.0f;
		}

		if ('d' == n16KeyCode || 'D' == n16KeyCode)
		{
			move.x += 1.0f;
		}

		if ('a' == n16KeyCode || 'A' == n16KeyCode)
		{
			move.x -= 1.0f;
		}

		if (fabs(move.x) > 0.1f && fabs(move.z) > 0.1f)
		{
			XMVECTOR vector = XMVector3Normalize(XMLoadFloat3(&move));
			move.x = XMVectorGetX(vector);
			move.z = XMVectorGetZ(vector);
		}

		if (VK_UP == n16KeyCode)
		{
			g_fPitch += fTurnSpeed;
		}

		if (VK_DOWN == n16KeyCode)
		{
			g_fPitch -= fTurnSpeed;
		}

		if (VK_RIGHT == n16KeyCode)
		{
			g_fYaw -= fTurnSpeed;
		}

		if (VK_LEFT == n16KeyCode)
		{
			g_fYaw += fTurnSpeed;
		}

		// Prevent looking too far up or down.
		g_fPitch = min(g_fPitch, XM_PIDIV4);
		g_fPitch = max(-XM_PIDIV4, g_fPitch);

		// Move the camera in model space.
		float x = move.x * -cosf(g_fYaw) - move.z * sinf(g_fYaw);
		float z = move.x * sinf(g_fYaw) - move.z * cosf(g_fYaw);
		g_f3EyePos.x += x * fMoveSpeed;
		g_f3EyePos.z += z * fMoveSpeed;

		// Determine the look direction.
		float r = cosf(g_fPitch);
		g_f3LockAt.x = r * sinf(g_fYaw);
		g_f3LockAt.y = sinf(g_fPitch);
		g_f3LockAt.z = r * cosf(g_fYaw);

		if (VK_TAB == n16KeyCode)
		{//按Tab键还原摄像机位置
			g_f3EyePos = XMFLOAT3(0.0f, 0.0f, -10.0f); //眼睛位置
			g_f3LockAt = XMFLOAT3(0.0f, 0.0f, 0.0f);    //眼睛所盯的位置
			g_f3HeapUp = XMFLOAT3(0.0f, 1.0f, 0.0f);    //头部正上方位置
		}

	}

	break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

BOOL LoadMeshVertex(const CHAR*pszMeshFileName, UINT&nVertexCnt, ST_GRS_VERTEX*&ppVertex, UINT*&ppIndices)
{
	ifstream fin;
	char input;
	BOOL bRet = TRUE;
	try
	{
		fin.open(pszMeshFileName);
		if (fin.fail())
		{
			throw CGRSCOMException(E_FAIL);
		}
		fin.get(input);
		while (input != ':')
		{
			fin.get(input);
		}
		fin >> nVertexCnt;

		fin.get(input);
		while (input != ':')
		{
			fin.get(input);
		}
		fin.get(input);
		fin.get(input);

		ppVertex = (ST_GRS_VERTEX*)GRS_CALLOC( nVertexCnt * sizeof(ST_GRS_VERTEX));
		ppIndices = (UINT*) GRS_CALLOC( nVertexCnt * sizeof(UINT));

		for (UINT i = 0; i < nVertexCnt; i++)
		{
			fin >> ppVertex[i].m_v4Position.x >> ppVertex[i].m_v4Position.y >> ppVertex[i].m_v4Position.z;
			ppVertex[i].m_v4Position.w = 1.0f;
			fin >> ppVertex[i].m_vTex.x >> ppVertex[i].m_vTex.y;
			fin >> ppVertex[i].m_vNor.x >> ppVertex[i].m_vNor.y >> ppVertex[i].m_vNor.z;

			ppIndices[i] = i;
		}
	}
	catch (CGRSCOMException& e)
	{
		e;
		bRet = FALSE;
	}
	return bRet;
}
