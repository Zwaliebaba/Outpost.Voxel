#include "pch.h"

#include "GraphicsDevice.h"

#include <array>
#include <cstddef>
#include <format>
#include <utility>

namespace NeuronClient
{
namespace
{

// The debug layer's messages below warning severity are not worth a line.
constexpr std::array<D3D12_MESSAGE_SEVERITY, 2> QUIET_SEVERITIES{D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE};

[[nodiscard]] const char* SeverityName(D3D12_MESSAGE_SEVERITY _severity) noexcept
{
  switch (_severity)
  {
  case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    return "corruption";
  case D3D12_MESSAGE_SEVERITY_ERROR:
    return "error";
  case D3D12_MESSAGE_SEVERITY_WARNING:
    return "warning";
  case D3D12_MESSAGE_SEVERITY_INFO:
    return "info";
  case D3D12_MESSAGE_SEVERITY_MESSAGE:
    return "message";
  }
  return "unknown";
}

[[nodiscard]] std::string AdapterDescription(IDXGIAdapter1* _adapter)
{
  DXGI_ADAPTER_DESC1 desc{};
  if (FAILED(_adapter->GetDesc1(&desc)))
  {
    return "an adapter that cannot describe itself";
  }
  return winrt::to_string(desc.Description);
}

[[nodiscard]] bool IsSoftware(IDXGIAdapter1* _adapter)
{
  DXGI_ADAPTER_DESC1 desc{};
  return SUCCEEDED(_adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

[[nodiscard]] std::string DescribeHresult(HRESULT _result)
{
  return std::format("HRESULT 0x{:08X}: {}", static_cast<std::uint32_t>(_result),
                     winrt::to_string(winrt::hresult_error(_result).message()));
}

} // namespace

GraphicsDevice::GraphicsDevice(const GraphicsDeviceDesc& _desc)
{
  // The debug layer and DRED are switched on before the device exists, or not at all.
  UINT factoryFlags = 0;
  if (_desc.debugLayer)
  {
    winrt::com_ptr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.put()))))
    {
      debug->EnableDebugLayer();
      m_debugLayer = DebugLayerState::On;
      factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
      if (_desc.gpuBasedValidation)
      {
        if (const winrt::com_ptr<ID3D12Debug1> debug1 = debug.try_as<ID3D12Debug1>())
        {
          debug1->SetEnableGPUBasedValidation(TRUE);
        }
      }
    }
    else
    {
      m_debugLayer = DebugLayerState::Unavailable;
    }
  }
  winrt::com_ptr<ID3D12DeviceRemovedExtendedDataSettings> dred;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(dred.put()))))
  {
    dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  }

  // The DXGI debug layer comes with the Direct3D one but is not guaranteed to, so a factory that cannot have it is made
  // without it.
  if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.put()))))
  {
    m_factory = nullptr;
    winrt::check_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(m_factory.put())));
  }

  std::vector<std::string> passedOver;
  if (_desc.warp)
  {
    winrt::com_ptr<IDXGIAdapter1> warp;
    winrt::check_hresult(m_factory->EnumWarpAdapter(IID_PPV_ARGS(warp.put())));
    if (const std::optional<std::string> lacks = TryAdapter(warp.get()))
    {
      passedOver.push_back(std::format("{}: {}", AdapterDescription(warp.get()), *lacks));
    }
  }
  else
  {
    for (UINT index = 0;; ++index)
    {
      winrt::com_ptr<IDXGIAdapter1> adapter;
      const HRESULT result =
        m_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.put()));
      if (result == DXGI_ERROR_NOT_FOUND)
      {
        break;
      }
      winrt::check_hresult(result);
      // Without --adapter, only hardware: the Microsoft Basic Render Driver is WARP, and --warp asks for that by name.
      if (_desc.adapter ? *_desc.adapter != index : IsSoftware(adapter.get()))
      {
        continue;
      }
      const std::optional<std::string> lacks = TryAdapter(adapter.get());
      if (!lacks)
      {
        break;
      }
      passedOver.push_back(std::format("adapter {} ({}): {}", index, AdapterDescription(adapter.get()), *lacks));
    }
  }
  if (!m_device)
  {
    std::string message = "No adapter offers Direct3D feature level 12_1 with Shader Model 6.0.";
    if (_desc.adapter && passedOver.empty())
    {
      message = std::format("There is no adapter {} in high-performance order.", *_desc.adapter);
    }
    for (const std::string& line : passedOver)
    {
      message += "\n" + line;
    }
    throw winrt::hresult_error(DXGI_ERROR_UNSUPPORTED, winrt::to_hstring(message));
  }

  if (m_debugLayer == DebugLayerState::On)
  {
    m_infoQueue = m_device.try_as<ID3D12InfoQueue>();
  }
  if (m_infoQueue)
  {
    std::array<D3D12_MESSAGE_SEVERITY, QUIET_SEVERITIES.size()> denied = QUIET_SEVERITIES;
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumSeverities = static_cast<UINT>(denied.size());
    filter.DenyList.pSeverityList = denied.data();
    winrt::check_hresult(m_infoQueue->PushStorageFilter(&filter));
    // Breaking into a debugger that is not there ends the process, so only a debugger gets a break.
    if (IsDebuggerPresent())
    {
      m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
      m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    }
  }

  const D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
                                           D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
  winrt::check_hresult(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_queue.put())));
  m_queue->SetName(L"Direct queue");
  winrt::check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())));
  m_fenceEvent.attach(winrt::check_pointer(CreateEventW(nullptr, FALSE, FALSE, nullptr)));

  winrt::check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_executeAllocator.put())));
  winrt::check_hresult(
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_executeAllocator.get(), nullptr, IID_PPV_ARGS(m_executeList.put())));
  m_executeList->SetName(L"One-off commands");
  winrt::check_hresult(m_executeList->Close());
}

