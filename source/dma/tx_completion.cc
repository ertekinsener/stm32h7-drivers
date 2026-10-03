#include "dma/tx_completion.h"

#include <utility>

namespace dma {
namespace {

constexpr ULONG kAllEvents = 0x1F;

}  // namespace

TxCompletion::~TxCompletion() {
  if (created_) {
    tx_event_flags_delete(&flags_);
  }
}

std::expected<void, Error> TxCompletion::Create(const char* name) noexcept {
  if (created_) {
    return {};
  }
  // ThreadX only stores the name pointer and never writes through it.
  if (tx_event_flags_create(&flags_, const_cast<CHAR*>(name)) != TX_SUCCESS) {
    return std::unexpected(Error::kRtos);
  }
  created_ = true;
  return {};
}

std::expected<Event, Error> TxCompletion::Wait(ULONG timeout_ticks) noexcept {
  ULONG actual = 0;
  switch (tx_event_flags_get(&flags_, kAllEvents, TX_OR_CLEAR, &actual,
                             timeout_ticks)) {
    case TX_SUCCESS:
      break;
    case TX_NO_EVENTS:
      return std::unexpected(Error::kTimeout);
    default:
      return std::unexpected(Error::kRtos);
  }

  const auto events = static_cast<Event>(actual);
  if (Any(events & kErrorEvents)) {
    return std::unexpected(Error::kTransferError);
  }
  return events;
}

void TxCompletion::OnEvent(void* context, Event events) noexcept {
  auto* self = static_cast<TxCompletion*>(context);
  tx_event_flags_set(&self->flags_, std::to_underlying(events), TX_OR);
}

}  // namespace dma
