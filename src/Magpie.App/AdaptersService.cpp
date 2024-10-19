#include "pch.h"
#include "AdaptersService.h"
#include "Logger.h"
#include <d3d11.h>
#include "Win32Utils.h"

namespace winrt::Magpie::App {

bool AdaptersService::Initialize() noexcept {
	if (!_InitializeDXGI()) {
		return false;
	}

	[]()->fire_and_forget {
		AdaptersService& that = AdaptersService::Get();
		CoreDispatcher dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();

		while (true) {
			co_await resume_background();

			if (!that._adaptersChangedEvent.wait()) {
				co_return;
			}

			// _adaptersChangedCookie 由 _adaptersChangedEvent 保护。
			// _adaptersChangedCookie 为零表示正在退出，否则表示显卡变化。
			if (that._adaptersChangedCookie == 0) {
				co_return;
			}

			co_await dispatcher;

			that._dxgiFactory->UnregisterAdaptersChangedEvent(that._adaptersChangedCookie);
			if (!that._InitializeDXGI()) {
				co_return;
			}

			that.AdaptersChanged.Invoke();
		}
	}();

	return true;
}

void AdaptersService::Uninitialize() noexcept {
	_dxgiFactory->UnregisterAdaptersChangedEvent(_adaptersChangedCookie);
	_adaptersChangedCookie = 0;
	_adaptersChangedEvent.SetEvent();
	_adaptersChangedEvent.reset();
	_dxgiFactory = nullptr;
}

bool AdaptersService::_InitializeDXGI() noexcept {
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory));
	if (FAILED(hr)) {
		Logger::Get().ComError("CreateDXGIFactory1 失败", hr);
		return false;
	}

	if (!_adaptersChangedEvent) {
		hr = _adaptersChangedEvent.create();
		if (FAILED(hr)) {
			Logger::Get().ComError("创建 event 失败", hr);
			return false;
		}
	}

	hr = _dxgiFactory->RegisterAdaptersChangedEvent(
		_adaptersChangedEvent.get(), &_adaptersChangedCookie);
	if (FAILED(hr)) {
		Logger::Get().ComError("RegisterAdaptersChangedEvent 失败", hr);
		return false;
	}

	_adapterInfos.clear();
	++_adapterInfosVersion;

	SmallVector<com_ptr<IDXGIAdapter1>> adapters;

	com_ptr<IDXGIAdapter1> curAdapter;
	for (UINT adapterIdx = 0;
		SUCCEEDED(_dxgiFactory->EnumAdapters1(adapterIdx, curAdapter.put()));
		++adapterIdx
	) {
		DXGI_ADAPTER_DESC1 desc;
		hr = curAdapter->GetDesc1(&desc);
		if (FAILED(hr)) {
			continue;
		}

		// 不包含 WARP
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}

		_adapterInfos.push_back({
			.idx = adapterIdx,
			.vendorId = desc.VendorId,
			.deviceId = desc.DeviceId,
			.description = desc.Description
		});

		adapters.push_back(std::move(curAdapter));
	}

	if (!_adapterInfos.empty()) {
		// 检查每个显卡的功能级别，由于过于耗时转到后台执行
		[](SmallVector<com_ptr<IDXGIAdapter1>> adapters)->fire_and_forget {
			AdaptersService& that = AdaptersService::Get();
			CoreDispatcher dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();

			const uint32_t adapterInfosVersion = that._adapterInfosVersion;

			co_await resume_background();

			SmallVector<uint32_t> oldAdapters;
			{
				wil::srwlock writeLock;
				Win32Utils::RunParallel([&](uint32_t i) {
					D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
					if (FAILED(D3D11CreateDevice(adapters[i].get(), D3D_DRIVER_TYPE_UNKNOWN,
						NULL, 0, &fl, 1, D3D11_SDK_VERSION, nullptr, nullptr, nullptr))) {
						auto lock = writeLock.lock_exclusive();
						oldAdapters.push_back(i);
					}
				}, (uint32_t)adapters.size());
			}

			co_await dispatcher;

			if (adapterInfosVersion != that._adapterInfosVersion) {
				// 检查过程中显卡变化则放弃后续处理
				co_return;
			}

			for (uint32_t i : oldAdapters) {
				that._adapterInfos[i].isFL11Supported = false;
			}

			if (!oldAdapters.empty()) {
				that.AdaptersChanged.Invoke();
			}
		}(std::move(adapters));
	}

	return true;
}

}
