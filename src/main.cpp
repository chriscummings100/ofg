// Native OFG entry point. Verifies Slang RHI startup before the laboratory gains a window and rendering.
#include <slang-rhi.h>
#include <slang-rhi/agility-sdk.h>

#include <cstdio>

// Select the Agility SDK runtime copied beside the executable by Slang RHI's build.
SLANG_RHI_EXPORT_AGILITY_SDK

// Creates a D3D12 device, reports the selected adapter, and returns failure if initialization cannot complete.
int main()
{
    rhi::DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = rhi::DeviceType::D3D12;
    deviceDesc.enableValidation = true;

    rhi::ComPtr<rhi::IDevice> device;
    const rhi::Result result = rhi::getRHI()->createDevice(deviceDesc, device.writeRef());
    if (SLANG_FAILED(result))
    {
        std::fprintf(
            stderr,
            "OFG could not create a D3D12 device (result 0x%08X).\n",
            static_cast<unsigned int>(result)
        );
        return 1;
    }

    std::printf("OFG initialized D3D12 on %s.\n", device->getInfo().adapterName);
    return 0;
}
