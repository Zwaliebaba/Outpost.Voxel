#include "pch.h"

#include "GpuResources.h"
#include "GraphicsDevice.h"
#include "InstanceConstants.h"
#include "PaletteConstants.h"
#include "TestSupport.h"
#include "UploadRing.h"
#include "ViewConstants.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <type_traits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{
namespace
{

#include "Shaders/LayoutEchoCS.h"

enum RootParameter : std::uint8_t
{
  ViewParameter,
  InstanceParameter,
  PaletteParameter,
  EchoParameter,
  RootParameterCount
};

// Fills a constants struct word by word with values that name the struct and the word, and that are ordinary floats, so
// that nothing between the CPU and the shader can mistake one for a NaN or a denormal and change its bits.
template <typename T> [[nodiscard]] T Sentinel(std::uint32_t _structIndex) noexcept
{
  static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % sizeof(std::uint32_t) == 0);
  std::array<std::uint32_t, sizeof(T) / sizeof(std::uint32_t)> words{};
  for (std::uint32_t i = 0; i < words.size(); ++i)
  {
    words[i] = 0x40000000u | (_structIndex << 16u) | i;
  }
  T value{};
  std::memcpy(&value, words.data(), sizeof(T));
  return value;
}

template <typename T> void AppendWords(std::vector<std::uint32_t>& _words, const T& _value)
{
  const std::size_t at = _words.size();
  _words.resize(at + sizeof(T) / sizeof(std::uint32_t));
  std::memcpy(_words.data() + at, &_value, sizeof(T));
}

} // namespace

// R16: the C++ constant structs and their HLSL mirrors agree on every field (Design/SampleRenderer.md §7.4, §14).
TEST_CLASS(LayoutEchoTests)
{
public:
  TEST_METHOD(MirrorsReadEveryFieldWhereTheStructPutsIt)
  {
    RunGpuTest(
      [](NeuronClient::GraphicsDevice& _device)
      {
        const auto view = Sentinel<NeuronClient::ViewConstants>(1);
        const auto instance = Sentinel<NeuronClient::InstanceConstants>(2);
        const auto palette = Sentinel<NeuronClient::PaletteConstants>(3);
        std::vector<std::uint32_t> expected;
        AppendWords(expected, view);
        AppendWords(expected, instance);
        AppendWords(expected, palette);

        NeuronClient::UploadRing constants(_device, TEST_CONSTANTS_BYTES, L"Layout echo constants");
        const D3D12_GPU_VIRTUAL_ADDRESS viewAddress = constants.Push(view);
        const D3D12_GPU_VIRTUAL_ADDRESS instanceAddress = constants.Push(instance);
        const D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = constants.Push(palette);
        // One word more than the mirrors hold, still zero afterwards, shows the echo wrote nothing past them.
        const std::uint64_t echoBytes = (expected.size() + 1) * sizeof(std::uint32_t);
        const std::vector<std::byte> zeros(echoBytes);
        const winrt::com_ptr<ID3D12Resource> echo =
          NeuronClient::CreateStaticBuffer(_device, zeros, L"Layout echo", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        std::array<D3D12_ROOT_PARAMETER, RootParameterCount> parameters{};
        parameters[ViewParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[ViewParameter].Descriptor = {0, 0};
        parameters[InstanceParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[InstanceParameter].Descriptor = {1, 0};
        parameters[PaletteParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[PaletteParameter].Descriptor = {2, 0};
        parameters[EchoParameter].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[EchoParameter].Descriptor = {0, 0};
        const D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                          D3D12_ROOT_SIGNATURE_FLAG_NONE};
        const winrt::com_ptr<ID3D12RootSignature> rootSignature =
          NeuronClient::CreateRootSignature(_device, rootSignatureDesc, L"Layout echo root signature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
        pipelineDesc.pRootSignature = rootSignature.get();
        pipelineDesc.CS = {LAYOUT_ECHO_CS, sizeof(LAYOUT_ECHO_CS)};
        winrt::com_ptr<ID3D12PipelineState> pipeline;
        winrt::check_hresult(_device.Device()->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(pipeline.put())));

        _device.Execute(
          [&](ID3D12GraphicsCommandList* _list)
          {
            const D3D12_RESOURCE_BARRIER toWrite =
              NeuronClient::Transition(echo.get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            _list->ResourceBarrier(1, &toWrite);
            _list->SetComputeRootSignature(rootSignature.get());
            _list->SetPipelineState(pipeline.get());
            _list->SetComputeRootConstantBufferView(ViewParameter, viewAddress);
            _list->SetComputeRootConstantBufferView(InstanceParameter, instanceAddress);
            _list->SetComputeRootConstantBufferView(PaletteParameter, paletteAddress);
            _list->SetComputeRootUnorderedAccessView(EchoParameter, echo->GetGPUVirtualAddress());
            _list->Dispatch(1, 1, 1);
          });

        // A buffer decays to COMMON when the command list that used it completes.
        const std::vector<std::byte> bytes = NeuronClient::ReadBuffer(_device, echo.get(), D3D12_RESOURCE_STATE_COMMON, echoBytes);
        std::vector<std::uint32_t> echoed(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(echoed.data(), bytes.data(), bytes.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
          Assert::AreEqual(expected[i], echoed[i], std::format(L"word {} of the echo", i).c_str());
        }
        Assert::AreEqual(0u, echoed[expected.size()], L"the echo wrote past the mirrors");
      });
  }
};

} // namespace NeuronClientTests
