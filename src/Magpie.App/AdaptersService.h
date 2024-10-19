#pragma once
#include <dxgi1_6.h>
#include <WinRTUtils.h>
#include <SmallVector.h>

namespace winrt::Magpie::App {

struct AdapterInfo {
	uint32_t idx = 0;
	uint32_t vendorId = 0;
	uint32_t deviceId = 0;
	std::wstring description;
	// 延迟检测
	bool isFL11Supported = true;
};

class AdaptersService {
public:
	static AdaptersService& Get() noexcept {
		static AdaptersService instance;
		return instance;
	}

	bool Initialize() noexcept;

	void Uninitialize() noexcept;

	const SmallVectorImpl<AdapterInfo>& AdapterInfos() const noexcept {
		return _adapterInfos;
	}

	WinRTUtils::Event<delegate<>> AdaptersChanged;

private:
	AdaptersService() = default;

	bool _InitializeDXGI() noexcept;

	com_ptr<IDXGIFactory7> _dxgiFactory;
	wil::unique_event_nothrow _adaptersChangedEvent;
	DWORD _adaptersChangedCookie = 0;

	uint32_t _adapterInfosVersion = 0;
	SmallVector<AdapterInfo, 2> _adapterInfos;
};

}
