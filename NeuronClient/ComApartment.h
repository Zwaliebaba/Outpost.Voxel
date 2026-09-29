#pragma once

#include "WindowsSdk.h"

#include <objbase.h>

namespace NeuronClient
{

// COM on the calling thread for as long as this lives, which WIC's objects need (Design/ADR/ADR-031). The thread joins the
// multithreaded apartment, unless it is in a single-threaded one already, which those objects work in as well. Throws
// winrt::hresult_error when COM cannot start.
class ComApartment
{
public:
  ComApartment()
  {
    // RPC_E_CHANGED_MODE is the single-threaded apartment the thread is in already, which this leaves as it is. Any other
    // success, S_FALSE among them, is balanced when this goes.
    const HRESULT entered = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (entered != RPC_E_CHANGED_MODE)
    {
      winrt::check_hresult(entered);
      m_entered = true;
    }
  }

  ~ComApartment()
  {
    if (m_entered)
    {
      CoUninitialize();
    }
  }

  ComApartment(const ComApartment&) = delete;
  ComApartment& operator=(const ComApartment&) = delete;
  ComApartment(ComApartment&&) = delete;
  ComApartment& operator=(ComApartment&&) = delete;

private:
  bool m_entered = false;
};

} // namespace NeuronClient