GraphicsDevice::~GraphicsDevice()
{
  // Nothing the GPU still reads may be released under it, and a device lost on the way out has nothing left to report.
  if (m_queue && m_fence && m_fenceEvent)
  {
    const UINT64 value = ++m_fenceValue;
    if (SUCCEEDED(m_queue->Signal(m_fence.get(), value)) && m_fence->GetCompletedValue() < value &&
        SUCCEEDED(m_fence->SetEventOnCompletion(value, m_fenceEvent.get())))
    {
      WaitForSingleObject(m_fenceEvent.get(), INFINITE);
    }
  }
}

std::optional<std::string> GraphicsDevice::TryAdapter(IDXGIAdapter1* _adapter)
{
  winrt::com_ptr<ID3D12Device> device;
  const HRESULT result = D3D12CreateDevice(_adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.put()));
  if (FAILED(result))
  {
    return std::format("no feature level 12_1 ({})", DescribeHresult(result));
  }
  D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_0};
  if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) ||
      shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0)
  {
    return std::string("no Shader Model 6.0");
  }
  m_adapter.copy_from(_adapter);
  m_device = std::move(device);
  m_adapterName = winrt::to_hstring(AdapterDescription(_adapter)).c_str();
  return std::nullopt;
}

std::uint64_t GraphicsDevice::Signal()
{
  ++m_fenceValue;
  winrt::check_hresult(m_queue->Signal(m_fence.get(), m_fenceValue));
  return m_fenceValue;
}

void GraphicsDevice::WaitFor(std::uint64_t _value)
{
  if (m_fence->GetCompletedValue() < _value)
  {
    winrt::check_hresult(m_fence->SetEventOnCompletion(_value, m_fenceEvent.get()));
    WaitForSingleObject(m_fenceEvent.get(), INFINITE);
  }
  // A removed device signals every fence to UINT64_MAX, so a wait always ends; this says why it ended.
  winrt::check_hresult(m_device->GetDeviceRemovedReason());
}

void GraphicsDevice::Flush()
{
  WaitFor(Signal());
}

void GraphicsDevice::Execute(const std::function<void(ID3D12GraphicsCommandList*)>& _record)
{
  winrt::check_hresult(m_executeAllocator->Reset());
  winrt::check_hresult(m_executeList->Reset(m_executeAllocator.get(), nullptr));
  _record(m_executeList.get());
  winrt::check_hresult(m_executeList->Close());
  std::array<ID3D12CommandList*, 1> lists{m_executeList.get()};
  m_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
  Flush();
}

std::string GraphicsDevice::DescribeRemoval() const
{
  std::string message;
  const HRESULT reason = m_device ? m_device->GetDeviceRemovedReason() : S_OK;
  if (FAILED(reason))
  {
    message = "The device was removed: " + DescribeHresult(reason);
    if (const winrt::com_ptr<ID3D12DeviceRemovedExtendedData> dred = m_device.try_as<ID3D12DeviceRemovedExtendedData>())
    {
      D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
      if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&breadcrumbs)))
      {
        // A command list whose last completed breadcrumb falls short of its count is one the GPU was inside.
        for (const D3D12_AUTO_BREADCRUMB_NODE* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext)
        {
          const UINT32 completed = node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0u;
          if (completed >= node->BreadcrumbCount || node->pCommandHistory == nullptr)
          {
            continue;
          }
          const std::string name =
            node->pCommandListDebugNameW != nullptr ? winrt::to_string(node->pCommandListDebugNameW) : "an unnamed command list";
          message += std::format("\nDRED: {} completed {} of {} operations; the next was D3D12_AUTO_BREADCRUMB_OP {}", name, completed,
                                 node->BreadcrumbCount, static_cast<int>(node->pCommandHistory[completed]));
        }
      }
      D3D12_DRED_PAGE_FAULT_OUTPUT pageFault{};
      if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pageFault)) && pageFault.PageFaultVA != 0)
      {
        message += std::format("\nDRED: page fault at GPU address 0x{:016X}", pageFault.PageFaultVA);
      }
    }
  }
  for (const std::string& line : TakeDebugMessages())
  {
    message += (message.empty() ? "" : "\n") + line;
  }
  return message;
}

std::vector<std::string> GraphicsDevice::TakeDebugMessages() const
{
  std::vector<std::string> messages;
  if (!m_infoQueue)
  {
    return messages;
  }
  const UINT64 count = m_infoQueue->GetNumStoredMessages();
  for (UINT64 i = 0; i < count; ++i)
  {
    SIZE_T length = 0;
    if (FAILED(m_infoQueue->GetMessage(i, nullptr, &length)))
    {
      continue;
    }
    std::vector<std::byte> storage(length);
    auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
    if (SUCCEEDED(m_infoQueue->GetMessage(i, message, &length)))
    {
      const std::size_t textLength = message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0;
      messages.push_back(std::format("D3D12 {}: {}", SeverityName(message->Severity), std::string(message->pDescription, textLength)));
    }
  }
  m_infoQueue->ClearStoredMessages();
  return messages;
}

} // namespace NeuronClient
